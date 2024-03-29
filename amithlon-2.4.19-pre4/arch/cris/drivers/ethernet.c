/* $Id: ethernet.c,v 1.22 2002/01/30 07:48:22 matsfg Exp $
 *
 * e100net.c: A network driver for the ETRAX 100LX network controller.
 *
 * Copyright (c) 1998-2001 Axis Communications AB.
 *
 * The outline of this driver comes from skeleton.c.
 *
 * $Log: ethernet.c,v $
 * Revision 1.22  2002/01/30 07:48:22  matsfg
 * Initiate R_NETWORK_TR_CTRL
 *
 * Revision 1.21  2001/11/23 11:54:49  starvik
 * Added IFF_PROMISC and IFF_ALLMULTI handling in set_multicast_list
 * Removed compiler warnings
 *
 * Revision 1.20  2001/11/12 19:26:00  pkj
 * * Corrected e100_negotiate() to not assign half to current_duplex when
 *   it was supposed to compare them...
 * * Cleaned up failure handling in e100_open().
 * * Fixed compiler warnings.
 *
 * Revision 1.19  2001/11/09 07:43:09  starvik
 * Added full duplex support
 * Added ioctl to set speed and duplex
 * Clear LED timer only runs when LED is lit
 *
 * Revision 1.18  2001/10/03 14:40:43  jonashg
 * Update rx_bytes counter.
 *
 * Revision 1.17  2001/06/11 12:43:46  olof
 * Modified defines for network LED behavior
 *
 * Revision 1.16  2001/05/30 06:12:46  markusl
 * TxDesc.next should not be set to NULL
 *
 * Revision 1.15  2001/05/29 10:27:04  markusl
 * Updated after review remarks:
 * +Use IO_EXTRACT
 * +Handle underrun
 *
 * Revision 1.14  2001/05/29 09:20:14  jonashg
 * Use driver name on printk output so one can tell which driver that complains.
 *
 * Revision 1.13  2001/05/09 12:35:59  johana
 * Use DMA_NBR and IRQ_NBR defines from dma.h and irq.h
 *
 * Revision 1.12  2001/04/05 11:43:11  tobiasa
 * Check dev before panic.
 *
 * Revision 1.11  2001/04/04 11:21:05  markusl
 * Updated according to review remarks
 *
 * Revision 1.10  2001/03/26 16:03:06  bjornw
 * Needs linux/config.h
 *
 * Revision 1.9  2001/03/19 14:47:48  pkj
 * * Make sure there is always a pause after the network LEDs are
 *   changed so they will not look constantly lit during heavy traffic.
 * * Always use HZ when setting times relative to jiffies.
 * * Use LED_NETWORK_SET() when setting the network LEDs.
 *
 * Revision 1.8  2001/02/27 13:52:48  bjornw
 * malloc.h -> slab.h
 *
 * Revision 1.7  2001/02/23 13:46:38  bjornw
 * Spellling check
 *
 * Revision 1.6  2001/01/26 15:21:04  starvik
 * Don't disable interrupts while reading MDIO registers (MDIO is slow)
 * Corrected promiscuous mode
 * Improved deallocation of IRQs ("ifconfig eth0 down" now works)
 *
 * Revision 1.5  2000/11/29 17:22:22  bjornw
 * Get rid of the udword types legacy stuff
 *
 * Revision 1.4  2000/11/22 16:36:09  bjornw
 * Please marketing by using the correct case when spelling Etrax.
 *
 * Revision 1.3  2000/11/21 16:43:04  bjornw
 * Minor short->int change
 *
 * Revision 1.2  2000/11/08 14:27:57  bjornw
 * 2.4 port
 *
 * Revision 1.1  2000/11/06 13:56:00  bjornw
 * Verbatim copy of the 1.24 version of e100net.c from elinux
 *
 * Revision 1.24  2000/10/04 15:55:23  bjornw
 * * Use virt_to_phys etc. for DMA addresses
 * * Removed bogus CHECKSUM_UNNECESSARY
 *
 *
 */

#include <linux/config.h>

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/svinto.h>     /* DMA and register descriptions */
#include <asm/io.h>         /* LED_* I/O functions */
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/ethernet.h>

//#define ETHDEBUG
#define D(x)


/*
 * The name of the card. Is used for messages and in the requests for
 * io regions, irqs and dma channels
 */

static const char* cardname = "ETRAX 100LX built-in ethernet controller";

/* A default ethernet address. Highlevel SW will set the real one later */

static struct sockaddr default_mac = {
	0,
	{ 0x00, 0x40, 0x8C, 0xCD, 0x00, 0x00 }
};

/* Information that need to be kept for each board. */
struct net_local {
	struct net_device_stats stats;

	/* Tx control lock.  This protects the transmit buffer ring
	 * state along with the "tx full" state of the driver.  This
	 * means all netif_queue flow control actions are protected
	 * by this lock as well.
	 */
	spinlock_t lock;
};


/* Duplex settings */
enum duplex
{
	half,
	full,
	autoneg
};

/* Dma descriptors etc. */

#define RX_BUF_SIZE 32768

#define MAX_MEDIA_DATA_SIZE 1518

#define MIN_PACKET_LEN      46
#define ETHER_HEAD_LEN      14

/* 
** MDIO constants.
*/
#define MDIO_BASE_STATUS_REG                0x1
#define MDIO_BASE_CONTROL_REG               0x0
#define MDIO_BC_NEGOTIATE                0x0200
#define MDIO_BC_FULL_DUPLEX_MASK         0x0100
#define MDIO_BC_AUTO_NEG_MASK            0x1000
#define MDIO_BC_SPEED_SELECT_MASK        0x2000
#define MDIO_ADVERTISMENT_REG               0x4
#define MDIO_ADVERT_100_FD                0x100
#define MDIO_ADVERT_100_HD                0x080
#define MDIO_ADVERT_10_FD                 0x040
#define MDIO_ADVERT_10_HD                 0x020
#define MDIO_LINK_UP_MASK                   0x4
#define MDIO_START                          0x1
#define MDIO_READ                           0x2
#define MDIO_WRITE                          0x1
#define MDIO_PREAMBLE              0xfffffffful

/* Broadcom specific */
#define MDIO_AUX_CTRL_STATUS_REG           0x18
#define MDIO_FULL_DUPLEX_IND                0x1
#define MDIO_SPEED                          0x2
#define MDIO_PHYS_ADDR                      0x0

/* Network flash constants */
#define NET_FLASH_TIME                  (HZ/50) /* 20 ms */
#define NET_FLASH_PAUSE                (HZ/100) /* 10 ms */
#define NET_LINK_UP_CHECK_INTERVAL       (2*HZ) /* 2 s   */
#define NET_DUPLEX_CHECK_INTERVAL        (2*HZ) /* 2 s   */

#define NO_NETWORK_ACTIVITY 0
#define NETWORK_ACTIVITY    1

#define RX_DESC_BUF_SIZE   256
#define NBR_OF_RX_DESC     (RX_BUF_SIZE / \
                            RX_DESC_BUF_SIZE)

#define GET_BIT(bit,val)   (((val) >> (bit)) & 0x01)

/* Define some macros to access ETRAX 100 registers */
#define SETF(var, reg, field, val) var = (var & ~IO_MASK(##reg##, field)) | \
					  IO_FIELD(##reg##, field, val)
#define SETS(var, reg, field, val) var = (var & ~IO_MASK(##reg##, field)) | \
					  IO_STATE(##reg##, field, val)

static etrax_dma_descr *myNextRxDesc;  /* Points to the next descriptor to
                                          to be processed */
static etrax_dma_descr *myLastRxDesc;  /* The last processed descriptor */
static etrax_dma_descr *myPrevRxDesc;  /* The descriptor right before myNextRxDesc */

static unsigned char RxBuf[RX_BUF_SIZE];

static etrax_dma_descr RxDescList[NBR_OF_RX_DESC] __attribute__ ((aligned(4)));
static etrax_dma_descr TxDesc __attribute__ ((aligned(4)));

static struct sk_buff *tx_skb;

static unsigned int network_rec_config_shadow = 0;

/* Network speed indication. */
static struct timer_list speed_timer;
static struct timer_list clear_led_timer;
static int current_speed; /* Speed read from tranceiver */
static int current_speed_selection; /* Speed selected by user */
static int led_next_time;
static int led_active;

/* Duplex */
static struct timer_list duplex_timer;
static int full_duplex;
static enum duplex current_duplex;

/* Index to functions, as function prototypes. */

static int etrax_ethernet_init(struct net_device *dev);

static int e100_open(struct net_device *dev);
static int e100_set_mac_address(struct net_device *dev, void *addr);
static int e100_send_packet(struct sk_buff *skb, struct net_device *dev);
static void e100rx_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void e100tx_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void e100nw_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void e100_rx(struct net_device *dev);
static int e100_close(struct net_device *dev);
static int e100_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static void e100_tx_timeout(struct net_device *dev);
static struct net_device_stats *e100_get_stats(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);
static void e100_hardware_send_packet(char *buf, int length);
static void update_rx_stats(struct net_device_stats *);
static void update_tx_stats(struct net_device_stats *);

static void e100_check_speed(unsigned long dummy);
static void e100_set_speed(unsigned long speed);
static void e100_check_duplex(unsigned long dummy);
static void e100_set_duplex(enum duplex);
static void e100_negotiate(void);

static unsigned short e100_get_mdio_reg(unsigned char reg_num);
static void e100_send_mdio_cmd(unsigned short cmd, int write_cmd);
static void e100_send_mdio_bit(unsigned char bit);
static unsigned char e100_receive_mdio_bit(void);
static void e100_reset_tranceiver(void);

static void e100_clear_network_leds(unsigned long dummy);
static void e100_set_network_leds(int active);

#define tx_done(dev) (*R_DMA_CH0_CMD == 0)

/*
 * Check for a network adaptor of this type, and return '0' if one exists.
 * If dev->base_addr == 0, probe all likely locations.
 * If dev->base_addr == 1, always return failure.
 * If dev->base_addr == 2, allocate space for the device and return success
 * (detachable devices only).
 */

static int __init
etrax_ethernet_init(struct net_device *dev)
{
	int i;
	int anOffset = 0;

	printk("ETRAX 100LX 10/100MBit ethernet v2.0 (c) 2000-2001 Axis Communications AB\n");

	dev->base_addr = (unsigned int)R_NETWORK_SA_0; /* just to have something to show */

	printk("%s initialized\n", dev->name);

	/* make Linux aware of the new hardware  */

	if (!dev) {
		printk(KERN_WARNING "%s: dev == NULL. Should this happen?\n",
		       cardname);
		dev = init_etherdev(dev, sizeof(struct net_local));
		if (!dev)
			panic("init_etherdev failed\n");
	}

	/* setup generic handlers and stuff in the dev struct */

	ether_setup(dev);

	/* make room for the local structure containing stats etc */

	dev->priv = kmalloc(sizeof(struct net_local), GFP_KERNEL);
	if (dev->priv == NULL)
		return -ENOMEM;
	memset(dev->priv, 0, sizeof(struct net_local));

	/* now setup our etrax specific stuff */

	dev->irq = NETWORK_DMA_RX_IRQ_NBR; /* we really use DMATX as well... */
	dev->dma = NETWORK_RX_DMA_NBR;

	/* fill in our handlers so the network layer can talk to us in the future */

	dev->open               = e100_open;
	dev->hard_start_xmit    = e100_send_packet;
	dev->stop               = e100_close;
	dev->get_stats          = e100_get_stats;
	dev->set_multicast_list = set_multicast_list;
	dev->set_mac_address    = e100_set_mac_address;
	dev->do_ioctl           = e100_ioctl;
	dev->tx_timeout         = e100_tx_timeout;

	/* set the default MAC address */

	e100_set_mac_address(dev, &default_mac);

	/* Initialise the list of Etrax DMA-descriptors */

	/* Initialise receive descriptors */

	for (i = 0; i < (NBR_OF_RX_DESC - 1); i++) {
		RxDescList[i].ctrl   = 0;
		RxDescList[i].sw_len = RX_DESC_BUF_SIZE;
		RxDescList[i].next   = virt_to_phys(&RxDescList[i + 1]);
		RxDescList[i].buf    = virt_to_phys(RxBuf + anOffset);
		RxDescList[i].status = 0;
		RxDescList[i].hw_len = 0;
		anOffset += RX_DESC_BUF_SIZE;
	}

	RxDescList[i].ctrl   = d_eol;
	RxDescList[i].sw_len = RX_DESC_BUF_SIZE;
	RxDescList[i].next   = virt_to_phys(&RxDescList[0]);
	RxDescList[i].buf    = virt_to_phys(RxBuf + anOffset);
	RxDescList[i].status = 0;
	RxDescList[i].hw_len = 0;

	/* Initialise initial pointers */

	myNextRxDesc = &RxDescList[0];
	myLastRxDesc = &RxDescList[NBR_OF_RX_DESC - 1];
	myPrevRxDesc = &RxDescList[NBR_OF_RX_DESC - 1];

	/* Initialize speed indicator stuff. */

	current_speed = 10;
	current_speed_selection = 0; /* Auto */
	speed_timer.expires = jiffies + NET_LINK_UP_CHECK_INTERVAL;
	speed_timer.function = e100_check_speed;
	add_timer(&speed_timer);
        
	clear_led_timer.function = e100_clear_network_leds;
        
	full_duplex = 0;
	current_duplex = autoneg;
	duplex_timer.expires = jiffies + NET_DUPLEX_CHECK_INTERVAL;		
	duplex_timer.function = e100_check_duplex;
	add_timer(&duplex_timer);

	return 0;
}

/* set MAC address of the interface. called from the core after a
 * SIOCSIFADDR ioctl, and from the bootup above.
 */

static int
e100_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	int i;

	/* remember it */

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	/* Write it to the hardware.
	 * Note the way the address is wrapped:
	 * *R_NETWORK_SA_0 = a0_0 | (a0_1 << 8) | (a0_2 << 16) | (a0_3 << 24);
	 * *R_NETWORK_SA_1 = a0_4 | (a0_5 << 8);
	 */
	
	*R_NETWORK_SA_0 = dev->dev_addr[0] | (dev->dev_addr[1] << 8) |
		(dev->dev_addr[2] << 16) | (dev->dev_addr[3] << 24);
	*R_NETWORK_SA_1 = dev->dev_addr[4] | (dev->dev_addr[5] << 8);
	*R_NETWORK_SA_2 = 0;

	/* show it in the log as well */

	printk("%s: changed MAC to ", dev->name);

	for (i = 0; i < 5; i++)
		printk("%02X:", dev->dev_addr[i]);

	printk("%02X\n", dev->dev_addr[i]);

	return 0;
}

/*
 * Open/initialize the board. This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */

static int
e100_open(struct net_device *dev)
{
	unsigned long flags;

	/* disable the ethernet interface while we configure it */

	*R_NETWORK_GEN_CONFIG =
		IO_STATE(R_NETWORK_GEN_CONFIG, phy,    mii_clk) |
		IO_STATE(R_NETWORK_GEN_CONFIG, enable, off);

	/* enable the MDIO output pin */

	*R_NETWORK_MGM_CTRL = IO_STATE(R_NETWORK_MGM_CTRL, mdoe, enable);

	*R_IRQ_MASK0_CLR =
		IO_STATE(R_IRQ_MASK0_CLR, overrun, clr) |
		IO_STATE(R_IRQ_MASK0_CLR, underrun, clr) |
		IO_STATE(R_IRQ_MASK0_CLR, excessive_col, clr);
	
	/* clear dma0 and 1 eop and descr irq masks */
	*R_IRQ_MASK2_CLR =
		IO_STATE(R_IRQ_MASK2_CLR, dma0_descr, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma0_eop, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma1_descr, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma1_eop, clr);

	/* Reset and wait for the DMA channels */

	RESET_DMA(NETWORK_TX_DMA_NBR);
	RESET_DMA(NETWORK_RX_DMA_NBR);
	WAIT_DMA(NETWORK_TX_DMA_NBR);
	WAIT_DMA(NETWORK_RX_DMA_NBR);

	/* Initialise the etrax network controller */

	/* allocate the irq corresponding to the receiving DMA */

	if (request_irq(NETWORK_DMA_RX_IRQ_NBR, e100rx_interrupt, 0,
			cardname, (void *)dev)) {
		goto grace_exit0;
	}

	/* allocate the irq corresponding to the transmitting DMA */

	if (request_irq(NETWORK_DMA_TX_IRQ_NBR, e100tx_interrupt, 0,
			cardname, (void *)dev)) {
		goto grace_exit1;
	}

	/* allocate the irq corresponding to the network errors etc */

	if (request_irq(NETWORK_STATUS_IRQ_NBR, e100nw_interrupt, 0,
			cardname, (void *)dev)) {
		goto grace_exit2;
	}

	/*
	 * Always allocate the DMA channels after the IRQ,
	 * and clean up on failure.
	 */

	if (request_dma(NETWORK_TX_DMA_NBR, cardname)) {
		goto grace_exit3;
	}

	if (request_dma(NETWORK_RX_DMA_NBR, cardname)) {
		goto grace_exit4;
	}

	/* give the HW an idea of what MAC address we want */

	*R_NETWORK_SA_0 = dev->dev_addr[0] | (dev->dev_addr[1] << 8) |
		(dev->dev_addr[2] << 16) | (dev->dev_addr[3] << 24);
	*R_NETWORK_SA_1 = dev->dev_addr[4] | (dev->dev_addr[5] << 8);
	*R_NETWORK_SA_2 = 0;

#if 0
	/* use promiscuous mode for testing */
	*R_NETWORK_GA_0 = 0xffffffff;
	*R_NETWORK_GA_1 = 0xffffffff;

	*R_NETWORK_REC_CONFIG = 0xd; /* broadcast rec, individ. rec, ma0 enabled */
#else
	SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, broadcast, receive);
	SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, ma0, enable);
	SETF(network_rec_config_shadow, R_NETWORK_REC_CONFIG, duplex, full_duplex);
	*R_NETWORK_REC_CONFIG = network_rec_config_shadow;
#endif

	*R_NETWORK_GEN_CONFIG =
		IO_STATE(R_NETWORK_GEN_CONFIG, phy,    mii_clk) |
		IO_STATE(R_NETWORK_GEN_CONFIG, enable, on);

        *R_NETWORK_TR_CTRL = 
                IO_STATE(R_NETWORK_TR_CTRL, clr_error, clr) |
                IO_STATE(R_NETWORK_TR_CTRL, delay, none) |
                IO_STATE(R_NETWORK_TR_CTRL, cancel, dont) |
                IO_STATE(R_NETWORK_TR_CTRL, cd, enable) |
                IO_STATE(R_NETWORK_TR_CTRL, retry, enable) |
                IO_STATE(R_NETWORK_TR_CTRL, pad, enable) |
                IO_STATE(R_NETWORK_TR_CTRL, crc, enable);

	save_flags(flags);
	cli();

	/* enable the irq's for ethernet DMA */

	*R_IRQ_MASK2_SET =
		IO_STATE(R_IRQ_MASK2_SET, dma0_eop, set) |
		IO_STATE(R_IRQ_MASK2_SET, dma1_eop, set);

	*R_IRQ_MASK0_SET =
		IO_STATE(R_IRQ_MASK0_SET, overrun,       set) |
		IO_STATE(R_IRQ_MASK0_SET, underrun,      set) |
		IO_STATE(R_IRQ_MASK0_SET, excessive_col, set);

	tx_skb = 0;

	/* make sure the irqs are cleared */

	*R_DMA_CH0_CLR_INTR = IO_STATE(R_DMA_CH0_CLR_INTR, clr_eop, do);
	*R_DMA_CH1_CLR_INTR = IO_STATE(R_DMA_CH1_CLR_INTR, clr_eop, do);

	/* make sure the rec and transmit error counters are cleared */

	(void)*R_REC_COUNTERS;  /* dummy read */
	(void)*R_TR_COUNTERS;   /* dummy read */

	/* start the receiving DMA channel so we can receive packets from now on */

	*R_DMA_CH1_FIRST = virt_to_phys(myNextRxDesc);
	*R_DMA_CH1_CMD = IO_STATE(R_DMA_CH1_CMD, cmd, start);

	restore_flags(flags);
	
	/* We are now ready to accept transmit requeusts from
	 * the queueing layer of the networking.
	 */
	netif_start_queue(dev);

	return 0;

grace_exit4:
	free_dma(NETWORK_TX_DMA_NBR);
grace_exit3:
	free_irq(NETWORK_STATUS_IRQ_NBR, (void *)dev);
grace_exit2:
	free_irq(NETWORK_DMA_TX_IRQ_NBR, (void *)dev);
grace_exit1:
	free_irq(NETWORK_DMA_RX_IRQ_NBR, (void *)dev);
grace_exit0:
	return -EAGAIN;
}


static void
e100_check_speed(unsigned long dummy)
{
	unsigned long data;
	int old_speed = current_speed;

	data = e100_get_mdio_reg(MDIO_BASE_STATUS_REG);
	if (!(data & MDIO_LINK_UP_MASK)) {
		current_speed = 0;
	} else {
		data = e100_get_mdio_reg(MDIO_AUX_CTRL_STATUS_REG);
		current_speed = (data & MDIO_SPEED ? 100 : 10);
	}
	
	if (old_speed != current_speed)
		e100_set_network_leds(NO_NETWORK_ACTIVITY);

	/* Reinitialize the timer. */
	speed_timer.expires = jiffies + NET_LINK_UP_CHECK_INTERVAL;
	add_timer(&speed_timer);
}

static void
e100_negotiate(void)
{
	unsigned short cmd;
	unsigned short data = e100_get_mdio_reg(MDIO_ADVERTISMENT_REG);
	int bitCounter;

	/* Discard old speed and duplex settings */
	data &= ~(MDIO_ADVERT_100_HD | MDIO_ADVERT_100_FD | 
	          MDIO_ADVERT_10_FD | MDIO_ADVERT_10_HD);
  
	switch (current_speed_selection) {
		case 10 :
			if (current_duplex == full)
				data |= MDIO_ADVERT_10_FD;
			else if (current_duplex == half)
				data |= MDIO_ADVERT_10_HD;
			else
				data |= MDIO_ADVERT_10_HD |  MDIO_ADVERT_10_FD;
			break;

		case 100 :
			 if (current_duplex == full)
				data |= MDIO_ADVERT_100_FD;
			else if (current_duplex == half)
				data |= MDIO_ADVERT_100_HD;
			else
				data |= MDIO_ADVERT_100_HD |  MDIO_ADVERT_100_FD;
			break;

		case 0 : /* Auto */
			 if (current_duplex == full)
				data |= MDIO_ADVERT_100_FD | MDIO_ADVERT_10_FD;
			else if (current_duplex == half)
				data |= MDIO_ADVERT_100_HD | MDIO_ADVERT_10_HD;
			else
				data |= MDIO_ADVERT_100_HD | MDIO_ADVERT_100_FD | MDIO_ADVERT_10_FD | MDIO_ADVERT_10_HD;
			break;

		default : /* assume autoneg speed and duplex */
			data |= MDIO_ADVERT_100_HD | MDIO_ADVERT_100_FD | 
			        MDIO_ADVERT_10_FD | MDIO_ADVERT_10_HD;
	}

	cmd = (MDIO_START << 14) | (MDIO_WRITE << 12) | (MDIO_PHYS_ADDR << 7) |
	      (MDIO_ADVERTISMENT_REG<< 2);

	e100_send_mdio_cmd(cmd, 1);

	/* Data... */
	for (bitCounter=15; bitCounter>=0 ; bitCounter--) {
		e100_send_mdio_bit(GET_BIT(bitCounter, data));
	}

	/* Renegotiate with link partner */
	data = e100_get_mdio_reg(MDIO_BASE_CONTROL_REG);
	data |= MDIO_BC_NEGOTIATE;

	cmd = (MDIO_START << 14) | (MDIO_WRITE << 12) | (MDIO_PHYS_ADDR << 7) |
	      (MDIO_BASE_CONTROL_REG<< 2);

	e100_send_mdio_cmd(cmd, 1);

	/* Data... */
	for (bitCounter=15; bitCounter>=0 ; bitCounter--) {
		e100_send_mdio_bit(GET_BIT(bitCounter, data));
	}  
}

static void
e100_set_speed(unsigned long speed)
{
	current_speed_selection = speed;
	e100_negotiate();
}

static void
e100_check_duplex(unsigned long dummy)
{
	unsigned long data;

	data = e100_get_mdio_reg(MDIO_AUX_CTRL_STATUS_REG);
        
	if (data & MDIO_FULL_DUPLEX_IND) {
		if (!full_duplex) { /* Duplex changed to full? */
			full_duplex = 1;
			SETF(network_rec_config_shadow, R_NETWORK_REC_CONFIG, duplex, full_duplex);
			*R_NETWORK_REC_CONFIG = network_rec_config_shadow;
		}
	} else { /* half */
		if (full_duplex) { /* Duplex changed to half? */
			full_duplex = 0;
			SETF(network_rec_config_shadow, R_NETWORK_REC_CONFIG, duplex, full_duplex);
			*R_NETWORK_REC_CONFIG = network_rec_config_shadow;
		}
	}

	/* Reinitialize the timer. */
	duplex_timer.expires = jiffies + NET_DUPLEX_CHECK_INTERVAL;
	add_timer(&duplex_timer);
}

static void 
e100_set_duplex(enum duplex new_duplex)
{
	current_duplex = new_duplex;
	e100_negotiate();
}


static unsigned short
e100_get_mdio_reg(unsigned char reg_num)
{
	unsigned short cmd;    /* Data to be sent on MDIO port */
	unsigned short data;   /* Data read from MDIO */
	int bitCounter;
	
	/* Start of frame, OP Code, Physical Address, Register Address */
	cmd = (MDIO_START << 14) | (MDIO_READ << 12) | (MDIO_PHYS_ADDR << 7) |
		(reg_num << 2);
	
	e100_send_mdio_cmd(cmd, 0);
	
	data = 0;
	
	/* Data... */
	for (bitCounter=15; bitCounter>=0 ; bitCounter--) {
		data |= (e100_receive_mdio_bit() << bitCounter);
	}

	return data;
}

static void
e100_send_mdio_cmd(unsigned short cmd, int write_cmd)
{
	int bitCounter;
	unsigned char data = 0x2;
	
	/* Preamble */
	for (bitCounter = 31; bitCounter>= 0; bitCounter--)
		e100_send_mdio_bit(GET_BIT(bitCounter, MDIO_PREAMBLE));

	for (bitCounter = 15; bitCounter >= 2; bitCounter--)
		e100_send_mdio_bit(GET_BIT(bitCounter, cmd));

	/* Turnaround */
	for (bitCounter = 1; bitCounter >= 0 ; bitCounter--)
		if (write_cmd)
			e100_send_mdio_bit(GET_BIT(bitCounter, data));
		else
			e100_receive_mdio_bit();
}

static void
e100_send_mdio_bit(unsigned char bit)
{
	*R_NETWORK_MGM_CTRL =
		IO_STATE(R_NETWORK_MGM_CTRL, mdoe, enable) |
		IO_FIELD(R_NETWORK_MGM_CTRL, mdio, bit);
	udelay(1);
	*R_NETWORK_MGM_CTRL =
		IO_STATE(R_NETWORK_MGM_CTRL, mdoe, enable) |
		IO_MASK(R_NETWORK_MGM_CTRL, mdck) |
		IO_FIELD(R_NETWORK_MGM_CTRL, mdio, bit);
	udelay(1);
}

static unsigned char
e100_receive_mdio_bit()
{
	unsigned char bit;
	*R_NETWORK_MGM_CTRL = 0;
	bit = IO_EXTRACT(R_NETWORK_STAT, mdio, *R_NETWORK_STAT);
	udelay(1);
	*R_NETWORK_MGM_CTRL = IO_MASK(R_NETWORK_MGM_CTRL, mdck);
	udelay(1);
	return bit;
}

static void 
e100_reset_tranceiver(void)
{
	unsigned short cmd;
	unsigned short data;
	int bitCounter;

	data = e100_get_mdio_reg(MDIO_BASE_CONTROL_REG);

	cmd = (MDIO_START << 14) | (MDIO_WRITE << 12) | (MDIO_PHYS_ADDR << 7) | (MDIO_BASE_CONTROL_REG << 2);

	e100_send_mdio_cmd(cmd, 1);
	
	data |= 0x8000;
	
	for (bitCounter = 15; bitCounter >= 0 ; bitCounter--) {
		e100_send_mdio_bit(GET_BIT(bitCounter, data));
	}
}

/* Called by upper layers if they decide it took too long to complete
 * sending a packet - we need to reset and stuff.
 */

static void
e100_tx_timeout(struct net_device *dev)
{
	struct net_local *np = (struct net_local *)dev->priv;

	printk(KERN_WARNING "%s: transmit timed out, %s?\n", dev->name,
	       tx_done(dev) ? "IRQ problem" : "network cable problem");
	
	/* remember we got an error */
	
	np->stats.tx_errors++; 
	
	/* reset the TX DMA in case it has hung on something */
	
	RESET_DMA(NETWORK_TX_DMA_NBR);
	WAIT_DMA(NETWORK_TX_DMA_NBR);
	
	/* Reset the tranceiver. */
	
	e100_reset_tranceiver();
	
	/* and get rid of the packet that never got an interrupt */
	
	dev_kfree_skb(tx_skb);
	tx_skb = 0;
	
	/* tell the upper layers we're ok again */
	
	netif_wake_queue(dev);
}


/* This will only be invoked if the driver is _not_ in XOFF state.
 * What this means is that we need not check it, and that this
 * invariant will hold if we make sure that the netif_*_queue()
 * calls are done at the proper times.
 */

static int
e100_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *np = (struct net_local *)dev->priv;
	int length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	unsigned char *buf = skb->data;
	
#ifdef ETHDEBUG
	printk("send packet len %d\n", length);
#endif
	spin_lock_irq(&np->lock);  /* protect from tx_interrupt */

	tx_skb = skb; /* remember it so we can free it in the tx irq handler later */
	dev->trans_start = jiffies;
	
	e100_hardware_send_packet(buf, length);

	/* this simple TX driver has only one send-descriptor so we're full
	 * directly. If this had a send-ring instead, we would only do this if
	 * the ring got full.
	 */

	netif_stop_queue(dev);

	spin_unlock_irq(&np->lock);

	return 0;
}

/*
 * The typical workload of the driver:
 *   Handle the network interface interrupts.
 */

static void
e100rx_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct net_device *dev = (struct net_device *)dev_id;
	unsigned long irqbits = *R_IRQ_MASK2_RD;
 
	if (irqbits & IO_STATE(R_IRQ_MASK2_RD, dma1_eop, active)) {
		/* acknowledge the eop interrupt */

		*R_DMA_CH1_CLR_INTR = IO_STATE(R_DMA_CH1_CLR_INTR, clr_eop, do);

		/* check if one or more complete packets were indeed received */

		while (*R_DMA_CH1_FIRST != virt_to_phys(myNextRxDesc)) {
			/* Take out the buffer and give it to the OS, then
			 * allocate a new buffer to put a packet in.
			 */
			e100_rx(dev);
			((struct net_local *)dev->priv)->stats.rx_packets++;
			/* restart/continue on the channel, for safety */
			*R_DMA_CH1_CMD = IO_STATE(R_DMA_CH1_CMD, cmd, restart);
			/* clear dma channel 1 eop/descr irq bits */
			*R_DMA_CH1_CLR_INTR =
				IO_STATE(R_DMA_CH1_CLR_INTR, clr_eop, do) |
				IO_STATE(R_DMA_CH1_CLR_INTR, clr_descr, do);
			
			/* now, we might have gotten another packet
			   so we have to loop back and check if so */
		}
	}
}

/* the transmit dma channel interrupt
 *
 * this is supposed to free the skbuff which was pending during transmission,
 * and inform the kernel that we can send one more buffer
 */

static void
e100tx_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct net_device *dev = (struct net_device *)dev_id;
	unsigned long irqbits = *R_IRQ_MASK2_RD;
	struct net_local *np = (struct net_local *)dev->priv;

	/* check for a dma0_eop interrupt */
	if (irqbits & IO_STATE(R_IRQ_MASK2_RD, dma0_eop, active)) { 
		/* This protects us from concurrent execution of
		 * our dev->hard_start_xmit function above.
		 */

		spin_lock(&np->lock);
		
		/* acknowledge the eop interrupt */

		*R_DMA_CH0_CLR_INTR = IO_STATE(R_DMA_CH0_CLR_INTR, clr_eop, do);

		if (*R_DMA_CH0_FIRST == 0 && tx_skb) {
			np->stats.tx_bytes += tx_skb->len;
			np->stats.tx_packets++;
			/* dma is ready with the transmission of the data in tx_skb, so now
			   we can release the skb memory */
			dev_kfree_skb_irq(tx_skb);
			tx_skb = 0;
			netif_wake_queue(dev);
		} else {
			printk(KERN_WARNING "%s: tx weird interrupt\n",
			       cardname);
		}

		spin_unlock(&np->lock);
	}
}

static void
e100nw_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct net_local *np = (struct net_local *)dev->priv;
	unsigned long irqbits = *R_IRQ_MASK0_RD;

	/* check for underrun irq */
	if (irqbits & IO_STATE(R_IRQ_MASK0_RD, underrun, active)) { 
		*R_NETWORK_TR_CTRL = IO_STATE(R_NETWORK_TR_CTRL, clr_error, clr);
		np->stats.tx_errors++;
		D(printk("ethernet receiver underrun!\n"));
	}

	/* check for overrun irq */
	if (irqbits & IO_STATE(R_IRQ_MASK0_RD, overrun, active)) { 
		update_rx_stats(&np->stats); /* this will ack the irq */
		D(printk("ethernet receiver overrun!\n"));
	}
	/* check for excessive collision irq */
	if (irqbits & IO_STATE(R_IRQ_MASK0_RD, excessive_col, active)) { 
		*R_NETWORK_TR_CTRL = IO_STATE(R_NETWORK_TR_CTRL, clr_error, clr);
		np->stats.tx_errors++;
		D(printk("ethernet excessive collisions!\n"));
	}

}

/* We have a good packet(s), get it/them out of the buffers. */
static void
e100_rx(struct net_device *dev)
{
	struct sk_buff *skb;
	int length = 0;
	struct net_local *np = (struct net_local *)dev->priv;
	struct etrax_dma_descr *mySaveRxDesc = myNextRxDesc;
	unsigned char *skb_data_ptr;
#ifdef ETHDEBUG
	int i;
#endif

	if (!led_active && time_after(jiffies, led_next_time)) {
		/* light the network leds depending on the current speed. */
		e100_set_network_leds(NETWORK_ACTIVITY);

		/* Set the earliest time we may clear the LED */
		led_next_time = jiffies + NET_FLASH_TIME;
		led_active = 1;
		mod_timer(&clear_led_timer, jiffies + HZ/10);
	}

	/* If the packet is broken down in many small packages then merge
	 * count how much space we will need to alloc with skb_alloc() for
	 * it to fit.
	 */

	while (!(myNextRxDesc->status & d_eop)) {
		length += myNextRxDesc->sw_len; /* use sw_len for the first descs */
		myNextRxDesc->status = 0;
		myNextRxDesc = phys_to_virt(myNextRxDesc->next);
	}

	length += myNextRxDesc->hw_len; /* use hw_len for the last descr */
	((struct net_local *)dev->priv)->stats.rx_bytes += length;

#ifdef ETHDEBUG
	printk("Got a packet of length %d:\n", length);
	/* dump the first bytes in the packet */
	skb_data_ptr = (unsigned char *)phys_to_virt(mySaveRxDesc->buf);
	for (i = 0; i < 8; i++) {
		printk("%d: %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x\n", i * 8,
		       skb_data_ptr[0],skb_data_ptr[1],skb_data_ptr[2],skb_data_ptr[3],
		       skb_data_ptr[4],skb_data_ptr[5],skb_data_ptr[6],skb_data_ptr[7]);
		skb_data_ptr += 8;
	}
#endif

	skb = dev_alloc_skb(length - ETHER_HEAD_LEN);
	if (!skb) {
		np->stats.rx_errors++;
		printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n",
		       dev->name);
		return;
	}

	skb_put(skb, length - ETHER_HEAD_LEN);        /* allocate room for the packet body */
	skb_data_ptr = skb_push(skb, ETHER_HEAD_LEN); /* allocate room for the header */

#ifdef ETHDEBUG
	printk("head = 0x%x, data = 0x%x, tail = 0x%x, end = 0x%x\n",
	       skb->head, skb->data, skb->tail, skb->end);
	printk("copying packet to 0x%x.\n", skb_data_ptr);
#endif

	/* this loop can be made using max two memcpy's if optimized */

	while (mySaveRxDesc != myNextRxDesc) {
		memcpy(skb_data_ptr, phys_to_virt(mySaveRxDesc->buf),
		       mySaveRxDesc->sw_len);
		skb_data_ptr += mySaveRxDesc->sw_len;
		mySaveRxDesc = phys_to_virt(mySaveRxDesc->next);
	}

	memcpy(skb_data_ptr, phys_to_virt(mySaveRxDesc->buf),
	       mySaveRxDesc->hw_len);

	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);

	/* Send the packet to the upper layers */

	netif_rx(skb);

	/* Prepare for next packet */

	myNextRxDesc->status = 0;
	myPrevRxDesc = myNextRxDesc;
	myNextRxDesc = phys_to_virt(myNextRxDesc->next);

	myPrevRxDesc->ctrl |= d_eol;
	myLastRxDesc->ctrl &= ~d_eol;
	myLastRxDesc = myPrevRxDesc;

	return;
}

/* The inverse routine to net_open(). */
static int
e100_close(struct net_device *dev)
{
	struct net_local *np = (struct net_local *)dev->priv;

	printk("Closing %s.\n", dev->name);

	netif_stop_queue(dev);

	*R_NETWORK_GEN_CONFIG =
		IO_STATE(R_NETWORK_GEN_CONFIG, phy,    mii_clk) |
		IO_STATE(R_NETWORK_GEN_CONFIG, enable, off);
	
	*R_IRQ_MASK0_CLR =
		IO_STATE(R_IRQ_MASK0_CLR, overrun, clr) |
		IO_STATE(R_IRQ_MASK0_CLR, underrun, clr) |
		IO_STATE(R_IRQ_MASK0_CLR, excessive_col, clr);
	
	*R_IRQ_MASK2_CLR =
		IO_STATE(R_IRQ_MASK2_CLR, dma0_descr, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma0_eop, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma1_descr, clr) |
		IO_STATE(R_IRQ_MASK2_CLR, dma1_eop, clr);

	/* Stop the receiver and the transmitter */

	RESET_DMA(NETWORK_TX_DMA_NBR);
	RESET_DMA(NETWORK_RX_DMA_NBR);

	/* Flush the Tx and disable Rx here. */

	free_irq(NETWORK_DMA_RX_IRQ_NBR, (void *)dev);
	free_irq(NETWORK_DMA_TX_IRQ_NBR, (void *)dev);
	free_irq(NETWORK_STATUS_IRQ_NBR, (void *)dev);

	free_dma(NETWORK_TX_DMA_NBR);
	free_dma(NETWORK_RX_DMA_NBR);

	/* Update the statistics here. */

	update_rx_stats(&np->stats);
	update_tx_stats(&np->stats);

	return 0;
}

static int
e100_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	/* Maybe default should return -EINVAL instead? */
	switch (cmd) {
		case SET_ETH_SPEED_10:                  /* 10 Mbps */
			e100_set_speed(10);
			break;
		case SET_ETH_SPEED_100:                /* 100 Mbps */
			e100_set_speed(100);
			break;
		case SET_ETH_SPEED_AUTO:              /* Auto negotiate speed */
			e100_set_speed(0);
			break;
		case SET_ETH_DUPLEX_HALF:              /* Hhalf duplex. */
			e100_set_duplex(half);
			break;
		case SET_ETH_DUPLEX_FULL:              /* Full duplex. */
			e100_set_duplex(full);
			break;
		case SET_ETH_DUPLEX_AUTO:             /* Autonegotiate duplex*/
			e100_set_duplex(autoneg);
			break;
		default: /* Auto neg */
			e100_set_speed(0);
			e100_set_duplex(autoneg);
			break;
	}
	return 0;
}

static void
update_rx_stats(struct net_device_stats *es)
{
	unsigned long r = *R_REC_COUNTERS;
	/* update stats relevant to reception errors */
	es->rx_fifo_errors += IO_EXTRACT(R_REC_COUNTERS, congestion, r);
	es->rx_crc_errors += IO_EXTRACT(R_REC_COUNTERS, crc_error, r);
	es->rx_frame_errors += IO_EXTRACT(R_REC_COUNTERS, alignment_error, r);
	es->rx_length_errors += IO_EXTRACT(R_REC_COUNTERS, oversize, r);
}

static void
update_tx_stats(struct net_device_stats *es)
{
	unsigned long r = *R_TR_COUNTERS;
	/* update stats relevant to transmission errors */
	es->collisions +=
		IO_EXTRACT(R_TR_COUNTERS, single_col, r) +
		IO_EXTRACT(R_TR_COUNTERS, multiple_col, r);
	es->tx_errors += IO_EXTRACT(R_TR_COUNTERS, deferred, r);
}

/*
 * Get the current statistics.
 * This may be called with the card open or closed.
 */
static struct net_device_stats *
e100_get_stats(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	update_rx_stats(&lp->stats);
	update_tx_stats(&lp->stats);

	return &lp->stats;
}

/*
 * Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, clear multicast list
 * num_addrs > 0	Multicast mode, receive normal and MC packets,
 *			and do best-effort filtering.
 */
static void
set_multicast_list(struct net_device *dev)
{
	int num_addr = dev->mc_count;
	unsigned long int lo_bits;
	unsigned long int hi_bits;
	if (dev->flags & IFF_PROMISC)
	{
		/* promiscuous mode */
		lo_bits = 0xfffffffful;
		hi_bits = 0xfffffffful;

		/* Enable individual receive */
		SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, individual, receive);
		*R_NETWORK_REC_CONFIG = network_rec_config_shadow;
	} else if (dev->flags & IFF_ALLMULTI) {
		/* enable all multicasts */
		lo_bits = 0xfffffffful;
		hi_bits = 0xfffffffful;

		/* Disable individual receive */
		SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, individual, discard);
		*R_NETWORK_REC_CONFIG =  network_rec_config_shadow;
	} else if (num_addr == 0) {
		/* Normal, clear the mc list */
		lo_bits = 0x00000000ul;
		hi_bits = 0x00000000ul;

		/* Disable individual receive */
		SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, individual, discard);
		*R_NETWORK_REC_CONFIG =  network_rec_config_shadow;
	} else {
		/* MC mode, receive normal and MC packets */
		char hash_ix;
		struct dev_mc_list *dmi = dev->mc_list;
		int i;
		char *baddr;
		lo_bits = 0x00000000ul;
		hi_bits = 0x00000000ul;
		for (i=0; i<num_addr; i++) {
			/* Calculate the hash index for the GA registers */
			
			hash_ix = 0;
			baddr = dmi->dmi_addr;
			hash_ix ^= (*baddr) & 0x3f;
			hash_ix ^= ((*baddr) >> 6) & 0x03;
			++baddr;
			hash_ix ^= ((*baddr) << 2) & 0x03c;
			hash_ix ^= ((*baddr) >> 4) & 0xf;
			++baddr;
			hash_ix ^= ((*baddr) << 4) & 0x30;
			hash_ix ^= ((*baddr) >> 2) & 0x3f;
			++baddr;
			hash_ix ^= (*baddr) & 0x3f;
			hash_ix ^= ((*baddr) >> 6) & 0x03;
			++baddr;
			hash_ix ^= ((*baddr) << 2) & 0x03c;
			hash_ix ^= ((*baddr) >> 4) & 0xf;
			++baddr;
			hash_ix ^= ((*baddr) << 4) & 0x30;
			hash_ix ^= ((*baddr) >> 2) & 0x3f;
			
			hash_ix &= 0x3f;
			
			if (hash_ix > 32) {
				hi_bits |= (1 << (hash_ix-32));
			}
			else {
				lo_bits |= (1 << hash_ix);
			}
			dmi = dmi->next;
		}
		/* Disable individual receive */
		SETS(network_rec_config_shadow, R_NETWORK_REC_CONFIG, individual, discard);
		*R_NETWORK_REC_CONFIG = network_rec_config_shadow;
	}
	*R_NETWORK_GA_0 = lo_bits;
	*R_NETWORK_GA_1 = hi_bits;
}

void
e100_hardware_send_packet(char *buf, int length)
{
	D(printk("e100 send pack, buf 0x%x len %d\n", buf, length));

	if (!led_active && time_after(jiffies, led_next_time)) {
		/* light the network leds depending on the current speed. */
		e100_set_network_leds(NETWORK_ACTIVITY);

		/* Set the earliest time we may clear the LED */
		led_next_time = jiffies + NET_FLASH_TIME;
		led_active = 1;
		mod_timer(&clear_led_timer, jiffies + HZ/10);
	}

	/* configure the tx dma descriptor */

	TxDesc.sw_len = length;
	TxDesc.ctrl = d_eop | d_eol | d_wait;
	TxDesc.buf = virt_to_phys(buf);

	/* setup the dma channel and start it */

	*R_DMA_CH0_FIRST = virt_to_phys(&TxDesc);
	*R_DMA_CH0_CMD = IO_STATE(R_DMA_CH0_CMD, cmd, start);
}

static void
e100_clear_network_leds(unsigned long dummy)
{
	if (led_active && jiffies > time_after(jiffies, led_next_time)) {
		e100_set_network_leds(NO_NETWORK_ACTIVITY);

		/* Set the earliest time we may set the LED */
		led_next_time = jiffies + NET_FLASH_PAUSE;
		led_active = 0;
	}
}

static void
e100_set_network_leds(int active)
{
#if defined(CONFIG_ETRAX_NETWORK_LED_ON_WHEN_LINK)
	int light_leds = (active == NO_NETWORK_ACTIVITY);
#elif defined(CONFIG_ETRAX_NETWORK_LED_ON_WHEN_ACTIVITY)
	int light_leds = (active == NETWORK_ACTIVITY);
#else
#error "Define either CONFIG_ETRAX_NETWORK_LED_ON_WHEN_LINK or CONFIG_ETRAX_NETWORK_LED_ON_WHEN_ACTIVITY"
#endif

	if (!current_speed) {
		/* Make LED red, link is down */
		LED_NETWORK_SET(LED_RED);
	}
	else if (light_leds) {
		if (current_speed == 10) {
			LED_NETWORK_SET(LED_ORANGE);
		} else {
			LED_NETWORK_SET(LED_GREEN);
		}
	}
	else {
		LED_NETWORK_SET(LED_OFF);
	}
}

static struct net_device dev_etrax_ethernet;  /* only got one */

static int
etrax_init_module(void)
{
	struct net_device *d = &dev_etrax_ethernet;

	d->init = etrax_ethernet_init;

	if (register_netdev(d) == 0)
		return 0;
	else
		return -ENODEV;
}

module_init(etrax_init_module);
