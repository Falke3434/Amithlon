/* $Id: serial.c,v 1.18 2001/09/24 09:27:22 pkj Exp $
 *
 * Serial port driver for the ETRAX 100LX chip
 *
 *      Copyright (C) 1998, 1999, 2000, 2001  Axis Communications AB
 *
 *      Many, many authors. Based once upon a time on serial.c for 16x50.
 *
 * $Log: serial.c,v $
 * Revision 1.18  2001/09/24 09:27:22  pkj
 * Completed ext_baud_table[] in cflag_to_baud() and cflag_to_etrax_baud().
 *
 * Revision 1.17  2001/08/24 11:32:49  ronny
 * More fixes for the CONFIG_ETRAX_SERIAL_PORT0 define.
 *
 * Revision 1.16  2001/08/24 07:56:22  ronny
 * Added config ifdefs around ser0 irq requests.
 *
 * Revision 1.15  2001/08/16 09:10:31  bjarne
 * serial.c - corrected the initialization of rs_table, the wrong defines
 *            where used.
 *            Corrected a test in timed_flush_handler.
 *            Changed configured to enabled.
 * serial.h - Changed configured to enabled.
 *
 * Revision 1.14  2001/08/15 07:31:23  bjarne
 * Introduced two new members to the e100_serial struct.
 * configured - Will be set to 1 if the port has been configured in .config
 * uses_dma   - Should be set to 1 if the port uses DMA. Currently it is set to 1
 *              when a port is opened. This is used to limit the DMA interrupt
 *              routines to only manipulate DMA channels actually used by the
 *              serial driver.
 *
 * Revision 1.13  2001/05/09 12:40:31  johana
 * Use DMA_NBR and IRQ_NBR defines from dma.h and irq.h
 *
 * Revision 1.12  2001/04/19 12:23:07  bjornw
 * CONFIG_RS485 -> CONFIG_ETRAX_RS485
 *
 * Revision 1.11  2001/04/05 14:29:48  markusl
 * Updated according to review remarks i.e.
 * -Use correct types in port structure to avoid compiler warnings
 * -Try to use IO_* macros whenever possible
 * -Open should never return -EBUSY
 *
 * Revision 1.10  2001/03/05 13:14:07  bjornw
 * Another spelling fix
 *
 * Revision 1.9  2001/02/23 13:46:38  bjornw
 * Spellling check
 *
 * Revision 1.8  2001/01/23 14:56:35  markusl
 * Made use of ser1 optional
 * Needed by USB
 *
 * Revision 1.7  2001/01/19 16:14:48  perf
 * Added kernel options for serial ports 234.
 * Changed option names from CONFIG_ETRAX100_XYZ to CONFIG_ETRAX_XYZ.
 *
 * Revision 1.6  2000/11/22 16:36:09  bjornw
 * Please marketing by using the correct case when spelling Etrax.
 *
 * Revision 1.5  2000/11/21 16:43:37  bjornw
 * Fixed so it compiles under CONFIG_SVINTO_SIM
 *
 * Revision 1.4  2000/11/15 17:34:12  bjornw
 * Added a timeout timer for flushing input channels. The interrupt-based
 * fast flush system should be easy to merge with this later (works the same
 * way, only with an irq instead of a system timer_list)
 *
 * Revision 1.3  2000/11/13 17:19:57  bjornw
 * * Incredibly, this almost complete rewrite of serial.c worked (at least
 *   for output) the first time.
 *
 *   Items worth noticing:
 *
 *      No Etrax100 port 1 workarounds (does only compile on 2.4 anyway now)
 *      RS485 is not ported (why cant it be done in userspace as on x86 ?)
 *      Statistics done through async_icount - if any more stats are needed,
 *      that's the place to put them or in an arch-dep version of it.
 *      timeout_interrupt and the other fast timeout stuff not ported yet
 *      There be dragons in this 3k+ line driver
 *
 * Revision 1.2  2000/11/10 16:50:28  bjornw
 * First shot at a 2.4 port, does not compile totally yet
 *
 * Revision 1.1  2000/11/10 16:47:32  bjornw
 * Added verbatim copy of rev 1.49 etrax100ser.c from elinux
 *
 * Revision 1.49  2000/10/30 15:47:14  tobiasa
 * Changed version number.
 *
 * Revision 1.48  2000/10/25 11:02:43  johana
 * Changed %ul to %lu in printf's
 *
 * Revision 1.47  2000/10/18 15:06:53  pkj
 * Compile correctly with CONFIG_ETRAX100_SERIAL_FLUSH_DMA_FAST and
 * CONFIG_SERIAL_PROC_ENTRY together.
 * Some clean-up of the /proc/serial file.
 *
 * Revision 1.46  2000/10/16 12:59:40  johana
 * Added CONFIG_SERIAL_PROC_ENTRY for statistics and debug info.
 *
 * Revision 1.45  2000/10/13 17:10:59  pkj
 * Do not flush DMAs while flipping TTY buffers.
 *
 * Revision 1.44  2000/10/13 16:34:29  pkj
 * Added a delay in ser_interrupt() for 2.3ms when an error is detected.
 * We do not know why this delay is required yet, but without it the
 * irmaflash program does not work (this was the program that needed
 * the ser_interrupt() to be needed in the first place). This should not
 * affect normal use of the serial ports.
 *
 * Revision 1.43  2000/10/13 16:30:44  pkj
 * New version of the fast flush of serial buffers code. This time
 * it is localized to the serial driver and uses a fast timer to
 * do the work.
 *
 * Revision 1.42  2000/10/13 14:54:26  bennyo
 * Fix for switching RTS when using rs485
 *
 * Revision 1.41  2000/10/12 11:43:44  pkj
 * Cleaned up a number of comments.
 *
 * Revision 1.40  2000/10/10 11:58:39  johana
 * Made RS485 support generic for all ports.
 * Toggle rts in interrupt if no delay wanted.
 * WARNING: No true transmitter empty check??
 * Set d_wait bit when sending data so interrupt is delayed until
 * fifo flushed. (Fix tcdrain() problem)
 *
 * Revision 1.39  2000/10/04 16:08:02  bjornw
 * * Use virt_to_phys etc. for DMA addresses
 * * Removed CONFIG_FLUSH_DMA_FAST hacks
 * * Indentation fix
 *
 * Revision 1.38  2000/10/02 12:27:10  mattias
 * * added variable used when using fast flush on serial dma.
 *   (CONFIG_FLUSH_DMA_FAST)
 *
 * Revision 1.37  2000/09/27 09:44:24  pkj
 * Uncomment definition of SERIAL_HANDLE_EARLY_ERRORS.
 *
 * Revision 1.36  2000/09/20 13:12:52  johana
 * Support for CONFIG_ETRAX100_SERIAL_RX_TIMEOUT_TICKS:
 *   Number of timer ticks between flush of receive fifo (1 tick = 10ms).
 *   Try 0-3 for low latency applications. Approx 5 for high load
 *   applications (e.g. PPP). Maybe this should be more adaptive some day...
 *
 * Revision 1.35  2000/09/20 10:36:08  johana
 * Typo in get_lsr_info()
 *
 * Revision 1.34  2000/09/20 10:29:59  johana
 * Let rs_chars_in_buffer() check fifo content as well.
 * get_lsr_info() might work now (not tested).
 * Easier to change the port to debug.
 *
 * Revision 1.33  2000/09/13 07:52:11  torbjore
 * Support RS485
 *
 * Revision 1.32  2000/08/31 14:45:37  bjornw
 * After sending a break we need to reset the transmit DMA channel
 *
 * Revision 1.31  2000/06/21 12:13:29  johana
 * Fixed wait for all chars sent when closing port.
 * (Used to always take 1 second!)
 * Added shadows for directions of status/ctrl signals.
 *
 * Revision 1.30  2000/05/29 16:27:55  bjornw
 * Simulator ifdef moved a bit
 *
 * Revision 1.29  2000/05/09 09:40:30  mattias
 * * Added description of dma registers used in timeout_interrupt
 * * Removed old code
 *
 * Revision 1.28  2000/05/08 16:38:58  mattias
 * * Bugfix for flushing fifo in timeout_interrupt
 *   Problem occurs when bluetooth stack waits for a small number of bytes
 *   containing an event acknowledging free buffers in bluetooth HW
 *   As before, data was stuck in fifo until more data came on uart and
 *   flushed it up to the stack.
 *
 * Revision 1.27  2000/05/02 09:52:28  jonasd
 * Added fix for peculiar etrax behaviour when eop is forced on an empty
 * fifo. This is used when flashing the IRMA chip. Disabled by default.
 *
 * Revision 1.26  2000/03/29 15:32:02  bjornw
 * 2.0.34 updates
 *
 * Revision 1.25  2000/02/16 16:59:36  bjornw
 * * Receive DMA directly into the flip-buffer, eliminating an intermediary
 *   receive buffer and a memcpy. Will avoid some overruns.
 * * Error message on debug port if an overrun or flip buffer overrun occurs.
 * * Just use the first byte in the flag flip buffer for errors.
 * * Check for timeout on the serial ports only each 5/100 s, not 1/100.
 *
 * Revision 1.24  2000/02/09 18:02:28  bjornw
 * * Clear serial errors (overrun, framing, parity) correctly. Before, the
 *   receiver would get stuck if an error occurred and we did not restart
 *   the input DMA.
 * * Cosmetics (indentation, some code made into inlines)
 * * Some more debug options
 * * Actually shut down the serial port (DMA irq, DMA reset, receiver stop)
 *   when the last open is closed. Corresponding fixes in startup().
 * * rs_close() "tx FIFO wait" code moved into right place, bug & -> && fixed
 *   and make a special case out of port 1 (R_DMA_CHx_STATUS is broken for that)
 * * e100_disable_rx/enable_rx just disables/enables the receiver, not RTS
 *
 * Revision 1.23  2000/01/24 17:46:19  johana
 * Wait for flush of DMA/FIFO when closing port.
 *
 * Revision 1.22  2000/01/20 18:10:23  johana
 * Added TIOCMGET ioctl to return modem status.
 * Implemented modem status/control that works with the extra signals
 * (DTR, DSR, RI,CD) as well.
 * 3 different modes supported:
 * ser0 on PB (Bundy), ser1 on PB (Lisa) and ser2 on PA (Bundy)
 * Fixed DEF_TX value that caused the serial transmitter pin (txd) to go to 0 when
 * closing the last filehandle, NASTY!.
 * Added break generation, not tested though!
 * Use SA_SHIRQ when request_irq() for ser2 and ser3 (shared with) par0 and par1.
 * You can't use them at the same time (yet..), but you can hopefully switch
 * between ser2/par0, ser3/par1 with the same kernel config.
 * Replaced some magic constants with defines
 *
 *
 */

static char *serial_version = "$Revision: 1.18 $";

#include <linux/config.h>
#include <linux/version.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#if (LINUX_VERSION_CODE >= 131343)
#include <linux/init.h>
#endif
#if (LINUX_VERSION_CODE >= 131336)
#include <asm/uaccess.h>
#endif
#include <linux/kernel.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <asm/delay.h>

#include <asm/svinto.h>

/* non-arch dependant serial structures are in linux/serial.h */
#include <linux/serial.h>
/* while we keep our own stuff (struct e100_serial) in a local .h file */
#include "serial.h"

/*
 * All of the compatibilty code so we can compile serial.c against
 * older kernels is hidden in serial_compat.h
 */
#if defined(LOCAL_HEADERS) || (LINUX_VERSION_CODE < 0x020317) /* 2.3.23 */
#include "serial_compat.h"
#endif

static DECLARE_TASK_QUEUE(tq_serial);

struct tty_driver serial_driver, callout_driver;
static int serial_refcount;

/* serial subtype definitions */
#ifndef SERIAL_TYPE_NORMAL
#define SERIAL_TYPE_NORMAL	1
#define SERIAL_TYPE_CALLOUT	2
#endif

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

//#define SERIAL_DEBUG_INTR
//#define SERIAL_DEBUG_OPEN 
//#define SERIAL_DEBUG_FLOW
//#define SERIAL_DEBUG_DATA
//#define SERIAL_DEBUG_THROTTLE
//#define SERIAL_DEBUG_IO  /* Debug for Extra control and status pins */
#define SERIAL_DEBUG_LINE 0 /* What serport we want to debug */

/* Enable this to use serial interrupts to handle when you
   expect the first received event on the serial port to
   be an error, break or similar. Used to be able to flash IRMA
   from eLinux */
//#define SERIAL_HANDLE_EARLY_ERRORS


#ifndef CONFIG_ETRAX_SERIAL_RX_TIMEOUT_TICKS
/* Default number of timer ticks before flushing rx fifo 
 * When using "little data, low latency applications: use 0
 * When using "much data applications (PPP)" use ~5
 */
#define CONFIG_ETRAX_SERIAL_RX_TIMEOUT_TICKS 5 
#endif

#define MAX_FLUSH_TIME 8

#define _INLINE_ inline

static void change_speed(struct e100_serial *info);
static void rs_wait_until_sent(struct tty_struct *tty, int timeout);
static int rs_write(struct tty_struct * tty, int from_user,
                    const unsigned char *buf, int count);

#define DEF_BAUD 0x99   /* 115.2 kbit/s */
#define STD_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST )
#define DEF_RX 0x20  /* or SERIAL_CTRL_W >> 8 */
/* Default value of tx_ctrl register: has txd(bit 7)=1 (idle) as default */
#define DEF_TX 0x80  /* or SERIAL_CTRL_B */

/* offsets from R_SERIALx_CTRL */

#define REG_DATA 0
#define REG_TR_DATA 0
#define REG_STATUS 1
#define REG_TR_CTRL 1
#define REG_REC_CTRL 2
#define REG_BAUD 3
#define REG_XOFF 4  /* this is a 32 bit register */

/*
 * General note regarding the use of IO_* macros in this file: 
 *
 * We will use the bits defined for DMA channel 6 when using various
 * IO_* macros (e.g. IO_STATE, IO_MASK, IO_EXTRACT) and _assume_ they are
 * the same for all channels (which of course they are).
 *
 * We will also use the bits defined for serial port 0 when writing commands
 * to the different ports, as these bits too are the same for all ports.
 */


/* this is the data for the four serial ports in the etrax100 */
/*  DMA2(ser2), DMA4(ser3), DMA6(ser0) or DMA8(ser1) */
/* R_DMA_CHx_CLR_INTR, R_DMA_CHx_FIRST, R_DMA_CHx_CMD */

static struct e100_serial rs_table[] = {
	{ DEF_BAUD, (unsigned char *)R_SERIAL0_CTRL, 1U << 12, /* uses DMA 6 and 7 */
	  R_DMA_CH6_CLR_INTR, R_DMA_CH6_FIRST, R_DMA_CH6_CMD,
	  R_DMA_CH6_STATUS, R_DMA_CH6_HWSW,
	  R_DMA_CH7_CLR_INTR, R_DMA_CH7_FIRST, R_DMA_CH7_CMD,
	  R_DMA_CH7_STATUS, R_DMA_CH7_HWSW,
	  STD_FLAGS, DEF_RX, DEF_TX, 2,
#ifdef CONFIG_ETRAX_SERIAL_PORT0
          1
#else
          0
#endif
},  /* ttyS0 */
#ifndef CONFIG_SVINTO_SIM
	{ DEF_BAUD, (unsigned char *)R_SERIAL1_CTRL, 1U << 16, /* uses DMA 8 and 9 */
	  R_DMA_CH8_CLR_INTR, R_DMA_CH8_FIRST, R_DMA_CH8_CMD,
	  R_DMA_CH8_STATUS, R_DMA_CH8_HWSW,
	  R_DMA_CH9_CLR_INTR, R_DMA_CH9_FIRST, R_DMA_CH9_CMD,
	  R_DMA_CH9_STATUS, R_DMA_CH9_HWSW,
	  STD_FLAGS, DEF_RX, DEF_TX, 3 ,
#ifdef CONFIG_ETRAX_SERIAL_PORT1
          1
#else
          0
#endif
},  /* ttyS1 */

	{ DEF_BAUD, (unsigned char *)R_SERIAL2_CTRL, 1U << 4,  /* uses DMA 2 and 3 */
	  R_DMA_CH2_CLR_INTR, R_DMA_CH2_FIRST, R_DMA_CH2_CMD,
	  R_DMA_CH2_STATUS, R_DMA_CH2_HWSW,
	  R_DMA_CH3_CLR_INTR, R_DMA_CH3_FIRST, R_DMA_CH3_CMD,
	  R_DMA_CH3_STATUS, R_DMA_CH3_HWSW,
	  STD_FLAGS, DEF_RX, DEF_TX, 0,
#ifdef CONFIG_ETRAX_SERIAL_PORT2
          1
#else
          0
#endif
 },  /* ttyS2 */

	{ DEF_BAUD, (unsigned char *)R_SERIAL3_CTRL, 1U << 8,  /* uses DMA 4 and 5 */
	  R_DMA_CH4_CLR_INTR, R_DMA_CH4_FIRST, R_DMA_CH4_CMD,
	  R_DMA_CH4_STATUS, R_DMA_CH4_HWSW,
	  R_DMA_CH5_CLR_INTR, R_DMA_CH5_FIRST, R_DMA_CH5_CMD,
	  R_DMA_CH5_STATUS, R_DMA_CH5_HWSW,
	  STD_FLAGS, DEF_RX, DEF_TX, 1,
#ifdef CONFIG_ETRAX_SERIAL_PORT3
          1
#else
          0
#endif
 }   /* ttyS3 */
#endif
};


#define NR_PORTS (sizeof(rs_table)/sizeof(struct e100_serial))
  
static struct tty_struct *serial_table[NR_PORTS]; 
static struct termios *serial_termios[NR_PORTS];
static struct termios *serial_termios_locked[NR_PORTS];


/* RS-485 */
#if defined(CONFIG_ETRAX_RS485)
#if defined(CONFIG_ETRAX_RS485_ON_PA)
static int rs485_pa_bit = CONFIG_ETRAX_RS485_ON_PA_BIT;
#endif
#endif
  

/* For now we assume that all bits are on the same port for each serial port */

/* Dummy shadow variables */
static unsigned char dummy_ser0 = 0x00;
static unsigned char dummy_ser1 = 0x00;
static unsigned char dummy_ser2 = 0x00;
static unsigned char dummy_ser3 = 0x00;

static unsigned char dummy_dir_ser0 = 0x00;
static unsigned char dummy_dir_ser1 = 0x00;
static unsigned char dummy_dir_ser2 = 0x00;
static unsigned char dummy_dir_ser3 = 0x00;

/* Info needed for each ports extra control/status signals.
   We only supports that all pins uses same register for each port */
struct control_pins
{
	volatile unsigned char *port;
	volatile unsigned char *shadow;
	volatile unsigned char *dir_shadow;
	
	unsigned char dtr_bit;
	unsigned char ri_bit;
	unsigned char dsr_bit;
	unsigned char cd_bit;
};

static const struct control_pins e100_modem_pins[NR_PORTS] = 
{
/* Ser 0 */
  {
#if defined(CONFIG_ETRAX_SER0_DTR_RI_DSR_CD_ON_PB)
    R_PORT_PB_DATA,  &port_pb_data_shadow,  &port_pb_dir_shadow,
    CONFIG_ETRAX_SER0_DTR_ON_PB_BIT,
    CONFIG_ETRAX_SER0_RI_ON_PB_BIT, 
    CONFIG_ETRAX_SER0_DSR_ON_PB_BIT, 
    CONFIG_ETRAX_SER0_CD_ON_PB_BIT
#else
    &dummy_ser0, &dummy_ser0, &dummy_dir_ser0, 0, 1, 2, 3
#endif   
  },
/* Ser 1 */
  {
#if defined(CONFIG_ETRAX_SER1_DTR_RI_DSR_CD_ON_PB)
    R_PORT_PB_DATA,  &port_pb_data_shadow,  &port_pb_dir_shadow,
    CONFIG_ETRAX_SER1_DTR_ON_PB_BIT,
    CONFIG_ETRAX_SER1_RI_ON_PB_BIT, 
    CONFIG_ETRAX_SER1_DSR_ON_PB_BIT, 
    CONFIG_ETRAX_SER1_CD_ON_PB_BIT
#else
    &dummy_ser1, &dummy_ser1, &dummy_dir_ser1, 0, 1, 2, 3
#endif   
  },  
/* Ser 2 */
  {
#if defined(CONFIG_ETRAX_SER2_DTR_RI_DSR_CD_ON_PA)
    R_PORT_PA_DATA,  &port_pa_data_shadow,  &port_pa_dir_shadow,
    CONFIG_ETRAX_SER2_DTR_ON_PA_BIT,
    CONFIG_ETRAX_SER2_RI_ON_PA_BIT, 
    CONFIG_ETRAX_SER2_DSR_ON_PA_BIT, 
    CONFIG_ETRAX_SER2_CD_ON_PA_BIT
#else
    &dummy_ser2, &dummy_ser2, &dummy_dir_ser2, 0, 1, 2, 3
#endif   
  },
/* Ser 3 */
  {
    &dummy_ser3, &dummy_ser3, &dummy_dir_ser3, 0, 1, 2, 3
  }
};

#if defined(CONFIG_ETRAX_RS485) && defined(CONFIG_ETRAX_RS485_ON_PA)
unsigned char rs485_pa_port = CONFIG_ETRAX_RS485_ON_PA_BIT;
#endif

#define E100_RTS_MASK 0x20
#define E100_CTS_MASK 0x40


/* All serial port signals are active low:
 * active   = 0 -> 3.3V to RS-232 driver -> -12V on RS-232 level
 * inactive = 1 -> 0V   to RS-232 driver -> +12V on RS-232 level
 *
 * These macros returns the pin value: 0=0V, >=1 = 3.3V on ETRAX chip
 */

/* Output */
#define E100_RTS_GET(info) ((info)->rx_ctrl & E100_RTS_MASK)
/* Input */
#define E100_CTS_GET(info) ((info)->port[REG_STATUS] & E100_CTS_MASK)

/* These are typically PA or PB and 0 means 0V, 1 means 3.3V */
/* Is an output */
#define E100_DTR_GET(info) ((*e100_modem_pins[(info)->line].shadow) & (1 << e100_modem_pins[(info)->line].dtr_bit))

/* Normally inputs */
#define E100_RI_GET(info) ((*e100_modem_pins[(info)->line].port) & (1 << e100_modem_pins[(info)->line].ri_bit))
#define E100_CD_GET(info) ((*e100_modem_pins[(info)->line].port) & (1 << e100_modem_pins[(info)->line].cd_bit))

/* Input */
#define E100_DSR_GET(info) ((*e100_modem_pins[(info)->line].port) & (1 << e100_modem_pins[(info)->line].dsr_bit))


#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

/*
 * tmp_buf is used as a temporary buffer by serial_write.  We need to
 * lock it in case the memcpy_fromfs blocks while swapping in a page,
 * and some other program tries to do a serial write at the same time.
 * Since the lock will only come under contention when the system is
 * swapping and available memory is low, it makes sense to share one
 * buffer across all the serial ports, since it significantly saves
 * memory if large numbers of serial ports are open.
 */
static unsigned char *tmp_buf;
#ifdef DECLARE_MUTEX
static DECLARE_MUTEX(tmp_buf_sem);
#else
static struct semaphore tmp_buf_sem = MUTEX;
#endif

#ifdef CONFIG_ETRAX_SERIAL_FLUSH_DMA_FAST

/* clock select 10 for timer 1 gives 230400 Hz */
#define FASTTIMER_SELECT (10)
/* we use a source of 230400 Hz and a divider of 15 => 15360 Hz */
#define FASTTIMER_DIV (15)

/* fast flush timer stuff */
static int fast_timer_started = 0;
static unsigned long int fast_timer_ints = 0;

static void _INLINE_ start_flush_timer(void)
{
	if (fast_timer_started)
		return;

	*R_TIMER_CTRL = r_timer_ctrl_shadow = 
		(r_timer_ctrl_shadow &
		 ~IO_MASK(R_TIMER_CTRL, timerdiv1) &
		 ~IO_MASK(R_TIMER_CTRL, tm1) &
		 ~IO_MASK(R_TIMER_CTRL, clksel1)) | 
		IO_FIELD(R_TIMER_CTRL, timerdiv1, FASTTIMER_DIV) |
		IO_STATE(R_TIMER_CTRL, tm1, stop_ld) | 
		IO_FIELD(R_TIMER_CTRL, clksel1, FASTTIMER_SELECT);

	*R_TIMER_CTRL = r_timer_ctrl_shadow = 
		(r_timer_ctrl_shadow & ~IO_MASK(R_TIMER_CTRL, tm1)) |
		IO_STATE(R_TIMER_CTRL, tm1, run);

	/* enable timer1 irq */

	*R_IRQ_MASK0_SET = IO_STATE(R_IRQ_MASK0_SET, timer1, set);
	fast_timer_started = 1;
}
#endif /* CONFIG_ETRAX_SERIAL_FLUSH_DMA_FAST */

/*
 * This function maps from the Bxxxx defines in asm/termbits.h into real
 * baud rates.
 */

static int 
cflag_to_baud(unsigned int cflag)
{
	static int baud_table[] = {
		0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400,
		4800, 9600, 19200, 38400 };

	static int ext_baud_table[] = {
		0, 57600, 115200, 230400, 460800, 921600, 1843200, 6250000,
                0, 0, 0, 0, 0, 0, 0, 0 };

	if (cflag & CBAUDEX)
		return ext_baud_table[(cflag & CBAUD) & ~CBAUDEX];
	else 
		return baud_table[cflag & CBAUD];
}

/* and this maps to an etrax100 hardware baud constant */

static unsigned char 
cflag_to_etrax_baud(unsigned int cflag)
{
	char retval;

	static char baud_table[] = {
		-1, -1, -1, -1, -1, -1, -1, 0, 1, 2, -1, 3, 4, 5, 6, 7 };

	static char ext_baud_table[] = {
		-1, 8, 9, 10, 11, 12, 13, 14, -1, -1, -1, -1, -1, -1, -1, -1 };

	if (cflag & CBAUDEX)
		retval = ext_baud_table[(cflag & CBAUD) & ~CBAUDEX];
	else 
		retval = baud_table[cflag & CBAUD];

	if (retval < 0) {
		printk("serdriver tried setting invalid baud rate, flags %x.\n", cflag);
		retval = 5; /* choose default 9600 instead */
	}

	return retval | (retval << 4); /* choose same for both TX and RX */
}


/* Various static support functions */

/* Functions to set or clear DTR/RTS on the requested line */
/* It is complicated by the fact that RTS is a serial port register, while
 * DTR might not be implemented in the HW at all, and if it is, it can be on
 * any general port.
 */

static inline void 
e100_dtr(struct e100_serial *info, int set)
{
#ifndef CONFIG_SVINTO_SIM
	unsigned char mask  = ( 1 << e100_modem_pins[info->line].dtr_bit);
#ifdef SERIAL_DEBUG_IO  
	printk("ser%i dtr %i mask: 0x%02X\n", info->line, set, mask);
	printk("ser%i shadow before 0x%02X get: %i\n", 
	       info->line, *e100_modem_pins[info->line].shadow,
	       E100_DTR_GET(info));
#endif
	/* DTR is active low */
	{
		unsigned long flags;	
		save_flags(flags);
		cli();
		*e100_modem_pins[info->line].shadow &= ~mask;
		*e100_modem_pins[info->line].shadow |= (set ? 0 : mask); 
		*e100_modem_pins[info->line].port = *e100_modem_pins[info->line].shadow;
		restore_flags(flags);
	}
	
#if 0
	REG_SHADOW_SET(e100_modem_pins[info->line].port,
		       *e100_modem_pins[info->line].shadow,
		       e100_modem_pins[info->line].dtr_bit, !set);
#endif
#ifdef SERIAL_DEBUG_IO
	printk("ser%i shadow after 0x%02X get: %i\n", 
	       info->line, *e100_modem_pins[info->line].shadow, 
	       E100_DTR_GET(info));
#endif
#endif
}

/* set = 0 means 3.3V on the pin, bitvalue: 0=active, 1=inactive  
 *                                          0=0V    , 1=3.3V
 */
static inline void 
e100_rts(struct e100_serial *info, int set)
{
#ifndef CONFIG_SVINTO_SIM
#ifdef SERIAL_DEBUG_IO  
	printk("ser%i rts %i\n", info->line, set);
#endif
	info->rx_ctrl &= ~E100_RTS_MASK;
	info->rx_ctrl |= (set ? 0 : E100_RTS_MASK);  /* RTS is active low */
	info->port[REG_REC_CTRL] = info->rx_ctrl;
#endif
}

/* If this behaves as a modem, RI and CD is an output */
static inline void 
e100_ri_out(struct e100_serial *info, int set)
{
#ifndef CONFIG_SVINTO_SIM
	/* RI is active low */
	{
		unsigned char mask  = ( 1 << e100_modem_pins[info->line].ri_bit);
		unsigned long flags;	
		save_flags(flags);
		cli();
		*e100_modem_pins[info->line].shadow &= ~mask;
		*e100_modem_pins[info->line].shadow |= (set ? 0 : mask); 
		*e100_modem_pins[info->line].port = *e100_modem_pins[info->line].shadow;
		restore_flags(flags);
	}
#if 0
	REG_SHADOW_SET(e100_modem_pins[info->line].port,
		       *e100_modem_pins[info->line].shadow,
		       e100_modem_pins[info->line].ri_bit, !set);
#endif
#endif
}
static inline void 
e100_cd_out(struct e100_serial *info, int set)
{
#ifndef CONFIG_SVINTO_SIM
	/* CD is active low */
	{
		unsigned char mask  = ( 1 << e100_modem_pins[info->line].cd_bit);
		unsigned long flags;	
		save_flags(flags);
		cli();
		*e100_modem_pins[info->line].shadow &= ~mask;
		*e100_modem_pins[info->line].shadow |= (set ? 0 : mask); 
		*e100_modem_pins[info->line].port = *e100_modem_pins[info->line].shadow;
		restore_flags(flags);
	}
#if 0
	REG_SHADOW_SET(e100_modem_pins[info->line].port,
		       *e100_modem_pins[info->line].shadow,
		       e100_modem_pins[info->line].cd_bit, !set);
#endif
#endif
}

static inline void
e100_disable_rx(struct e100_serial *info)
{
#ifndef CONFIG_SVINTO_SIM
	/* disable the receiver */
	info->port[REG_REC_CTRL] = info->rx_ctrl &=
		~IO_MASK(R_SERIAL0_REC_CTRL, rec_enable);
#endif
}

static inline void 
e100_enable_rx(struct e100_serial *info)
{
#ifndef CONFIG_SVINTO_SIM
	/* enable the receiver */
	info->port[REG_REC_CTRL] = info->rx_ctrl |=
		IO_MASK(R_SERIAL0_REC_CTRL, rec_enable);
#endif
}

/* the rx DMA uses both the dma_descr and the dma_eop interrupts */

static inline void
e100_disable_rxdma_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("rxdma_irq(%d): 0\n",info->line);
#endif
	*R_IRQ_MASK2_CLR = (info->irq << 2) | (info->irq << 3);
}

static inline void
e100_enable_rxdma_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("rxdma_irq(%d): 1\n",info->line);
#endif
	*R_IRQ_MASK2_SET = (info->irq << 2) | (info->irq << 3);
}

/* the tx DMA uses only dma_descr interrupt */

static inline void
e100_disable_txdma_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("txdma_irq(%d): 0\n",info->line);
#endif
	*R_IRQ_MASK2_CLR = info->irq;
}

static inline void
e100_enable_txdma_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("txdma_irq(%d): 1\n",info->line);
#endif
	*R_IRQ_MASK2_SET = info->irq;
}

#ifdef SERIAL_HANDLE_EARLY_ERRORS
/* in order to detect and fix errors on the first byte
 we have to use the serial interrupts as well. */

static inline void
e100_disable_serial_data_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("ser_irq(%d): 0\n",info->line);
#endif
	*R_IRQ_MASK1_CLR = (1U << (8+2*info->line));
}

static inline void
e100_enable_serial_data_irq(struct e100_serial *info) 
{
#ifdef SERIAL_DEBUG_INTR
	printk("ser_irq(%d): 1\n",info->line);
	printk("**** %d = %d\n",
	       (8+2*info->line),
	       (1U << (8+2*info->line)));
#endif
	*R_IRQ_MASK1_SET = (1U << (8+2*info->line));
}
#endif

#if defined(CONFIG_ETRAX_RS485)
/* Enable RS-485 mode on selected port. This is UGLY. */
static int
e100_enable_rs485(struct tty_struct *tty,struct rs485_control *r)
{
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;

#if defined(CONFIG_ETRAX_RS485_ON_PA)	
	*R_PORT_PA_DATA = port_pa_data_shadow |= (1 << rs485_pa_bit);
#endif

	info->rs485.rts_on_send = 0x01 & r->rts_on_send;
	info->rs485.rts_after_sent = 0x01 & r->rts_after_sent;
	info->rs485.delay_rts_before_send = r->delay_rts_before_send;
	info->rs485.enabled = r->enabled;
	
	return 0;
}

static int
e100_write_rs485(struct tty_struct *tty,struct rs485_write *r)
{
	int stop_delay;
	int total, i;
	int max_j, delay_ms, bits;
	tcflag_t cflags;
	int size = (*r).outc_size;
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;
	struct wait_queue wait = { current, NULL };

	/* If we are in RS-485 mode, we need to toggle RTS and disable
	 * the receiver before initiating a DMA transfer
	 */
	e100_rts(info, info->rs485.rts_on_send);
#if defined(CONFIG_ETRAX_RS485_DISABLE_RECEIVER)
	e100_disable_rx(info);
	e100_disable_rxdma_irq(info);
#endif

	if (info->rs485.delay_rts_before_send > 0){
		current->timeout = jiffies + (info->rs485.delay_rts_before_send * HZ)/1000;
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		current->timeout = 0;
	}
	total = rs_write(tty, 1, (*r).outc, (*r).outc_size);

	/* If we are in RS-485 mode the following things has to be done:
	 * wait until DMA is ready
	 * wait on transmit shift register
	 * wait to toggle RTS
	 * enable the receiver
	 */	

	/* wait on transmit shift register */
	/* All is sent, check if we should wait more before toggling rts */
	
	/* calc. number of bits / data byte */
	cflags = info->tty->termios->c_cflag;
	/* databits + startbit and 1 stopbit */
	if((cflags & CSIZE) == CS7) 
	  bits = 9;
	else
	  bits = 10;  

	if(cflags & CSTOPB)     /* 2 stopbits ? */
	  bits++;

	if(cflags & PARENB)     /* parity bit ? */
	  bits++;
	
	/* calc timeout */
	delay_ms = ((bits * size * 1000) / info->baud) + 1;
	max_j = jiffies + (delay_ms * HZ)/1000 + 10;

	while (jiffies < max_j ) {
	  if (info->port[REG_STATUS] &
	      IO_STATE(R_SERIAL0_STATUS, tr_ready, ready)) {
	    for( i=0 ; i<100; i++ ) {};
	    if (info->port[REG_STATUS] &
		IO_STATE(R_SERIAL0_STATUS, tr_ready, ready)) {
	      /* ~25 for loops per usec */
	      stop_delay = 1000000 / info->baud;
	      if(cflags & CSTOPB) 
		stop_delay *= 2;
	      udelay(stop_delay);
	      break;
	    }
	  }
	}

	e100_rts(info, info->rs485.rts_after_sent);
	
#if defined(CONFIG_ETRAX_RS485_DISABLE_RECEIVER)
	e100_enable_rx(info);
	e100_enable_rxdma_irq(info);
#endif

	return total;
}
#endif

/*
 * ------------------------------------------------------------
 * rs_stop() and rs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */

/* FIXME - when are these used and what is the purpose ? 
 * In rs_stop we probably just can block the transmit DMA ready irq
 * and in rs_start we re-enable it (and then the old one will come).
 */

static void 
rs_stop(struct tty_struct *tty)
{
}

static void 
rs_start(struct tty_struct *tty)
{
}

/*
 * ----------------------------------------------------------------------
 *
 * Here starts the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * rs_interrupt().  They were separated out for readability's sake.
 *
 * Note: rs_interrupt() is a "fast" interrupt, which means that it
 * runs with interrupts turned off.  People who may want to modify
 * rs_interrupt() should try to keep the interrupt handler as fast as
 * possible.  After you are done making modifications, it is not a bad
 * idea to do:
 * 
 * gcc -S -DKERNEL -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer serial.c
 *
 * and look at the resulting assemble code in serial.s.
 *
 * 				- Ted Ts'o (tytso@mit.edu), 7-Mar-93
 * -----------------------------------------------------------------------
 */

/*
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver.
 */
static _INLINE_ void 
rs_sched_event(struct e100_serial *info,
				    int event)
{
	info->event |= 1 << event;
	queue_task(&info->tqueue, &tq_serial);
	mark_bh(SERIAL_BH);
}

/* The output DMA channel is free - use it to send as many chars as possible
 * NOTES:
 *   We don't pay attention to info->x_char, which means if the TTY wants to
 *   use XON/XOFF it will set info->x_char but we won't send any X char!
 * 
 *   To implement this, we'd just start a DMA send of 1 byte pointing at a
 *   buffer containing the X char, and skip updating xmit. We'd also have to
 *   check if the last sent char was the X char when we enter this function
 *   the next time, to avoid updating xmit with the sent X value.
 */

static void 
transmit_chars(struct e100_serial *info)
{
	unsigned int c, sentl;
	struct etrax_dma_descr *descr;

#ifdef CONFIG_SVINTO_SIM
	/* This will output too little if tail is not 0 always since
	 * we don't reloop to send the other part. Anyway this SHOULD be a
	 * no-op - transmit_chars would never really be called during sim
	 * since rs_write does not write into the xmit buffer then.
	 */
	if(info->xmit.tail)
		printk("Error in serial.c:transmit_chars(), tail!=0\n");
	if(info->xmit.head != info->xmit.tail) {
		SIMCOUT(info->xmit.buf + info->xmit.tail,
			CIRC_CNT(info->xmit.head,
				 info->xmit.tail,
				 SERIAL_XMIT_SIZE));
		info->xmit.head = info->xmit.tail;  /* move back head */
		info->tr_running = 0;
	}
	return;
#endif
	/* acknowledge both a dma_descr and dma_eop irq in R_DMAx_CLRINTR */
	*info->oclrintradr =
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_descr, do) |
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_eop, do);

#ifdef SERIAL_DEBUG_INTR
	if(info->line == SERIAL_DEBUG_LINE)
		printk("tc\n");
#endif
	if(!info->tr_running) {
		/* weirdo... we shouldn't get here! */
		printk("Achtung: transmit_chars with !tr_running\n");
		return;
	}

	descr = &info->tr_descr;

	/* first get the amount of bytes sent during the last DMA transfer,
	   and update xmit accordingly */

	/* if the stop bit was not set, all data has been sent */
	if(!(descr->status & d_stop)) {
		sentl = descr->sw_len;
	} else 
		/* otherwise we find the amount of data sent here */
		sentl = descr->hw_len;

	/* update stats */
	info->icount.tx += sentl;

	/* update xmit buffer */
	info->xmit.tail = (info->xmit.tail + sentl) & (SERIAL_XMIT_SIZE - 1);

	/* if there is only a few chars left in the buf, wake up the blocked
	   write if any */
	if (CIRC_CNT(info->xmit.head,
		     info->xmit.tail,
		     SERIAL_XMIT_SIZE) < WAKEUP_CHARS)
		rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);

	/* find out the largest amount of consecutive bytes we want to send now */

	c = CIRC_CNT_TO_END(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);

	if(c <= 0) {
		/* our job here is done, don't schedule any new DMA transfer */
		info->tr_running = 0;

#if defined(CONFIG_ETRAX_RS485)
		/* Check if we should toggle RTS now */
		if (info->rs485.enabled)
		{
			/* Make sure fifo is empty */
			int in_fifo = 0 ;
			do{
				in_fifo = IO_EXTRACT(R_DMA_CH6_STATUS, avail,
						    *info->ostatusadr);
			}  while (in_fifo > 0) ;
			/* Any way to really check transmitter empty? (TEMT) */
			/* Control RTS to set to RX mode */
			e100_rts(info, info->rs485.rts_after_sent); 
#if defined(CONFIG_ETRAX_RS485_DISABLE_RECEIVER)
			e100_enable_rx(info);
			e100_enable_rxdma_irq(info);
#endif
		}
#endif /* RS485 */

		return;
	}

	/* ok we can schedule a dma send of c chars starting at info->xmit.tail */
	/* set up the descriptor correctly for output */

	descr->ctrl = d_int | d_eol | d_wait; /* Wait needed for tty_wait_until_sent() */
	descr->sw_len = c;
	descr->buf = virt_to_phys(info->xmit.buf + info->xmit.tail);
	descr->status = 0;

	*info->ofirstadr = virt_to_phys(descr); /* write to R_DMAx_FIRST */
	*info->ocmdadr = 1;       /* dma command start -> R_DMAx_CMD */
	
	/* DMA is now running (hopefully) */

}

static void 
start_transmit(struct e100_serial *info)
{
#if 0
	if(info->line == SERIAL_DEBUG_LINE)
		printk("x\n");
#endif

	info->tr_descr.sw_len = 0;
	info->tr_descr.hw_len = 0;
	info->tr_descr.status = 0;
	info->tr_running = 1;

	transmit_chars(info);
}


static _INLINE_ void 
receive_chars(struct e100_serial *info)
{
	struct tty_struct *tty;
	unsigned char rstat;
	unsigned int recvl;
	struct etrax_dma_descr *descr;

#ifdef CONFIG_SVINTO_SIM
	/* No receive in the simulator.  Will probably be when the rest of
	 * the serial interface works, and this piece will just be removed.
	 */
	return;
#endif

	tty = info->tty;

	/* acknowledge both a dma_descr and dma_eop irq in R_DMAx_CLRINTR */

	*info->iclrintradr =
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_descr, do) |
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_eop, do);

	if(!tty) /* something wrong... */
		return;

	descr = &info->rec_descr;
  
	/* find out how many bytes were read */

	/* if the eop bit was not set, all data has been received */
	if(!(descr->status & d_eop)) {
		recvl = descr->sw_len;
	} else {
		/* otherwise we find the amount of data received here */
		recvl = descr->hw_len;
	}
	if(recvl) {
		unsigned char *buf;
		struct async_icount *icount;

		icount = &info->icount;

		/* update stats */
		icount->rx += recvl;

		/* read the status register so we can detect errors */
		rstat = info->port[REG_STATUS];

		if(rstat & (IO_MASK(R_SERIAL0_STATUS, overrun) |
			    IO_MASK(R_SERIAL0_STATUS, par_err) |
			    IO_MASK(R_SERIAL0_STATUS, framing_err))) {
			/* if we got an error, we must reset it by reading the
			 * data_in field
			 */
			(void)info->port[REG_DATA];
		}
		
		/* we only ever write errors into the first byte in the flip 
		 * flag buffer, so we dont have to clear it all every time
		 */

		if(rstat & 0x04) {
			icount->parity++;
			*tty->flip.flag_buf_ptr = TTY_PARITY;
		} else if(rstat & 0x08) {
			icount->overrun++;
			*tty->flip.flag_buf_ptr = TTY_OVERRUN;
		} else if(rstat & 0x02) {
			icount->frame++;
			*tty->flip.flag_buf_ptr = TTY_FRAME;
		} else
			*tty->flip.flag_buf_ptr = 0;

		/* use the flip buffer next in turn to restart DMA into */
		
		if (tty->flip.buf_num) {
			buf = tty->flip.char_buf;
		} else {
			buf = tty->flip.char_buf + TTY_FLIPBUF_SIZE;
		}

		if(buf == phys_to_virt(descr->buf)) {
			printk("ttyS%d flip-buffer overrun!\n", info->line);
			icount->overrun++;
			*tty->flip.flag_buf_ptr = TTY_OVERRUN;
			/* restart old buffer */
		} else {
			descr->buf = virt_to_phys(buf);
			
			/* schedule or push a flip of the buffer */
			
			info->tty->flip.count = recvl;

#if (LINUX_VERSION_CODE > 131394) /* 2.1.66 */
			/* this includes a check for low-latency */
			tty_flip_buffer_push(tty);
#else
			queue_task_irq_off(&tty->flip.tqueue, &tq_timer);
#endif	
		}
	}
	
	/* restart the receiving dma */
	
	descr->sw_len = TTY_FLIPBUF_SIZE;
	descr->ctrl = d_int | d_eol | d_eop;
	descr->hw_len = 0;
	descr->status = 0;

	*info->ifirstadr = virt_to_phys(descr);
	*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, start);

#ifdef SERIAL_HANDLE_EARLY_ERRORS
	e100_enable_serial_data_irq(info);
#endif	
	/* input dma should be running now */
}

static void 
start_receive(struct e100_serial *info)
{
	struct etrax_dma_descr *descr;
	
#ifdef CONFIG_SVINTO_SIM
	/* No receive in the simulator.  Will probably be when the rest of
	 * the serial interface works, and this piece will just be removed.
	 */
	return;
#endif

	/* reset the input dma channel to be sure it works */
	
	*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, reset);
	while(IO_EXTRACT(R_DMA_CH6_CMD, cmd, *info->icmdadr) ==
	      IO_STATE_VALUE(R_DMA_CH6_CMD, cmd, reset));

	descr = &info->rec_descr;
	
	/* start the receiving dma into the flip buffer */
	
	descr->ctrl = d_int | d_eol | d_eop;
	descr->sw_len = TTY_FLIPBUF_SIZE;
	descr->buf = virt_to_phys(info->tty->flip.char_buf_ptr);
	descr->hw_len = 0;
	descr->status = 0;
	
	info->tty->flip.count = 0;

	*info->ifirstadr = virt_to_phys(descr);
	*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, start);
	
}


static _INLINE_ void 
status_handle(struct e100_serial *info, unsigned short status)
{
}

/* the bits in the MASK2 register are laid out like this:
   DMAI_EOP DMAI_DESCR DMAO_EOP DMAO_DESCR
   where I is the input channel and O is the output channel for the port.
   info->irq is the bit number for the DMAO_DESCR so to check the others we
   shift info->irq to the left.
*/

/* dma output channel interrupt handler
   this interrupt is called from DMA2(ser2), DMA4(ser3), DMA6(ser0) or
   DMA8(ser1) when they have finished a descriptor with the intr flag set.
*/

static void 
tr_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct e100_serial *info;
	unsigned long ireg;
	int i;
	
#ifdef CONFIG_SVINTO_SIM
	/* No receive in the simulator.  Will probably be when the rest of
	 * the serial interface works, and this piece will just be removed.
	 */
	{
		const char *s = "What? tr_interrupt in simulator??\n";
		SIMCOUT(s,strlen(s));
	}
	return;
#endif
	
	/* find out the line that caused this irq and get it from rs_table */
	
	ireg = *R_IRQ_MASK2_RD;  /* get the active irq bits for the dma channels */
	
	for(i = 0; i < NR_PORTS; i++) {
		info = rs_table + i;
		if (!info->uses_dma) 
			continue; 
		/* check for dma_descr (dont need to check for dma_eop in output dma for serial */
		if(ireg & info->irq) {  
			/* we can send a new dma bunch. make it so. */
			transmit_chars(info);
		}
		
		/* FIXME: here we should really check for a change in the
		   status lines and if so call status_handle(info) */
	}
}

/* dma input channel interrupt handler */

static void 
rec_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct e100_serial *info;
	unsigned long ireg;
	int i;

#ifdef CONFIG_SVINTO_SIM
	/* No receive in the simulator.  Will probably be when the rest of
	 * the serial interface works, and this piece will just be removed.
	 */
	{
		const char *s = "What? rec_interrupt in simulator??\n";
		SIMCOUT(s,strlen(s));
	}
	return;
#endif
	
	/* find out the line that caused this irq and get it from rs_table */
	
	ireg = *R_IRQ_MASK2_RD;  /* get the active irq bits for the dma channels */
	
	for(i = 0; i < NR_PORTS; i++) {
		info = rs_table + i;
		if (!info->uses_dma) 
			continue; 
		/* check for both dma_eop and dma_descr for the input dma channel */
		if(ireg & ((info->irq << 2) | (info->irq << 3))) {
			/* we have received something */
			receive_chars(info);
		}
		
		/* FIXME: here we should really check for a change in the
		   status lines and if so call status_handle(info) */
	}
}

/* dma fifo/buffer timeout handler
   forces an end-of-packet for the dma input channel if no chars 
   have been received for CONFIG_ETRAX_RX_TIMEOUT_TICKS/100 s.
   If CONFIG_ETRAX_SERIAL_FLUSH_DMA_FAST is configured then this
   handler is instead run at 15360 Hz.
*/

#ifndef CONFIG_ETRAX_SERIAL_FLUSH_DMA_FAST
static int timeout_divider = 0;
#endif

static struct timer_list flush_timer;

static void 
timed_flush_handler(unsigned long ptr)
{
	struct e100_serial *info;
	int i;
	unsigned int magic;

#ifdef CONFIG_SVINTO_SIM
	return;
#endif
	
	for(i = 0; i < NR_PORTS; i++) {
		info = rs_table + i;
		if(!info->enabled || !(info->flags & ASYNC_INITIALIZED))
			continue;

		/* istatusadr (bit 6-0) hold number of bytes in fifo 
		 * ihwswadr (bit 31-16) holds number of bytes in dma buffer
		 * ihwswadr (bit 15-0) specifies size of dma buffer
		 */

		magic = (*info->istatusadr & 0x3f);
		magic += ((*info->ihwswadr&0xffff ) - (*info->ihwswadr >> 16));

		/* if magic is equal to fifo_magic (magic in previous
		 * timeout_interrupt) then no new data has arrived since last
		 * interrupt and we'll force eop to flush fifo+dma buffers
		 */

		if(magic != info->fifo_magic) {
			info->fifo_magic = magic;
			info->fifo_didmagic = 0;
		} else {
			/* hit the timeout, force an EOP for the input
			 * dma channel if we haven't already
			 */
			if(!info->fifo_didmagic && magic) {
				info->fifo_didmagic = 1;
				info->fifo_magic = 0;
				*R_SET_EOP = 1U << info->iseteop;
			}
		}
	}

	/* restart flush timer */

	mod_timer(&flush_timer, jiffies + MAX_FLUSH_TIME);
}


#ifdef SERIAL_HANDLE_EARLY_ERRORS

/* If there is an error (ie break) when the DMA is running and
 * there are no bytes in the fifo the DMA is stopped and we get no
 * eop interrupt. Thus we have to monitor the first bytes on a DMA
 * transfer, and if it is without error we can turn the serial
 * interrupts off.
 */

static void 
ser_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct e100_serial *info;
	int i;
	unsigned char rstat;

	for(i = 0; i < NR_PORTS; i++) {
		info = rs_table + i;
		if (!info->uses_dma) 
			continue; 
		rstat = info->port[REG_STATUS];
		
		if(*R_IRQ_MASK1_RD & (1U << (8+2*info->line))) { /* This line caused the irq */
#ifdef SERIAL_DEBUG_INTR
			printk("Interrupt from serport %d\n", i);
#endif
			if(rstat & 0x0e) {
				/* FIXME: This is weird, but if this delay is
				 * not present then irmaflash does not work...
				 */
				udelay(2300);

				/* if we got an error, we must reset it by
				 * reading the data_in field
				 */
				(void)info->port[REG_DATA];
				
				PROCSTAT(early_errors_cnt[info->line]++);

				/* restart the DMA */
				*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, restart);
			} 
			else { /* it was a valid byte, now let the dma do the rest */
#ifdef SERIAL_DEBUG_INTR
				printk("** OK, disabling ser_interrupts\n");
#endif
				e100_disable_serial_data_irq(info);
			}
		}
	}
}
#endif

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */

/*
 * This routine is used to handle the "bottom half" processing for the
 * serial driver, known also the "software interrupt" processing.
 * This processing is done at the kernel interrupt level, after the
 * rs_interrupt() has returned, BUT WITH INTERRUPTS TURNED ON.  This
 * is where time-consuming activities which can not be done in the
 * interrupt driver proper are done; the interrupt driver schedules
 * them using rs_sched_event(), and they get done here.
 */
static void 
do_serial_bh(void)
{
	run_task_queue(&tq_serial);
}

static void 
do_softint(void *private_)
{
	struct e100_serial	*info = (struct e100_serial *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;
	
	if (test_and_clear_bit(RS_EVENT_WRITE_WAKEUP, &info->event)) {
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		    tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
		wake_up_interruptible(&tty->write_wait);
	}
}

/*
 * This routine is called from the scheduler tqueue when the interrupt
 * routine has signalled that a hangup has occurred.  The path of
 * hangup processing is:
 *
 * 	serial interrupt routine -> (scheduler tqueue) ->
 * 	do_serial_hangup() -> tty->hangup() -> rs_hangup()
 * 
 */
static void 
do_serial_hangup(void *private_)
{
	struct e100_serial	*info = (struct e100_serial *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;
	
	tty_hangup(tty);
}

static int 
startup(struct e100_serial * info)
{
	unsigned long flags;
	unsigned long page;

	page = get_zeroed_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	save_flags(flags); cli();

	/* if it was already initialized, skip this */
	
	if (info->flags & ASYNC_INITIALIZED) {
		free_page(page);
		restore_flags(flags);
		return 0;
	}
	
	if (info->xmit.buf)
		free_page(page);
	else
		info->xmit.buf = (unsigned char *) page;
		
#ifdef SERIAL_DEBUG_OPEN
	printk("starting up ttyS%d (xmit_buf 0x%x)...\n", info->line, info->xmit.buf);
#endif

	if(info->tty) {

		/* clear the tty flip flag buffer since we will not
		 * be using it (we only use the first byte..)
		 */

		memset(info->tty->flip.flag_buf, 0, TTY_FLIPBUF_SIZE * 2);
	}

	save_flags(flags);
	cli();
	
#ifdef CONFIG_SVINTO_SIM
	/* Bits and pieces collected from below.  Better to have them
	   in one ifdef:ed clause than to mix in a lot of ifdefs,
	   right? */
	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit.head = info->xmit.tail = 0;
	
	/* No real action in the simulator, but may set info important
	   to ioctl. */
	change_speed(info);
#else

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in change_speed())
	 */
	
	/*
	 * Reset the DMA channels and make sure their interrupts are cleared
	 */
	
	info->uses_dma = 1;
	*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, reset);
	*info->ocmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, reset);

	/* wait until reset cycle is complete */
	while(IO_EXTRACT(R_DMA_CH6_CMD, cmd, *info->icmdadr) ==
	      IO_STATE_VALUE(R_DMA_CH6_CMD, cmd, reset));

	while(IO_EXTRACT(R_DMA_CH6_CMD, cmd, *info->ocmdadr) ==
	      IO_STATE_VALUE(R_DMA_CH6_CMD, cmd, reset));
	
	*info->iclrintradr =
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_descr, do) |
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_eop, do);
	*info->oclrintradr =
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_descr, do) |
		IO_STATE(R_DMA_CH6_CLR_INTR, clr_eop, do);
	
	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);

        info->xmit.head = info->xmit.tail = 0;
	
	/*
	 * and set the speed and other flags of the serial port
	 * this will start the rx/tx as well
	 */
#ifdef SERIAL_HANDLE_EARLY_ERRORS
	e100_enable_serial_data_irq(info);
#endif	
	change_speed(info);

	/* dummy read to reset any serial errors */

	(void)info->port[REG_DATA];

	/* enable the interrupts */

	e100_enable_txdma_irq(info);
	e100_enable_rxdma_irq(info);

	info->tr_running = 0;  /* to be sure we dont lock up the transmitter */

	/* setup the dma input descriptor and start dma */
	
	start_receive(info);
	
	/* for safety, make sure the descriptors last result is 0 bytes written */
	
	info->tr_descr.sw_len = 0;
	info->tr_descr.hw_len = 0;
	info->tr_descr.status = 0;

	/* enable RTS/DTR last */

	e100_rts(info, 1);
	e100_dtr(info, 1);
		
#endif /* CONFIG_SVINTO_SIM */
	
	info->flags |= ASYNC_INITIALIZED;
	
	restore_flags(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void 
shutdown(struct e100_serial * info)
{
	unsigned long flags;

#ifndef CONFIG_SVINTO_SIM	
	/* shut down the transmitter and receiver  */

	e100_disable_rx(info);
	info->port[REG_TR_CTRL] = (info->tx_ctrl &= ~0x40);

	e100_disable_rxdma_irq(info);
	e100_disable_txdma_irq(info);

	info->tr_running = 0;

	/* reset both dma channels */

	*info->icmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, reset);
	*info->ocmdadr = IO_STATE(R_DMA_CH6_CMD, cmd, reset);

#endif /* CONFIG_SVINTO_SIM */

	if (!(info->flags & ASYNC_INITIALIZED))
		return;
	
#ifdef SERIAL_DEBUG_OPEN
	printk("Shutting down serial port %d (irq %d)....\n", info->line,
	       info->irq);
#endif
	
	save_flags(flags);
	cli(); /* Disable interrupts */
	
	if (info->xmit.buf) {
		unsigned long pg = (unsigned long) info->xmit.buf;
		info->xmit.buf = 0;
		free_page(pg);
	}

	if (!info->tty || (info->tty->termios->c_cflag & HUPCL)) {
		/* hang up DTR and RTS if HUPCL is enabled */
		e100_dtr(info, 0);
		e100_rts(info, 0); /* could check CRTSCTS before doing this */
	}

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);
	
	info->flags &= ~ASYNC_INITIALIZED;
	restore_flags(flags);
}


/* change baud rate and other assorted parameters */

static void 
change_speed(struct e100_serial *info)
{
	unsigned int cflag;

	/* first some safety checks */
	
	if(!info->tty || !info->tty->termios)
		return;
	if (!info->port)
		return;
	
	cflag = info->tty->termios->c_cflag;
	
	/* possibly, the tx/rx should be disabled first to do this safely */
	
	/* change baud-rate and write it to the hardware */
	
	info->baud = cflag_to_baud(cflag);
	
#ifndef CONFIG_SVINTO_SIM
	info->port[REG_BAUD] = cflag_to_etrax_baud(cflag);
	/* start with default settings and then fill in changes */

	/* 8 bit, no/even parity */
	info->rx_ctrl &= ~(IO_MASK(R_SERIAL0_REC_CTRL, rec_bitnr) |
			   IO_MASK(R_SERIAL0_REC_CTRL, rec_par_en) |
			   IO_MASK(R_SERIAL0_REC_CTRL, rec_par));

	/* 8 bit, no/even parity, 1 stop bit, no cts */
	info->tx_ctrl &= ~(IO_MASK(R_SERIAL0_TR_CTRL, tr_bitnr) |
			   IO_MASK(R_SERIAL0_TR_CTRL, tr_par_en) |
			   IO_MASK(R_SERIAL0_TR_CTRL, tr_par) |
			   IO_MASK(R_SERIAL0_TR_CTRL, stop_bits) |
			   IO_MASK(R_SERIAL0_TR_CTRL, auto_cts));
	
	if ((cflag & CSIZE) == CS7) {
		/* set 7 bit mode */
		info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_bitnr, tr_7bit);
		info->rx_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_bitnr, rec_7bit);
	}
	
	if (cflag & CSTOPB) {
		/* set 2 stop bit mode */
		info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, stop_bits, two_bits);
	}	  
	
	if (cflag & PARENB) {
		/* enable parity */
		info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_par_en, enable);
		info->rx_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_par_en, enable);
	}
	
	if (cflag & PARODD) {
		/* set odd parity */
		info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_par, odd);
		info->rx_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_par, odd);
	}
	
	if (cflag & CRTSCTS) {
		/* enable automatic CTS handling */
		info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, auto_cts, active);
	}
	
	/* make sure the tx and rx are enabled */
	
	info->tx_ctrl |= IO_STATE(R_SERIAL0_TR_CTRL, tr_enable, enable);
	info->rx_ctrl |= IO_STATE(R_SERIAL0_REC_CTRL, rec_enable, enable);

	/* actually write the control regs to the hardware */
	
	info->port[REG_TR_CTRL] = info->tx_ctrl;
	info->port[REG_REC_CTRL] = info->rx_ctrl;
	*((unsigned long *)&info->port[REG_XOFF]) = 0;

#endif /* CONFIG_SVINTO_SIM */
}

/* start transmitting chars NOW */

static void 
rs_flush_chars(struct tty_struct *tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;

	if (info->tr_running
	    || info->xmit.head == info->xmit.tail
	    || tty->stopped
	    || tty->hw_stopped
	    || !info->xmit.buf)
		return;

#ifdef SERIAL_DEBUG_FLOW
	printk("rs_flush_chars\n");
#endif
	
	/* this protection might not exactly be necessary here */
	
	save_flags(flags);
	cli();
	start_transmit(info);
	restore_flags(flags);
}

static int 
rs_write(struct tty_struct * tty, int from_user,
	 const unsigned char *buf, int count)
{
	int	c, ret = 0;
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;
	
	/* first some sanity checks */
	
	if (!tty || !info->xmit.buf || !tmp_buf)
		return 0;
	
#ifdef SERIAL_DEBUG_DATA
	if(info->line == SERIAL_DEBUG_LINE)
		printk("rs_write (%d), status %d\n", 
		       count, info->port[REG_STATUS]);
#endif

#ifdef CONFIG_SVINTO_SIM
	/* Really simple.  The output is here and now. */
	SIMCOUT(buf, count);
	return;
#endif
	save_flags(flags);
	
	/* the cli/restore_flags pairs below are needed because the
	 * DMA interrupt handler moves the info->xmit values. the memcpy
	 * needs to be in the critical region unfortunately, because we
	 * need to read xmit values, memcpy, write xmit values in one
	 * atomic operation... this could perhaps be avoided by more clever
	 * design.
	 */
	if(from_user) {
		down(&tmp_buf_sem);
		while (1) {
			int c1;
			c = CIRC_SPACE_TO_END(info->xmit.head,
					      info->xmit.tail,
					      SERIAL_XMIT_SIZE);
			if (count < c)
				c = count;
			if (c <= 0)
				break;

			c -= copy_from_user(tmp_buf, buf, c);
			if (!c) {
				if (!ret)
					ret = -EFAULT;
				break;
			}
			cli();
			c1 = CIRC_SPACE_TO_END(info->xmit.head,
					       info->xmit.tail,
					       SERIAL_XMIT_SIZE);
			if (c1 < c)
				c = c1;
			memcpy(info->xmit.buf + info->xmit.head, tmp_buf, c);
			info->xmit.head = ((info->xmit.head + c) &
					   (SERIAL_XMIT_SIZE-1));
			restore_flags(flags);
			buf += c;
			count -= c;
			ret += c;
		}
		up(&tmp_buf_sem);
	} else {
		cli();	
		while(1) {
			c = CIRC_SPACE_TO_END(info->xmit.head,
					      info->xmit.tail,
					      SERIAL_XMIT_SIZE);

			if (count < c)
				c = count;
			if (c <= 0)
				break;
		
			memcpy(info->xmit.buf + info->xmit.head, buf, c);
			info->xmit.head = (info->xmit.head + c) &
				(SERIAL_XMIT_SIZE-1);
			buf += c;
			count -= c;
			ret += c;
		}
		restore_flags(flags);
	}
	
	/* enable transmitter if not running, unless the tty is stopped
	 * this does not need IRQ protection since if tr_running == 0
	 * the IRQ's are not running anyway for this port.
	 */
	
	if(info->xmit.head != info->xmit.tail
	   && !tty->stopped &&
	   !tty->hw_stopped &&
	   !info->tr_running) {
		start_transmit(info);
	}
 	
	return ret;
}

/* how much space is available in the xmit buffer? */

static int 
rs_write_room(struct tty_struct *tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	
	return CIRC_SPACE(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

/* How many chars are in the xmit buffer?
 * This does not include any chars in the transmitter FIFO.
 * Use wait_until_sent for waiting for FIFO drain.
 */

static int 
rs_chars_in_buffer(struct tty_struct *tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;

	return CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

/* discard everything in the xmit buffer */

static void 
rs_flush_buffer(struct tty_struct *tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;
	
	save_flags(flags);
	cli();
	info->xmit.head = info->xmit.tail = 0;
	restore_flags(flags);

	wake_up_interruptible(&tty->write_wait);

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 *
 * Since we don't bother to check for info->x_char in transmit_chars yet,
 * we don't really implement this function yet.
 */
static void rs_send_xchar(struct tty_struct *tty, char ch)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;

	printk("serial.c:rs_send_xchar not implemented!\n");

	info->x_char = ch;
	if (ch) {
		/* Make sure transmit interrupts are on */
		/* TODO. */
	}
}

/*
 * ------------------------------------------------------------
 * rs_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void 
rs_throttle(struct tty_struct * tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("throttle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif
	
	if (I_IXOFF(tty))
		info->x_char = STOP_CHAR(tty);
	
	/* Turn off RTS line (do this atomic) should here be an else ?? */
	
	save_flags(flags); 
	cli();
	e100_rts(info, 0);
	restore_flags(flags);
}

static void 
rs_unthrottle(struct tty_struct * tty)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("unthrottle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif
	
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			info->x_char = START_CHAR(tty);
	}
	
	/* Assert RTS line (do this atomic) */
	
	save_flags(flags); 
	cli();
	e100_rts(info, 1);
	restore_flags(flags);
}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int 
get_serial_info(struct e100_serial * info,
		struct serial_struct * retinfo)
{
	struct serial_struct tmp;
	
	/* this is all probably wrong, there are a lot of fields
	 * here that we don't have in e100_serial and maybe we
	 * should set them to something else than 0.
	 */

	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->line;
	tmp.port = (int)info->port;
	tmp.irq = info->irq;
	tmp.flags = info->flags;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	if (copy_to_user(retinfo,&tmp,sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int
set_serial_info(struct e100_serial * info,
		struct serial_struct * new_info)
{
	struct serial_struct new_serial;
	struct e100_serial old_info;
	int 			retval = 0;

	if (copy_from_user(&new_serial,new_info,sizeof(new_serial)))
		return -EFAULT;

	old_info = *info;
	
	if(!capable(CAP_SYS_ADMIN)) {
		if((new_serial.type != info->type) ||
		   (new_serial.close_delay != info->close_delay) ||
		   ((new_serial.flags & ~ASYNC_USR_MASK) !=
		    (info->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		goto check_and_exit;
	}
	
	if (info->count > 1)
		return -EBUSY;
	
	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */
	
	info->flags = ((info->flags & ~ASYNC_FLAGS) |
		       (new_serial.flags & ASYNC_FLAGS));
	info->type = new_serial.type;
	info->close_delay = new_serial.close_delay;
	info->closing_wait = new_serial.closing_wait;
#if (LINUX_VERSION_CODE > 0x20100)
	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;
#endif

 check_and_exit:
	if(info->flags & ASYNC_INITIALIZED) {
		change_speed(info);
	} else
		retval = startup(info);
	return retval;
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space. 
 */
static int 
get_lsr_info(struct e100_serial * info, unsigned int *value)
{
	unsigned int result;

#ifdef CONFIG_SVINTO_SIM
	/* Always open. */
	result = TIOCSER_TEMT;
#else
	if (*info->ostatusadr & 0x007F)  /* something in fifo */
		result = 0;
	else
		result = TIOCSER_TEMT;
#endif

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

#ifdef SERIAL_DEBUG_IO 
struct state_str
{
  int state;
  const char *str;
  
};

const struct state_str control_state_str[]={
	{TIOCM_DTR, "DTR" },
	{TIOCM_RTS, "RTS"},
	{TIOCM_ST, "ST?" },
	{TIOCM_SR, "SR?" },
	{TIOCM_CTS, "CTS" },
	{TIOCM_CD, "CD" },
	{TIOCM_RI, "RI" },
	{TIOCM_DSR, "DSR" },
	{0, NULL }
};

char *get_control_state_str(int MLines, char *s)
{
	int i = 0;
	s[0]='\0';
	while (control_state_str[i].str != NULL) {
		if (MLines & control_state_str[i].state) {
			if (s[0] != '\0') {
				strcat(s, ", ");
			}
			strcat(s, control_state_str[i].str);
		}
		i++;
	}
	return s;
}
#endif

static int 
get_modem_info(struct e100_serial * info, unsigned int *value)
{
	unsigned int result;
	/* Polarity isn't verified */
#if 0 /*def SERIAL_DEBUG_IO  */

	printk("get_modem_info: RTS: %i DTR: %i CD: %i RI: %i DSR: %i CTS: %i\n",
	       E100_RTS_GET(info),
	       E100_DTR_GET(info),
	       E100_CD_GET(info),
	       E100_RI_GET(info),
	       E100_DSR_GET(info),
	       E100_CTS_GET(info));
#endif
	result =  
		(!E100_RTS_GET(info) ? TIOCM_RTS : 0)
		| (!E100_DTR_GET(info) ? TIOCM_DTR : 0)
		| (!E100_CD_GET(info) ? TIOCM_CAR : 0)
		| (!E100_RI_GET(info) ? TIOCM_RNG : 0)
		| (!E100_DSR_GET(info) ? TIOCM_DSR : 0)
		| (!E100_CTS_GET(info) ? TIOCM_CTS : 0);
	
#ifdef SERIAL_DEBUG_IO 
	printk("e100ser: modem state: %i 0x%08X\n", result, result);
	{
		char s[100];
		
		get_control_state_str(result, s);
		printk("state: %s\n", s);
	}
#endif  
	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}


static int
set_modem_info(struct e100_serial * info, unsigned int cmd,
	       unsigned int *value)
{
	unsigned int arg;

	if (copy_from_user(&arg, value, sizeof(int)))
		return -EFAULT;

	switch (cmd) {
	case TIOCMBIS: 
		if (arg & TIOCM_RTS) {
			e100_rts(info, 1);
		}
		if (arg & TIOCM_DTR) {
			e100_dtr(info, 1);
		}
		/* Handle FEMALE behaviour */
		if (arg & TIOCM_RI) {
			e100_ri_out(info, 1);
		}
		if (arg & TIOCM_CD) {
			e100_cd_out(info, 1);
		}
		break;
	case TIOCMBIC:
		if (arg & TIOCM_RTS) {
			e100_rts(info, 0);
		}
		if (arg & TIOCM_DTR) {
			e100_dtr(info, 0);
		}
		/* Handle FEMALE behaviour */
		if (arg & TIOCM_RI) {
			e100_ri_out(info, 0);
		}
		if (arg & TIOCM_CD) {
			e100_cd_out(info, 0);
		}
		break;
	case TIOCMSET:
		e100_rts(info, arg & TIOCM_RTS);
		e100_dtr(info, arg & TIOCM_DTR);
		/* Handle FEMALE behaviour */
		e100_ri_out(info, arg & TIOCM_RI);
		e100_cd_out(info, arg & TIOCM_CD);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * This routine sends a break character out the serial port.
 */
#if (LINUX_VERSION_CODE < 131394) /* Linux 2.1.66 */
static void 
send_break(struct e100_serial * info, int duration)
{
	unsigned long flags;	

	if (!info->port)
		return;

	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + duration;

	save_flags(flags);
	cli();

	/* Go to manual mode and set the txd pin to 0 */

	info->tx_ctrl &= 0x3F; /* Clear bit 7 (txd) and 6 (tr_enable) */
	info->port[REG_TR_CTRL] = info->tx_ctrl;

	/* wait for "duration" jiffies */

	schedule();

	info->tx_ctrl |= (0x80 | 0x40); /* Set bit 7 (txd) and 6 (tr_enable) */
	info->port[REG_TR_CTRL] = info->tx_ctrl;

	/* the DMA gets awfully confused if we toggle the tranceiver like this 
	 * so we need to reset it 
	 */
	*info->ocmdadr = 4;

	restore_flags(flags);
}
#else
static void 
rs_break(struct tty_struct *tty, int break_state)
{
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;

	if (!info->port)
		return;
	
	save_flags(flags);
	cli();
	if (break_state == -1) {
		/* Go to manual mode and set the txd pin to 0 */
		info->tx_ctrl &= 0x3F; /* Clear bit 7 (txd) and 6 (tr_enable) */
	} else {
		info->tx_ctrl |= (0x80 | 0x40); /* Set bit 7 (txd) and 6 (tr_enable) */
	}
	info->port[REG_TR_CTRL] = info->tx_ctrl;
	restore_flags(flags);
}
#endif

static int 
rs_ioctl(struct tty_struct *tty, struct file * file,
	 unsigned int cmd, unsigned long arg)
{
	int error;
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;
	int retval;
	
	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGWILD)  &&
	    (cmd != TIOCSERSWILD) && (cmd != TIOCSERGSTRUCT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
			return -EIO;
	}
	
	switch (cmd) {
#if (LINUX_VERSION_CODE < 131394) /* Linux 2.1.66 */
	        case TCSBRK:	/* SVID version: non-zero arg --> no break */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (signal_pending(current))
				return -EINTR;
			if (!arg) {
				send_break(info, HZ/4);	/* 1/4 second */
				if (signal_pending(current))
					return -EINTR;
			}
			return 0;
		case TCSBRKP:	/* support for POSIX tcsendbreak() */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (signal_pending(current))
				return -EINTR;
			send_break(info, arg ? arg*(HZ/10) : HZ/4);
			if (signal_pending(current))
				return -EINTR;
			return 0;
		case TIOCGSOFTCAR:
			error = verify_area(VERIFY_WRITE, (void *) arg,sizeof(long));
			if (error)
				return error;
			put_fs_long(C_CLOCAL(tty) ? 1 : 0,
				    (unsigned long *) arg);
			return 0;
		case TIOCSSOFTCAR:
			arg = get_fs_long((unsigned long *) arg);
			tty->termios->c_cflag =
				((tty->termios->c_cflag & ~CLOCAL) |
				 (arg ? CLOCAL : 0));
			return 0;
#endif
		case TIOCMGET:
			return get_modem_info(info, (unsigned int *) arg);
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			return set_modem_info(info, cmd, (unsigned int *) arg);
		case TIOCGSERIAL:
			return get_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSSERIAL:
			return set_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSERGETLSR: /* Get line status register */
			return get_lsr_info(info, (unsigned int *) arg);

		case TIOCSERGSTRUCT:
			if (copy_to_user((struct e100_serial *) arg,
					 info, sizeof(struct e100_serial)))
				return -EFAULT;
			return 0;

#if defined(CONFIG_ETRAX_RS485)
		case TIOCSERSETRS485:
			error = verify_area(VERIFY_WRITE, (void *) arg,
					sizeof(struct rs485_control));
			
			if (error)
				return error;
			
			return e100_enable_rs485(tty, (struct rs485_control *) arg);

		case TIOCSERWRRS485:
			error = verify_area(VERIFY_WRITE, (void *) arg,
					sizeof(struct rs485_write));
			
			if (error)
				return error;
			
			return e100_write_rs485(tty, (struct rs485_write *) arg);
#endif
			
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static void 
rs_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;

	change_speed(info);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		rs_start(tty);
	}
	
}

/*
 * ------------------------------------------------------------
 * rs_close()
 * 
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * S structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void 
rs_close(struct tty_struct *tty, struct file * filp)
{
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;
	unsigned long flags;

	if (!info)
		return;
  
	/* interrupts are disabled for this entire function */
  
	save_flags(flags); 
	cli();
  
	if (tty_hung_up_p(filp)) {
		restore_flags(flags);
		return;
	}
  
#ifdef SERIAL_DEBUG_OPEN
	printk("[%d] rs_close ttyS%d, count = %d\n", current->pid, 
	       info->line, info->count);
#endif
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("rs_close: bad serial port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk("rs_close: bad serial port count for ttyS%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		restore_flags(flags);
		return;
	}
	info->flags |= ASYNC_CLOSING;
	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */
	if (info->flags & ASYNC_NORMAL_ACTIVE)
		info->normal_termios = *tty->termios;
	if (info->flags & ASYNC_CALLOUT_ACTIVE)
		info->callout_termios = *tty->termios;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the serial receiver and the DMA receive interrupt.
	 */
#ifdef SERIAL_HANDLE_EARLY_ERRORS 
	e100_disable_serial_data_irq(info);
#endif

#ifndef CONFIG_SVINTO_SIM
	e100_disable_rx(info);
	e100_disable_rxdma_irq(info);

	if (info->flags & ASYNC_INITIALIZED) {
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important as we have a transmit FIFO!
		 */
		rs_wait_until_sent(tty, HZ);
	}
#endif

	shutdown(info);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = 0;
	if (info->blocked_open) {
		if (info->close_delay) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(info->close_delay);
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE|
			 ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);
	restore_flags(flags);

	/* port closed */

#if defined(CONFIG_ETRAX_RS485)
	if (info->rs485.enabled) {
		info->rs485.enabled = 0;
#if defined(CONFIG_ETRAX_RS485_ON_PA)
		*R_PORT_PA_DATA = port_pa_data_shadow &= ~(1 << rs485_pa_bit);
#endif
	}
#endif
}

/*
 * rs_wait_until_sent() --- wait until the transmitter is empty
 */
static void rs_wait_until_sent(struct tty_struct *tty, int timeout)
{
	unsigned long orig_jiffies;
	struct e100_serial *info = (struct e100_serial *)tty->driver_data;

	/*
	 * Check R_DMA_CHx_STATUS bit 0-6=number of available bytes in FIFO
	 * R_DMA_CHx_HWSW bit 31-16=nbr of bytes left in DMA buffer (0=64k)
	 */
	orig_jiffies = jiffies;
	while(info->xmit.head != info->xmit.tail || /* More in send queue */
	      (*info->ostatusadr & 0x007f)) { /* more in FIFO */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, orig_jiffies + timeout))
			break;
	}
	set_current_state(TASK_RUNNING);
}

/*
 * rs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
void 
rs_hangup(struct tty_struct *tty)
{
	struct e100_serial * info = (struct e100_serial *)tty->driver_data;
	
	rs_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE);
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * rs_open() and friends
 * ------------------------------------------------------------
 */
static int 
block_til_ready(struct tty_struct *tty, struct file * filp,
		struct e100_serial *info)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long   flags;
	int		retval;
	int		do_clocal = 0, extra_count = 0;
	
	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		if (info->flags & ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}
  
	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */
	if (tty->driver.subtype == SERIAL_TYPE_CALLOUT) {
		if (info->flags & ASYNC_NORMAL_ACTIVE)
			return -EBUSY;
		if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ASYNC_SESSION_LOCKOUT) &&
		    (info->session != current->session))
			return -EBUSY;
		if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ASYNC_PGRP_LOCKOUT) &&
		    (info->pgrp != current->pgrp))
			return -EBUSY;
		info->flags |= ASYNC_CALLOUT_ACTIVE;
		return 0;
	}
	
	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		if (info->flags & ASYNC_CALLOUT_ACTIVE)
			return -EBUSY;
		info->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}
	
	if (info->flags & ASYNC_CALLOUT_ACTIVE) {
		if (info->normal_termios.c_cflag & CLOCAL)
			do_clocal = 1;
	} else {
		if (tty->termios->c_cflag & CLOCAL)
			do_clocal = 1;
	}
	
	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready before block: ttyS%d, count = %d\n",
	       info->line, info->count);
#endif
	save_flags(flags); 
	cli();
	if (!tty_hung_up_p(filp)) {
		extra_count++;
		info->count--;
	}
	restore_flags(flags);
	info->blocked_open++;
	while (1) {
		save_flags(flags);
		cli();
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE)) {
			/* assert RTS and DTR */
			e100_rts(info, 1);
			e100_dtr(info, 1);
		}
		restore_flags(flags);
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ASYNC_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
			if (info->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;	
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    !(info->flags & ASYNC_CLOSING) && do_clocal)
			/* && (do_clocal || DCD_IS_ASSERTED) */
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef SERIAL_DEBUG_OPEN
		printk("block_til_ready blocking: ttyS%d, count = %d\n",
		       info->line, info->count);
#endif
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&info->open_wait, &wait);
	if (extra_count)
		info->count++;
	info->blocked_open--;
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready after blocking: ttyS%d, count = %d\n",
	       info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}	

/*
 * This routine is called whenever a serial port is opened. 
 * It performs the serial-specific initialization for the tty structure.
 */
static int 
rs_open(struct tty_struct *tty, struct file * filp)
{
	struct e100_serial	*info;
	int 			retval, line;
	unsigned long           page;

	/* find which port we want to open */

	line = MINOR(tty->device) - tty->driver.minor_start;
  
	if (line < 0 || line >= NR_PORTS)
		return -ENODEV;

	/* find the corresponding e100_serial struct in the table */

	info = rs_table + line;
	/* dont allow the opening of ports that are not enabled in the HW config */
	if (!info->enabled) return -ENODEV; 
  
#ifdef SERIAL_DEBUG_OPEN
	printk("[%d] rs_open %s%d, count = %d\n", current->pid,
	       tty->driver.name, info->line,
	       info->count);
#endif

	info->count++;
	tty->driver_data = info;
	info->tty = tty;

#if (LINUX_VERSION_CODE > 0x20100)
	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;
#endif

	if (!tmp_buf) {
		page = get_zeroed_page(GFP_KERNEL);
		if (!page) {
			return -ENOMEM;
		}
		if (tmp_buf)
			free_page(page);
		else
			tmp_buf = (unsigned char *) page;
	}

	/*
	 * If the port is the middle of closing, bail out now
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		return ((info->flags & ASYNC_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS);
#else
		return -EAGAIN;
#endif
	}

	/*
	 * Start up the serial port
	 */

	retval = startup(info);
	if (retval)
		return retval;
  
	retval = block_til_ready(tty, filp, info);
	if (retval) {
#ifdef SERIAL_DEBUG_OPEN
		printk("rs_open returning after block_til_ready with %d\n",
		       retval);
#endif
		return retval;
	}

	if ((info->count == 1) && (info->flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
			*tty->termios = info->normal_termios;
		else 
			*tty->termios = info->callout_termios;
		change_speed(info);
	}

	info->session = current->session;
	info->pgrp = current->pgrp;
  
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_open ttyS%d successful...\n", info->line);
#endif
	return 0;
}

/*
 * /proc fs routines....
 */

static inline int line_info(char *buf, struct e100_serial *info)
{
	char	stat_buf[30], control, status;
	int	ret;
	unsigned long flags;

	ret = sprintf(buf, "%d: uart:E100 port:%lX irq:%d",
		      info->line, info->port, info->irq);

	if (!info->port || (info->type == PORT_UNKNOWN)) {
		ret += sprintf(buf+ret, "\n");
		return ret;
	}

	stat_buf[0] = 0;
	stat_buf[1] = 0;
	if (E100_RTS_GET(info))
		strcat(stat_buf, "|RTS");
	if (E100_CTS_GET(info))
		strcat(stat_buf, "|CTS");
	if (E100_DTR_GET(info))
		strcat(stat_buf, "|DTR");
	if (E100_DSR_GET(info))
		strcat(stat_buf, "|DSR");
	if (E100_CD_GET(info))
		strcat(stat_buf, "|CD");
	if (E100_RI_GET(info))
		strcat(stat_buf, "|RI");

	ret += sprintf(buf+ret, " baud:%d", info->baud);

	ret += sprintf(buf+ret, " tx:%d rx:%d",
		      info->icount.tx, info->icount.rx);

	if (info->icount.frame)
		ret += sprintf(buf+ret, " fe:%d", info->icount.frame);
	
	if (info->icount.parity)
		ret += sprintf(buf+ret, " pe:%d", info->icount.parity);
	
	if (info->icount.brk)
		ret += sprintf(buf+ret, " brk:%d", info->icount.brk);	

	if (info->icount.overrun)
		ret += sprintf(buf+ret, " oe:%d", info->icount.overrun);

	/*
	 * Last thing is the RS-232 status lines
	 */
	ret += sprintf(buf+ret, " %s\n", stat_buf+1);
	return ret;
}

int rs_read_proc(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	int i, len = 0, l;
	off_t	begin = 0;

	len += sprintf(page, "serinfo:1.0 driver:%s\n",
		       serial_version);
	for (i = 0; i < NR_PORTS && len < 4000; i++) {
		if (!rs_table[i].enabled) 
			continue; 
		l = line_info(page + len, &rs_table[i]);
		len += l;
		if (len+begin > off+count)
			goto done;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
	}
	*eof = 1;
done:
	if (off >= len+begin)
		return 0;
	*start = page + (off-begin);
	return ((count < begin+len-off) ? count : begin+len-off);
}

/* Finally, routines used to initialize the serial driver. */

static void 
show_serial_version(void)
{
	printk("ETRAX 100LX serial-driver %s, (c) 2000 Axis Communications AB\r\n",
	       serial_version);
}

/* rs_init inits the driver at boot (using the module_init chain) */

static int __init
rs_init(void)
{
	int i;
	struct e100_serial *info;

	show_serial_version();
        
	init_bh(SERIAL_BH, do_serial_bh);

	/* Setup the timed flush handler system */

	init_timer(&flush_timer);
	flush_timer.function = timed_flush_handler;
	mod_timer(&flush_timer, jiffies + MAX_FLUSH_TIME);

	/* Initialize the tty_driver structure */
  
	memset(&serial_driver, 0, sizeof(struct tty_driver));
	serial_driver.magic = TTY_DRIVER_MAGIC;
#if (LINUX_VERSION_CODE > 0x20100)
	serial_driver.driver_name = "serial";
#endif
	serial_driver.name = "ttyS";
	serial_driver.major = TTY_MAJOR;
	serial_driver.minor_start = 64;
	serial_driver.num = NR_PORTS;       /* etrax100 has 4 serial ports */
	serial_driver.type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver.subtype = SERIAL_TYPE_NORMAL;
	serial_driver.init_termios = tty_std_termios;
	serial_driver.init_termios.c_cflag =
		B115200 | CS8 | CREAD | HUPCL | CLOCAL; /* is normally B9600 default... */
	serial_driver.flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	serial_driver.refcount = &serial_refcount;
	serial_driver.table = serial_table;
	serial_driver.termios = serial_termios;
	serial_driver.termios_locked = serial_termios_locked;
  
	serial_driver.open = rs_open;
	serial_driver.close = rs_close;
	serial_driver.write = rs_write;
	/* should we have an rs_put_char as well here ? */
	serial_driver.flush_chars = rs_flush_chars;
	serial_driver.write_room = rs_write_room;
	serial_driver.chars_in_buffer = rs_chars_in_buffer;
	serial_driver.flush_buffer = rs_flush_buffer;
	serial_driver.ioctl = rs_ioctl;
	serial_driver.throttle = rs_throttle;
	serial_driver.unthrottle = rs_unthrottle;
	serial_driver.set_termios = rs_set_termios;
	serial_driver.stop = rs_stop;
	serial_driver.start = rs_start;
	serial_driver.hangup = rs_hangup;
#if (LINUX_VERSION_CODE >= 131394) /* Linux 2.1.66 */
	serial_driver.break_ctl = rs_break;
#endif
#if (LINUX_VERSION_CODE >= 131343)
	serial_driver.send_xchar = rs_send_xchar;
	serial_driver.wait_until_sent = rs_wait_until_sent;
	serial_driver.read_proc = rs_read_proc;
#endif
	  
	/*
	 * The callout device is just like normal device except for
	 * major number and the subtype code.
	 */
	callout_driver = serial_driver;
	callout_driver.name = "cua";
	callout_driver.major = TTYAUX_MAJOR;
	callout_driver.subtype = SERIAL_TYPE_CALLOUT;
#if (LINUX_VERSION_CODE >= 131343)
	callout_driver.read_proc = 0;
	callout_driver.proc_entry = 0;
#endif
  
	if (tty_register_driver(&serial_driver))
		panic("Couldn't register serial driver\n");
	if (tty_register_driver(&callout_driver))
		panic("Couldn't register callout driver\n");
  
	/* do some initializing for the separate ports */
  
	for (i = 0, info = rs_table; i < NR_PORTS; i++,info++) {
		info->uses_dma = 0;   
		info->line = i;
		info->tty = 0;
		info->type = PORT_ETRAX;
		info->tr_running = 0;
		info->fifo_magic = 0;
		info->fifo_didmagic = 0;
		info->flags = 0;
		info->close_delay = 5*HZ/10;
		info->closing_wait = 30*HZ;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;
		info->tqueue.routine = do_softint;
		info->tqueue.data = info;
		info->callout_termios = callout_driver.init_termios;
		info->normal_termios = serial_driver.init_termios;
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);
		info->xmit.buf = 0;
		info->xmit.tail = info->xmit.head = 0;
		if (info->enabled) {
			printk(KERN_INFO "%s%d at 0x%x is a builtin UART with DMA\n",
			       serial_driver.name, info->line, (unsigned int)info->port);
		}
	}

#ifndef CONFIG_SVINTO_SIM
	/* Not needed in simulator.  May only complicate stuff. */
	/* hook the irq's for DMA channel 6 and 7, serial output and input, and some more... */
#ifdef CONFIG_ETRAX_SERIAL_PORT0
	if(request_irq(SER0_DMA_TX_IRQ_NBR, tr_interrupt, SA_INTERRUPT, "serial 0 dma tr", NULL))
		panic("irq22");
	if(request_irq(SER0_DMA_RX_IRQ_NBR, rec_interrupt, SA_INTERRUPT, "serial 0 dma rec", NULL))
		panic("irq23");
#endif
#ifdef SERIAL_HANDLE_EARLY_ERRORS
	if(request_irq(SERIAL_IRQ_NBR, ser_interrupt, SA_INTERRUPT, "serial ", NULL))
		panic("irq8");
#endif
#ifdef CONFIG_ETRAX_SERIAL_PORT1
	if(request_irq(SER1_DMA_TX_IRQ_NBR, tr_interrupt, SA_INTERRUPT, "serial 1 dma tr", NULL))
		panic("irq24");
	if(request_irq(SER1_DMA_RX_IRQ_NBR, rec_interrupt, SA_INTERRUPT, "serial 1 dma rec", NULL))
		panic("irq25");
#endif
#ifdef CONFIG_ETRAX_SERIAL_PORT2
	/* DMA Shared with par0 (and SCSI0 and ATA) */
	if(request_irq(SER2_DMA_TX_IRQ_NBR, tr_interrupt, SA_SHIRQ, "serial 2 dma tr", NULL))
		panic("irq18");
	if(request_irq(SER2_DMA_RX_IRQ_NBR, rec_interrupt, SA_SHIRQ, "serial 2 dma rec", NULL))
		panic("irq19");
#endif
#ifdef CONFIG_ETRAX_SERIAL_PORT3
	/* DMA Shared with par1 (and SCSI1 and Extern DMA 0) */
	if(request_irq(SER3_DMA_TX_IRQ_NBR, tr_interrupt, SA_SHIRQ, "serial 3 dma tr", NULL))
		panic("irq20");
	if(request_irq(SER3_DMA_RX_IRQ_NBR, rec_interrupt, SA_SHIRQ, "serial 3 dma rec", NULL))
		panic("irq21");
#endif
#ifdef CONFIG_ETRAX_SERIAL_FLUSH_DMA_FAST
	/* TODO: a timeout_interrupt needs to be written that calls timeout_handler */
	if(request_irq(TIMER1_IRQ_NBR, timeout_interrupt, SA_SHIRQ,
		       "fast serial dma timeout", NULL)) {
		printk("err: timer1 irq\n");
	}
#endif
#endif /* CONFIG_SVINTO_SIM */

	return 0;
}

/* this makes sure that rs_init is called during kernel boot */

module_init(rs_init);

/*
 * register_serial and unregister_serial allows for serial ports to be
 * configured at run-time, to support PCMCIA modems.
 */
int 
register_serial(struct serial_struct *req)
{
	return -1;
}

void unregister_serial(int line)
{
}
