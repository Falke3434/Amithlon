/*
 * drivers/char/vme_scc.c: MVME147, MVME162, BVME6000 SCC serial ports
 * implementation.
 * Copyright 1999 Richard Hirst <richard@sleepie.demon.co.uk>
 *
 * Based on atari_SCC.c which was
 *   Copyright 1994-95 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 *   Partially based on PC-Linux serial.c by Linus Torvalds and Theodore Ts'o
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/kdev_t.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/mm.h>
#include <linux/serial.h>
#include <linux/fcntl.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/tqueue.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/console.h>
#include <linux/init.h>
#include <asm/setup.h>
#include <asm/bootinfo.h>

#ifdef CONFIG_MVME147_SCC
#include <asm/mvme147hw.h>
#endif
#ifdef CONFIG_MVME162_SCC
#include <asm/mvme16xhw.h>
#endif
#ifdef CONFIG_BVME6000_SCC
#include <asm/bvme6000hw.h>
#endif

#include <linux/generic_serial.h>
#include "scc.h"


#define CHANNEL_A	0
#define CHANNEL_B	1

#define SCC_MINOR_BASE	64

/* Shadows for all SCC write registers */
static unsigned char scc_shadow[2][16];

/* Location to access for SCC register access delay */
static volatile unsigned char *scc_del = NULL;

/* To keep track of STATUS_REG state for detection of Ext/Status int source */
static unsigned char scc_last_status_reg[2];

/***************************** Prototypes *****************************/

/* Function prototypes */
static void scc_disable_tx_interrupts(void * ptr);
static void scc_enable_tx_interrupts(void * ptr);
static void scc_disable_rx_interrupts(void * ptr);
static void scc_enable_rx_interrupts(void * ptr);
static int  scc_get_CD(void * ptr);
static void scc_shutdown_port(void * ptr);
static int scc_set_real_termios(void  *ptr);
static void scc_hungup(void  *ptr);
static void scc_close(void  *ptr);
static int scc_chars_in_buffer(void * ptr);
static int scc_open(struct tty_struct * tty, struct file * filp);
static int scc_ioctl(struct tty_struct * tty, struct file * filp,
                     unsigned int cmd, unsigned long arg);
static void scc_throttle(struct tty_struct *tty);
static void scc_unthrottle(struct tty_struct *tty);
static void scc_tx_int(int irq, void *data, struct pt_regs *fp);
static void scc_rx_int(int irq, void *data, struct pt_regs *fp);
static void scc_stat_int(int irq, void *data, struct pt_regs *fp);
static void scc_spcond_int(int irq, void *data, struct pt_regs *fp);
static void scc_setsignals(struct scc_port *port, int dtr, int rts);
static void scc_break_ctl(struct tty_struct *tty, int break_state);

static struct tty_driver scc_driver, scc_callout_driver;

static struct tty_struct *scc_table[2] = { NULL, };
static struct termios * scc_termios[2];
static struct termios * scc_termios_locked[2];
struct scc_port scc_ports[2];

int scc_refcount;
int scc_initialized = 0;

/*---------------------------------------------------------------------------
 * Interface from generic_serial.c back here
 *--------------------------------------------------------------------------*/

static struct real_driver scc_real_driver = {
        scc_disable_tx_interrupts,
        scc_enable_tx_interrupts,
        scc_disable_rx_interrupts,
        scc_enable_rx_interrupts,
        scc_get_CD,
        scc_shutdown_port,
        scc_set_real_termios,
        scc_chars_in_buffer,
        scc_close,
        scc_hungup,
        NULL
};


/*----------------------------------------------------------------------------
 * vme_scc_init() and support functions
 *---------------------------------------------------------------------------*/

static int scc_init_drivers(void)
{
	int error;

	memset(&scc_driver, 0, sizeof(scc_driver));
	scc_driver.magic = TTY_DRIVER_MAGIC;
	scc_driver.driver_name = "scc";
#ifdef CONFIG_DEVFS_FS
	scc_driver.name = "tts/%d";
#else
	scc_driver.name = "ttyS";
#endif
	scc_driver.major = TTY_MAJOR;
	scc_driver.minor_start = SCC_MINOR_BASE;
	scc_driver.num = 2;
	scc_driver.type = TTY_DRIVER_TYPE_SERIAL;
	scc_driver.subtype = SERIAL_TYPE_NORMAL;
	scc_driver.init_termios = tty_std_termios;
	scc_driver.init_termios.c_cflag =
	  B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	scc_driver.flags = TTY_DRIVER_REAL_RAW;
	scc_driver.refcount = &scc_refcount;
	scc_driver.table = scc_table;
	scc_driver.termios = scc_termios;
	scc_driver.termios_locked = scc_termios_locked;

	scc_driver.open	= scc_open;
	scc_driver.close = gs_close;
	scc_driver.write = gs_write;
	scc_driver.put_char = gs_put_char;
	scc_driver.flush_chars = gs_flush_chars;
	scc_driver.write_room = gs_write_room;
	scc_driver.chars_in_buffer = gs_chars_in_buffer;
	scc_driver.flush_buffer = gs_flush_buffer;
	scc_driver.ioctl = scc_ioctl;
	scc_driver.throttle = scc_throttle;
	scc_driver.unthrottle = scc_unthrottle;
	scc_driver.set_termios = gs_set_termios;
	scc_driver.stop = gs_stop;
	scc_driver.start = gs_start;
	scc_driver.hangup = gs_hangup;
	scc_driver.break_ctl = scc_break_ctl;

	scc_callout_driver = scc_driver;
#ifdef CONFIG_DEVFS_FS
	scc_callout_driver.name = "cua/%d";
#else
	scc_callout_driver.name = "cua";
#endif
	scc_callout_driver.major = TTYAUX_MAJOR;
	scc_callout_driver.subtype = SERIAL_TYPE_CALLOUT;

	if ((error = tty_register_driver(&scc_driver))) {
		printk(KERN_ERR "scc: Couldn't register scc driver, error = %d\n",
		       error);
		return 1;
	}
	if ((error = tty_register_driver(&scc_callout_driver))) {
		tty_unregister_driver(&scc_driver);
		printk(KERN_ERR "scc: Couldn't register scc callout driver, error = %d\n",
		       error);
		return 1;
	}

	return 0;
}


/* ports[] array is indexed by line no (i.e. [0] for ttyS0, [1] for ttyS1).
 */

static void scc_init_portstructs(void)
{
	struct scc_port *port;
	int i;

	for (i = 0; i < 2; i++) {
		port = scc_ports + i;
		port->gs.callout_termios = tty_std_termios;
		port->gs.normal_termios = tty_std_termios;
		port->gs.magic = SCC_MAGIC;
		port->gs.close_delay = HZ/2;
		port->gs.closing_wait = 30 * HZ;
		port->gs.rd = &scc_real_driver;
#ifdef NEW_WRITE_LOCKING
		port->gs.port_write_sem = MUTEX;
#endif
		init_waitqueue_head(&port->gs.open_wait);
		init_waitqueue_head(&port->gs.close_wait);
	}
}


#ifdef CONFIG_MVME147_SCC
static int mvme147_scc_init(void)
{
	struct scc_port *port;

	printk(KERN_INFO "SCC: MVME147 Serial Driver\n");
	/* Init channel A */
	port = &scc_ports[0];
	port->channel = CHANNEL_A;
	port->ctrlp = (volatile unsigned char *)M147_SCC_A_ADDR;
	port->datap = port->ctrlp + 1;
	port->port_a = &scc_ports[0];
	port->port_b = &scc_ports[1];
	request_irq(MVME147_IRQ_SCCA_TX, scc_tx_int, SA_INTERRUPT,
		            "SCC-A TX", port);
	request_irq(MVME147_IRQ_SCCA_STAT, scc_stat_int, SA_INTERRUPT,
		            "SCC-A status", port);
	request_irq(MVME147_IRQ_SCCA_RX, scc_rx_int, SA_INTERRUPT,
		            "SCC-A RX", port);
	request_irq(MVME147_IRQ_SCCA_SPCOND, scc_spcond_int, SA_INTERRUPT,
		            "SCC-A special cond", port);
	{
		SCC_ACCESS_INIT(port);

		/* disable interrupts for this channel */
		SCCwrite(INT_AND_DMA_REG, 0);
		/* Set the interrupt vector */
		SCCwrite(INT_VECTOR_REG, MVME147_IRQ_SCC_BASE);
		/* Interrupt parameters: vector includes status, status low */
		SCCwrite(MASTER_INT_CTRL, MIC_VEC_INCL_STAT);
		SCCmod(MASTER_INT_CTRL, 0xff, MIC_MASTER_INT_ENAB);
	}

	/* Init channel B */
	port = &scc_ports[1];
	port->channel = CHANNEL_B;
	port->ctrlp = (volatile unsigned char *)M147_SCC_B_ADDR;
	port->datap = port->ctrlp + 1;
	port->port_a = &scc_ports[0];
	port->port_b = &scc_ports[1];
	request_irq(MVME147_IRQ_SCCB_TX, scc_tx_int, SA_INTERRUPT,
		            "SCC-B TX", port);
	request_irq(MVME147_IRQ_SCCB_STAT, scc_stat_int, SA_INTERRUPT,
		            "SCC-B status", port);
	request_irq(MVME147_IRQ_SCCB_RX, scc_rx_int, SA_INTERRUPT,
		            "SCC-B RX", port);
	request_irq(MVME147_IRQ_SCCB_SPCOND, scc_spcond_int, SA_INTERRUPT,
		            "SCC-B special cond", port);
	{
		SCC_ACCESS_INIT(port);

		/* disable interrupts for this channel */
		SCCwrite(INT_AND_DMA_REG, 0);
	}

        /* Ensure interrupts are enabled in the PCC chip */
        m147_pcc->serial_cntrl=PCC_LEVEL_SERIAL|PCC_INT_ENAB;

	/* Initialise the tty driver structures and register */
	scc_init_portstructs();
	scc_init_drivers();

	return 0;
}
#endif


#ifdef CONFIG_MVME162_SCC
static int mvme162_scc_init(void)
{
	struct scc_port *port;

	if (!(mvme16x_config & MVME16x_CONFIG_GOT_SCCA))
		return (-ENODEV);

	printk(KERN_INFO "SCC: MVME162 Serial Driver\n");
	/* Init channel A */
	port = &scc_ports[0];
	port->channel = CHANNEL_A;
	port->ctrlp = (volatile unsigned char *)MVME_SCC_A_ADDR;
	port->datap = port->ctrlp + 2;
	port->port_a = &scc_ports[0];
	port->port_b = &scc_ports[1];
	request_irq(MVME162_IRQ_SCCA_TX, scc_tx_int, SA_INTERRUPT,
		            "SCC-A TX", port);
	request_irq(MVME162_IRQ_SCCA_STAT, scc_stat_int, SA_INTERRUPT,
		            "SCC-A status", port);
	request_irq(MVME162_IRQ_SCCA_RX, scc_rx_int, SA_INTERRUPT,
		            "SCC-A RX", port);
	request_irq(MVME162_IRQ_SCCA_SPCOND, scc_spcond_int, SA_INTERRUPT,
		            "SCC-A special cond", port);
	{
		SCC_ACCESS_INIT(port);

		/* disable interrupts for this channel */
		SCCwrite(INT_AND_DMA_REG, 0);
		/* Set the interrupt vector */
		SCCwrite(INT_VECTOR_REG, MVME162_IRQ_SCC_BASE);
		/* Interrupt parameters: vector includes status, status low */
		SCCwrite(MASTER_INT_CTRL, MIC_VEC_INCL_STAT);
		SCCmod(MASTER_INT_CTRL, 0xff, MIC_MASTER_INT_ENAB);
	}

	/* Init channel B */
	port = &scc_ports[1];
	port->channel = CHANNEL_B;
	port->ctrlp = (volatile unsigned char *)MVME_SCC_B_ADDR;
	port->datap = port->ctrlp + 2;
	port->port_a = &scc_ports[0];
	port->port_b = &scc_ports[1];
	request_irq(MVME162_IRQ_SCCB_TX, scc_tx_int, SA_INTERRUPT,
		            "SCC-B TX", port);
	request_irq(MVME162_IRQ_SCCB_STAT, scc_stat_int, SA_INTERRUPT,
		            "SCC-B status", port);
	request_irq(MVME162_IRQ_SCCB_RX, scc_rx_int, SA_INTERRUPT,
		            "SCC-B RX", port);
	request_irq(MVME162_IRQ_SCCB_SPCOND, scc_spcond_int, SA_INTERRUPT,
		            "SCC-B special cond", port);

	{
		SCC_ACCESS_INIT(port);	/* Either channel will do */

		/* disable interrupts for this channel */
		SCCwrite(INT_AND_DMA_REG, 0);
	}

        /* Ensure interrupts are enabled in the MC2 chip */
        *(volatile char *)0xfff4201d = 0x14;

	/* Initialise the tty driver structures and register */
	scc_init_portstructs();
	scc_init_drivers();

	return 0;
}
#endif


#ifdef CONFIG_BVME6000_SCC
static int bvme6000_scc_init(void)
{
	struct scc_port *port;

	printk(KERN_INFO "SCC: BVME6000 Serial Driver\n");
	/* Init channel A */
	port = &scc_ports[0];
	port->channel = CHANNEL_A;
	port->ctrlp = (volatile unsigned char *)BVME_SCC_A_ADDR;
	port->datap = port->ctrlp + 4;
	port->port_a = &scc_ports[0];
	port->port_b = &scc_ports[1];
	request_irq(BVME_IRQ_SCCA_TX, scc_tx_int, SA_INTERRUPT,
		            "SCC-A TX", port);
	request_irq(BVME_IRQ_SCCA_STAT, scc_stat_int, SA_INTERRUPT,
		            "SCC-A status", port);
	request_irq(BVME_IRQ_SCCA_RX, scc_rx_int, SA_INTERRUPT,
		            "SCC-A RX", port);
	request_irq(BVME_IRQ_SCCA_SPCOND, scc_spcond_int, SA_INTERRUPT,
		            "SCC-A special cond", port);
	{
		SCC_ACCESS_INIT(port);

		/* disable interrupts for this channel */
		SCCwrite(INT_AND_DMA_REG, 0);
		/* Set the interrupt vector */
		SCCwrite(INT_VECTOR_REG, BVME_IRQ_SCC_BASE);
		/* Interrupt parameters: vector includes status, status low */
		SCCwrite(MASTER_INT_CTRL, MIC_VEC_INCL_STAT);
		SCCmod(MASTER_INT_CTRL, 0xff, MIC_MASTER_INT_ENAB);
	}

	/* Init channel B */
	port = &scc_ports[1];
	port->channel = CHANNEL_B;
	port->ctrlp = (volatile unsigned char *)BVME_SCC_B_ADDR;
	port->datap = port->ctrlp + 4;
	port->port_a = &scc_ports[0];
	port->port_b = &scc_ports[1];
	request_irq(BVME_IRQ_SCCB_TX, scc_tx_int, SA_INTERRUPT,
		            "SCC-B TX", port);
	request_irq(BVME_IRQ_SCCB_STAT, scc_stat_int, SA_INTERRUPT,
		            "SCC-B status", port);
	request_irq(BVME_IRQ_SCCB_RX, scc_rx_int, SA_INTERRUPT,
		            "SCC-B RX", port);
	request_irq(BVME_IRQ_SCCB_SPCOND, scc_spcond_int, SA_INTERRUPT,
		            "SCC-B special cond", port);

	{
		SCC_ACCESS_INIT(port);	/* Either channel will do */

		/* disable interrupts for this channel */
		SCCwrite(INT_AND_DMA_REG, 0);
	}

	/* Initialise the tty driver structures and register */
	scc_init_portstructs();
	scc_init_drivers();

	return 0;
}
#endif


int vme_scc_init(void)
{
	int res = -ENODEV;
	static int called = 0;

	if (called)
		return res;
	called = 1;
#ifdef CONFIG_MVME147_SCC
	if (MACH_IS_MVME147)
		res = mvme147_scc_init();
#endif
#ifdef CONFIG_MVME162_SCC
	if (MACH_IS_MVME16x)
		res = mvme162_scc_init();
#endif
#ifdef CONFIG_BVME6000_SCC
	if (MACH_IS_BVME6000)
		res = bvme6000_scc_init();
#endif
	return res;
}


/*---------------------------------------------------------------------------
 * Interrupt handlers
 *--------------------------------------------------------------------------*/

static void scc_rx_int(int irq, void *data, struct pt_regs *fp)
{
	unsigned char	ch;
	struct scc_port *port = data;
	struct tty_struct *tty = port->gs.tty;
	SCC_ACCESS_INIT(port);

	ch = SCCread_NB(RX_DATA_REG);
	if (!tty) {
		printk(KERN_WARNING "scc_rx_int with NULL tty!\n");
		SCCwrite_NB(COMMAND_REG, CR_HIGHEST_IUS_RESET);
		return;
	}
	if (tty->flip.count < TTY_FLIPBUF_SIZE) {
		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = 0;
		tty->flip.flag_buf_ptr++;
		tty->flip.char_buf_ptr++;
		tty->flip.count++;
	}

	/* Check if another character is already ready; in that case, the
	 * spcond_int() function must be used, because this character may have an
	 * error condition that isn't signalled by the interrupt vector used!
	 */
	if (SCCread(INT_PENDING_REG) &
	    (port->channel == CHANNEL_A ? IPR_A_RX : IPR_B_RX)) {
		scc_spcond_int (irq, data, fp);
		return;
	}

	SCCwrite_NB(COMMAND_REG, CR_HIGHEST_IUS_RESET);

	tty_flip_buffer_push(tty);
}


static void scc_spcond_int(int irq, void *data, struct pt_regs *fp)
{
	struct scc_port *port = data;
	struct tty_struct *tty = port->gs.tty;
	unsigned char	stat, ch, err;
	int		int_pending_mask = port->channel == CHANNEL_A ?
			                   IPR_A_RX : IPR_B_RX;
	SCC_ACCESS_INIT(port);
	
	if (!tty) {
		printk(KERN_WARNING "scc_spcond_int with NULL tty!\n");
		SCCwrite(COMMAND_REG, CR_ERROR_RESET);
		SCCwrite_NB(COMMAND_REG, CR_HIGHEST_IUS_RESET);
		return;
	}
	do {
		stat = SCCread(SPCOND_STATUS_REG);
		ch = SCCread_NB(RX_DATA_REG);

		if (stat & SCSR_RX_OVERRUN)
			err = TTY_OVERRUN;
		else if (stat & SCSR_PARITY_ERR)
			err = TTY_PARITY;
		else if (stat & SCSR_CRC_FRAME_ERR)
			err = TTY_FRAME;
		else
			err = 0;

		if (tty->flip.count < TTY_FLIPBUF_SIZE) {
			*tty->flip.char_buf_ptr = ch;
			*tty->flip.flag_buf_ptr = err;
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}

		/* ++TeSche: *All* errors have to be cleared manually,
		 * else the condition persists for the next chars
		 */
		if (err)
		  SCCwrite(COMMAND_REG, CR_ERROR_RESET);

	} while(SCCread(INT_PENDING_REG) & int_pending_mask);

	SCCwrite_NB(COMMAND_REG, CR_HIGHEST_IUS_RESET);

	tty_flip_buffer_push(tty);
}


static void scc_tx_int(int irq, void *data, struct pt_regs *fp)
{
	struct scc_port *port = data;
	SCC_ACCESS_INIT(port);

	if (!port->gs.tty) {
		printk(KERN_WARNING "scc_tx_int with NULL tty!\n");
		SCCmod (INT_AND_DMA_REG, ~IDR_TX_INT_ENAB, 0);
		SCCwrite(COMMAND_REG, CR_TX_PENDING_RESET);
		SCCwrite_NB(COMMAND_REG, CR_HIGHEST_IUS_RESET);
		return;
	}
	while ((SCCread_NB(STATUS_REG) & SR_TX_BUF_EMPTY)) {
		if (port->x_char) {
			SCCwrite(TX_DATA_REG, port->x_char);
			port->x_char = 0;
		}
		else if ((port->gs.xmit_cnt <= 0) || port->gs.tty->stopped ||
				port->gs.tty->hw_stopped)
			break;
		else {
			SCCwrite(TX_DATA_REG, port->gs.xmit_buf[port->gs.xmit_tail++]);
			port->gs.xmit_tail = port->gs.xmit_tail & (SERIAL_XMIT_SIZE-1);
			if (--port->gs.xmit_cnt <= 0)
				break;
		}
	}
	if ((port->gs.xmit_cnt <= 0) || port->gs.tty->stopped ||
			port->gs.tty->hw_stopped) {
		/* disable tx interrupts */
		SCCmod (INT_AND_DMA_REG, ~IDR_TX_INT_ENAB, 0);
		SCCwrite(COMMAND_REG, CR_TX_PENDING_RESET);   /* disable tx_int on next tx underrun? */
		port->gs.flags &= ~GS_TX_INTEN;
	}
	if (port->gs.tty && port->gs.xmit_cnt <= port->gs.wakeup_chars) {
		if ((port->gs.tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
				port->gs.tty->ldisc.write_wakeup)
			(port->gs.tty->ldisc.write_wakeup)(port->gs.tty);
		wake_up_interruptible(&port->gs.tty->write_wait);
	}

	SCCwrite_NB(COMMAND_REG, CR_HIGHEST_IUS_RESET);
}


static void scc_stat_int(int irq, void *data, struct pt_regs *fp)
{
	struct scc_port *port = data;
	unsigned channel = port->channel;
	unsigned char	last_sr, sr, changed;
	SCC_ACCESS_INIT(port);

	last_sr = scc_last_status_reg[channel];
	sr = scc_last_status_reg[channel] = SCCread_NB(STATUS_REG);
	changed = last_sr ^ sr;

	if (changed & SR_DCD) {
		port->c_dcd = !!(sr & SR_DCD);
		if (!(port->gs.flags & ASYNC_CHECK_CD))
			;	/* Don't report DCD changes */
		else if (port->c_dcd) {
			if (~(port->gs.flags & ASYNC_NORMAL_ACTIVE) ||
				~(port->gs.flags & ASYNC_CALLOUT_ACTIVE)) {
				/* Are we blocking in open?*/
				wake_up_interruptible(&port->gs.open_wait);
			}
		}
		else {
			if (!((port->gs.flags & ASYNC_CALLOUT_ACTIVE) &&
					(port->gs.flags & ASYNC_CALLOUT_NOHUP))) {
				if (port->gs.tty)
					tty_hangup (port->gs.tty);
			}
		}
	}
	SCCwrite(COMMAND_REG, CR_EXTSTAT_RESET);
	SCCwrite_NB(COMMAND_REG, CR_HIGHEST_IUS_RESET);
}


/*---------------------------------------------------------------------------
 * generic_serial.c callback funtions
 *--------------------------------------------------------------------------*/

static void scc_disable_tx_interrupts(void *ptr)
{
	struct scc_port *port = ptr;
	unsigned long	flags;
	SCC_ACCESS_INIT(port);

	save_flags(flags);
	cli();
	SCCmod(INT_AND_DMA_REG, ~IDR_TX_INT_ENAB, 0);
	port->gs.flags &= ~GS_TX_INTEN;
	restore_flags(flags);
}


static void scc_enable_tx_interrupts(void *ptr)
{
	struct scc_port *port = ptr;
	unsigned long	flags;
	SCC_ACCESS_INIT(port);

	save_flags(flags);
	cli();
	SCCmod(INT_AND_DMA_REG, 0xff, IDR_TX_INT_ENAB);
	/* restart the transmitter */
	scc_tx_int (0, port, 0);
	restore_flags(flags);
}


static void scc_disable_rx_interrupts(void *ptr)
{
	struct scc_port *port = ptr;
	unsigned long	flags;
	SCC_ACCESS_INIT(port);

	save_flags(flags);
	cli();
	SCCmod(INT_AND_DMA_REG,
	    ~(IDR_RX_INT_MASK|IDR_PARERR_AS_SPCOND|IDR_EXTSTAT_INT_ENAB), 0);
	restore_flags(flags);
}


static void scc_enable_rx_interrupts(void *ptr)
{
	struct scc_port *port = ptr;
	unsigned long	flags;
	SCC_ACCESS_INIT(port);

	save_flags(flags);
	cli();
	SCCmod(INT_AND_DMA_REG, 0xff,
		IDR_EXTSTAT_INT_ENAB|IDR_PARERR_AS_SPCOND|IDR_RX_INT_ALL);
	restore_flags(flags);
}


static int scc_get_CD(void *ptr)
{
	struct scc_port *port = ptr;
	unsigned channel = port->channel;

	return !!(scc_last_status_reg[channel] & SR_DCD);
}


static void scc_shutdown_port(void *ptr)
{
	struct scc_port *port = ptr;

	port->gs.flags &= ~ GS_ACTIVE;
	if (port->gs.tty && port->gs.tty->termios->c_cflag & HUPCL) {
		scc_setsignals (port, 0, 0);
	}
}


static int scc_set_real_termios (void *ptr)
{
	/* the SCC has char sizes 5,7,6,8 in that order! */
	static int chsize_map[4] = { 0, 2, 1, 3 };
	unsigned cflag, baud, chsize, channel, brgval = 0;
	unsigned long flags;
	struct scc_port *port = ptr;
	SCC_ACCESS_INIT(port);

	if (!port->gs.tty || !port->gs.tty->termios) return 0;

	channel = port->channel;

	if (channel == CHANNEL_A)
		return 0;		/* Settings controlled by boot PROM */

	cflag  = port->gs.tty->termios->c_cflag;
	baud = port->gs.baud;
	chsize = (cflag & CSIZE) >> 4;

	if (baud == 0) {
		/* speed == 0 -> drop DTR */
		save_flags(flags);
		cli();
		SCCmod(TX_CTRL_REG, ~TCR_DTR, 0);
		restore_flags(flags);
		return 0;
	}
	else if ((MACH_IS_MVME16x && (baud < 50 || baud > 38400)) ||
		 (MACH_IS_MVME147 && (baud < 50 || baud > 19200)) ||
		 (MACH_IS_BVME6000 &&(baud < 50 || baud > 76800))) {
		printk(KERN_NOTICE "SCC: Bad speed requested, %d\n", baud);
		return 0;
	}

	if (cflag & CLOCAL)
		port->gs.flags &= ~ASYNC_CHECK_CD;
	else
		port->gs.flags |= ASYNC_CHECK_CD;

#ifdef CONFIG_MVME147_SCC
	if (MACH_IS_MVME147)
		brgval = (M147_SCC_PCLK + baud/2) / (16 * 2 * baud) - 2;
#endif
#ifdef CONFIG_MVME162_SCC
	if (MACH_IS_MVME16x)
		brgval = (MVME_SCC_PCLK + baud/2) / (16 * 2 * baud) - 2;
#endif
#ifdef CONFIG_BVME6000_SCC
	if (MACH_IS_BVME6000)
		brgval = (BVME_SCC_RTxC + baud/2) / (16 * 2 * baud) - 2;
#endif
	/* Now we have all parameters and can go to set them: */
	save_flags(flags);
	cli();

	/* receiver's character size and auto-enables */
	SCCmod(RX_CTRL_REG, ~(RCR_CHSIZE_MASK|RCR_AUTO_ENAB_MODE),
			(chsize_map[chsize] << 6) |
			((cflag & CRTSCTS) ? RCR_AUTO_ENAB_MODE : 0));
	/* parity and stop bits (both, Tx and Rx), clock mode never changes */
	SCCmod (AUX1_CTRL_REG,
		~(A1CR_PARITY_MASK | A1CR_MODE_MASK),
		((cflag & PARENB
		  ? (cflag & PARODD ? A1CR_PARITY_ODD : A1CR_PARITY_EVEN)
		  : A1CR_PARITY_NONE)
		 | (cflag & CSTOPB ? A1CR_MODE_ASYNC_2 : A1CR_MODE_ASYNC_1)));
	/* sender's character size, set DTR for valid baud rate */
	SCCmod(TX_CTRL_REG, ~TCR_CHSIZE_MASK, chsize_map[chsize] << 5 | TCR_DTR);
	/* clock sources never change */
	/* disable BRG before changing the value */
	SCCmod(DPLL_CTRL_REG, ~DCR_BRG_ENAB, 0);
	/* BRG value */
	SCCwrite(TIMER_LOW_REG, brgval & 0xff);
	SCCwrite(TIMER_HIGH_REG, (brgval >> 8) & 0xff);
	/* BRG enable, and clock source never changes */
	SCCmod(DPLL_CTRL_REG, 0xff, DCR_BRG_ENAB);

	restore_flags(flags);

	return 0;
}


static int scc_chars_in_buffer (void *ptr)
{
	struct scc_port *port = ptr;
	SCC_ACCESS_INIT(port);

	return (SCCread (SPCOND_STATUS_REG) & SCSR_ALL_SENT) ? 0  : 1;
}


/* Comment taken from sx.c (2.4.0):
   I haven't the foggiest why the decrement use count has to happen
   here. The whole linux serial drivers stuff needs to be redesigned.
   My guess is that this is a hack to minimize the impact of a bug
   elsewhere. Thinking about it some more. (try it sometime) Try
   running minicom on a serial port that is driven by a modularized
   driver. Have the modem hangup. Then remove the driver module. Then
   exit minicom.  I expect an "oops".  -- REW */

static void scc_hungup(void *ptr)
{
	scc_disable_tx_interrupts(ptr);
	scc_disable_rx_interrupts(ptr);
	MOD_DEC_USE_COUNT;
}


static void scc_close(void *ptr)
{
	scc_disable_tx_interrupts(ptr);
	scc_disable_rx_interrupts(ptr);
	MOD_DEC_USE_COUNT;
}


/*---------------------------------------------------------------------------
 * Internal support functions
 *--------------------------------------------------------------------------*/

static void scc_setsignals(struct scc_port *port, int dtr, int rts)
{
	unsigned long flags;
	unsigned char t;
	SCC_ACCESS_INIT(port);

	save_flags(flags);
	cli();
	t = SCCread(TX_CTRL_REG);
	if (dtr >= 0) t = dtr? (t | TCR_DTR): (t & ~TCR_DTR);
	if (rts >= 0) t = rts? (t | TCR_RTS): (t & ~TCR_RTS);
	SCCwrite(TX_CTRL_REG, t);
	restore_flags(flags);
}


static void scc_send_xchar(struct tty_struct *tty, char ch)
{
	struct scc_port *port = (struct scc_port *)tty->driver_data;

	port->x_char = ch;
	if (ch)
		scc_enable_tx_interrupts(port);
}


/*---------------------------------------------------------------------------
 * Driver entrypoints referenced from above
 *--------------------------------------------------------------------------*/

static int scc_open (struct tty_struct * tty, struct file * filp)
{
	int line = MINOR(tty->device) - SCC_MINOR_BASE;
	int retval;
	struct scc_port *port = &scc_ports[line];
	int i, channel = port->channel;
	unsigned long	flags;
	SCC_ACCESS_INIT(port);
#if defined(CONFIG_MVME162_SCC) || defined(CONFIG_MVME147_SCC)
	static const struct {
		unsigned reg, val;
	} mvme_init_tab[] = {
		/* Values for MVME162 and MVME147 */
		/* no parity, 1 stop bit, async, 1:16 */
		{ AUX1_CTRL_REG, A1CR_PARITY_NONE|A1CR_MODE_ASYNC_1|A1CR_CLKMODE_x16 },
		/* parity error is special cond, ints disabled, no DMA */
		{ INT_AND_DMA_REG, IDR_PARERR_AS_SPCOND | IDR_RX_INT_DISAB },
		/* Rx 8 bits/char, no auto enable, Rx off */
		{ RX_CTRL_REG, RCR_CHSIZE_8 },
		/* DTR off, Tx 8 bits/char, RTS off, Tx off */
		{ TX_CTRL_REG, TCR_CHSIZE_8 },
		/* special features off */
		{ AUX2_CTRL_REG, 0 },
		{ CLK_CTRL_REG, CCR_RXCLK_BRG | CCR_TXCLK_BRG },
		{ DPLL_CTRL_REG, DCR_BRG_ENAB | DCR_BRG_USE_PCLK },
		/* Start Rx */
		{ RX_CTRL_REG, RCR_RX_ENAB | RCR_CHSIZE_8 },
		/* Start Tx */
		{ TX_CTRL_REG, TCR_TX_ENAB | TCR_RTS | TCR_DTR | TCR_CHSIZE_8 },
		/* Ext/Stat ints: DCD only */
		{ INT_CTRL_REG, ICR_ENAB_DCD_INT },
		/* Reset Ext/Stat ints */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* ...again */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
	};
#endif
#if defined(CONFIG_BVME6000_SCC)
	static const struct {
		unsigned reg, val;
	} bvme_init_tab[] = {
		/* Values for BVME6000 */
		/* no parity, 1 stop bit, async, 1:16 */
		{ AUX1_CTRL_REG, A1CR_PARITY_NONE|A1CR_MODE_ASYNC_1|A1CR_CLKMODE_x16 },
		/* parity error is special cond, ints disabled, no DMA */
		{ INT_AND_DMA_REG, IDR_PARERR_AS_SPCOND | IDR_RX_INT_DISAB },
		/* Rx 8 bits/char, no auto enable, Rx off */
		{ RX_CTRL_REG, RCR_CHSIZE_8 },
		/* DTR off, Tx 8 bits/char, RTS off, Tx off */
		{ TX_CTRL_REG, TCR_CHSIZE_8 },
		/* special features off */
		{ AUX2_CTRL_REG, 0 },
		{ CLK_CTRL_REG, CCR_RTxC_XTAL | CCR_RXCLK_BRG | CCR_TXCLK_BRG },
		{ DPLL_CTRL_REG, DCR_BRG_ENAB },
		/* Start Rx */
		{ RX_CTRL_REG, RCR_RX_ENAB | RCR_CHSIZE_8 },
		/* Start Tx */
		{ TX_CTRL_REG, TCR_TX_ENAB | TCR_RTS | TCR_DTR | TCR_CHSIZE_8 },
		/* Ext/Stat ints: DCD only */
		{ INT_CTRL_REG, ICR_ENAB_DCD_INT },
		/* Reset Ext/Stat ints */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* ...again */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
	};
#endif
	if (!(port->gs.flags & ASYNC_INITIALIZED)) {
		save_flags(flags);
		cli();
#if defined(CONFIG_MVME147_SCC) || defined(CONFIG_MVME162_SCC)
		if (MACH_IS_MVME147 || MACH_IS_MVME16x) {
			for (i=0; i<sizeof(mvme_init_tab)/sizeof(*mvme_init_tab); ++i)
				SCCwrite(mvme_init_tab[i].reg, mvme_init_tab[i].val);
		}
#endif
#if defined(CONFIG_BVME6000_SCC)
		if (MACH_IS_BVME6000) {
			for (i=0; i<sizeof(bvme_init_tab)/sizeof(*bvme_init_tab); ++i)
				SCCwrite(bvme_init_tab[i].reg, bvme_init_tab[i].val);
		}
#endif

		/* remember status register for detection of DCD and CTS changes */
		scc_last_status_reg[channel] = SCCread(STATUS_REG);

		port->c_dcd = 0;	/* Prevent initial 1->0 interrupt */
		scc_setsignals (port, 1,1);
		restore_flags(flags);
	}

	tty->driver_data = port;
	port->gs.tty = tty;
	port->gs.count++;
	retval = gs_init_port(&port->gs);
	if (retval) {
		port->gs.count--;
		return retval;
	}
	port->gs.flags |= GS_ACTIVE;
	if (port->gs.count == 1) {
		MOD_INC_USE_COUNT;
	}
	retval = gs_block_til_ready(port, filp);

	if (retval) {
		MOD_DEC_USE_COUNT;
		port->gs.count--;
		return retval;
	}

	if ((port->gs.count == 1) && (port->gs.flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
			*tty->termios = port->gs.normal_termios;
		else 
			*tty->termios = port->gs.callout_termios;
		scc_set_real_termios (port);
	}

	port->gs.session = current->session;
	port->gs.pgrp = current->pgrp;
	port->c_dcd = scc_get_CD (port);

	scc_enable_rx_interrupts(port);

	return 0;
}


static void scc_throttle (struct tty_struct * tty)
{
	struct scc_port *port = (struct scc_port *)tty->driver_data;
	unsigned long	flags;
	SCC_ACCESS_INIT(port);

	if (tty->termios->c_cflag & CRTSCTS) {
		save_flags(flags);
		cli();
		SCCmod(TX_CTRL_REG, ~TCR_RTS, 0);
		restore_flags(flags);
	}
	if (I_IXOFF(tty))
		scc_send_xchar(tty, STOP_CHAR(tty));
}


static void scc_unthrottle (struct tty_struct * tty)
{
	struct scc_port *port = (struct scc_port *)tty->driver_data;
	unsigned long	flags;
	SCC_ACCESS_INIT(port);

	if (tty->termios->c_cflag & CRTSCTS) {
		save_flags(flags);
		cli();
		SCCmod(TX_CTRL_REG, 0xff, TCR_RTS);
		restore_flags(flags);
	}
	if (I_IXOFF(tty))
		scc_send_xchar(tty, START_CHAR(tty));
}


static int scc_ioctl(struct tty_struct *tty, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}


static void scc_break_ctl(struct tty_struct *tty, int break_state)
{
	struct scc_port *port = (struct scc_port *)tty->driver_data;
	unsigned long	flags;
	SCC_ACCESS_INIT(port);

	save_flags(flags);
	cli();
	SCCmod(TX_CTRL_REG, ~TCR_SEND_BREAK, 
			break_state ? TCR_SEND_BREAK : 0);
	restore_flags(flags);
}


/*---------------------------------------------------------------------------
 * Serial console stuff...
 *--------------------------------------------------------------------------*/

#define scc_delay() do { __asm__ __volatile__ (" nop; nop"); } while (0)

static void scc_ch_write (char ch)
{
	volatile char *p = NULL;
	
#ifdef CONFIG_MVME147_SCC
	if (MACH_IS_MVME147)
		p = (volatile char *)M147_SCC_A_ADDR;
#endif
#ifdef CONFIG_MVME162_SCC
	if (MACH_IS_MVME16x)
		p = (volatile char *)MVME_SCC_A_ADDR;
#endif
#ifdef CONFIG_BVME6000_SCC
	if (MACH_IS_BVME6000)
		p = (volatile char *)BVME_SCC_A_ADDR;
#endif

	do {
		scc_delay();
	}
	while (!(*p & 4));
	scc_delay();
	*p = 8;
	scc_delay();
	*p = ch;
}

/* The console must be locked when we get here. */

static void scc_console_write (struct console *co, const char *str, unsigned count)
{
	unsigned long	flags;

	save_flags(flags);
	cli();

	while (count--)
	{
		if (*str == '\n')
			scc_ch_write ('\r');
		scc_ch_write (*str++);
	}
	restore_flags(flags);
}


static int scc_console_wait_key(struct console *co)
{
	unsigned long	flags;
	volatile char *p = NULL;
	int c;
	
#ifdef CONFIG_MVME147_SCC
	if (MACH_IS_MVME147)
		p = (volatile char *)M147_SCC_A_ADDR;
#endif
#ifdef CONFIG_MVME162_SCC
	if (MACH_IS_MVME16x)
		p = (volatile char *)MVME_SCC_A_ADDR;
#endif
#ifdef CONFIG_BVME6000_SCC
	if (MACH_IS_BVME6000)
		p = (volatile char *)BVME_SCC_A_ADDR;
#endif

	save_flags(flags);
	cli();

	/* wait for rx buf filled */
	while ((*p & 0x01) == 0)
		;

	*p = 8;
	scc_delay();
	c = *p;
	restore_flags(flags);
	return c;
}


static kdev_t scc_console_device(struct console *c)
{
	return MKDEV(TTY_MAJOR, SCC_MINOR_BASE + c->index);
}


static int __init scc_console_setup(struct console *co, char *options)
{
	return 0;
}


static struct console sercons = {
	name:		"ttyS",
	write:		scc_console_write,
	device:		scc_console_device,
	wait_key:	scc_console_wait_key,
	setup:		scc_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		-1,
};


void __init vme_scc_console_init(void)
{
	if (vme_brdtype == VME_TYPE_MVME147 ||
			vme_brdtype == VME_TYPE_MVME162 ||
			vme_brdtype == VME_TYPE_MVME172 ||
			vme_brdtype == VME_TYPE_BVME4000 ||
			vme_brdtype == VME_TYPE_BVME6000)
		register_console(&sercons);
}

