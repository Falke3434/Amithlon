/* epic100.c: A SMC 83c170 EPIC/100 Fast Ethernet driver for Linux. */
/*
	Written/copyright 1997-2001 by Donald Becker.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	This driver is for the SMC83c170/175 "EPIC" series, as used on the
	SMC EtherPower II 9432 PCI adapter, and several CardBus cards.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Information and updates available at
	http://www.scyld.com/network/epic100.html

	---------------------------------------------------------------------
	
	Linux kernel-specific changes:
	
	LK1.1.2 (jgarzik):
	* Merge becker version 1.09 (4/08/2000)

	LK1.1.3:
	* Major bugfix to 1.09 driver (Francis Romieu)
	
	LK1.1.4 (jgarzik):
	* Merge becker test version 1.09 (5/29/2000)

	LK1.1.5:
	* Fix locking (jgarzik)
	* Limit 83c175 probe to ethernet-class PCI devices (rgooch)

	LK1.1.6:
	* Merge becker version 1.11
	* Move pci_enable_device before any PCI BAR len checks

	LK1.1.7:
	* { fill me in }

	LK1.1.8:
	* ethtool driver info support (jgarzik)

	LK1.1.9:
	* ethtool media get/set support (jgarzik)

	LK1.1.10:
	* revert MII transceiver init change (jgarzik)

	LK1.1.11:
	* implement ETHTOOL_[GS]SET, _NWAY_RST, _[GS]MSGLVL, _GLINK (jgarzik)
	* replace some MII-related magic numbers with constants

	LK1.1.12:
	* fix power-up sequence

*/

#define DRV_NAME	"epic100"
#define DRV_VERSION	"1.11+LK1.1.12"
#define DRV_RELDATE	"Jan 18, 2002"


/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 32;

/* Used to pass the full-duplex flag, etc. */
#define MAX_UNITS 8		/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
static int rx_copybreak;

/* Operational parameters that are set at compile time. */

/* Keep the ring sizes a power of two for operational efficiency.
   The compiler will convert <unsigned>'%'<2^N> into a bit mask.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define TX_QUEUE_LEN	10		/* Limit ring entries actually used.  */
#define RX_RING_SIZE	32
#define TX_TOTAL_SIZE	TX_RING_SIZE*sizeof(struct epic_tx_desc)
#define RX_TOTAL_SIZE	RX_RING_SIZE*sizeof(struct epic_rx_desc)

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)

#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

/* Bytes transferred to chip before transmission starts. */
/* Initial threshold, increased on underflow, rounded down to 4 byte units. */
#define TX_FIFO_THRESH 256
#define RX_FIFO_THRESH 1		/* 0-3, 0==32, 64,96, or 3==128 bytes  */

#if !defined(__OPTIMIZE__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error You must compile this driver with "-O".
#endif

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/uaccess.h>

/* These identify the driver base version and may not be removed. */
static char version[] __devinitdata =
DRV_NAME ".c:v1.11 1/7/2001 Written by Donald Becker <becker@scyld.com>\n";
static char version2[] __devinitdata =
"  http://www.scyld.com/network/epic100.html\n";
static char version3[] __devinitdata =
"  (unofficial 2.4.x kernel port, version " DRV_VERSION ", " DRV_RELDATE ")\n";

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("SMC 83c170 EPIC series Ethernet driver");
MODULE_LICENSE("GPL");

MODULE_PARM(debug, "i");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM_DESC(debug, "EPIC/100 debug level (0-5)");
MODULE_PARM_DESC(max_interrupt_work, "EPIC/100 maximum events handled per interrupt");
MODULE_PARM_DESC(options, "EPIC/100: Bits 0-3: media type, bit 4: full duplex");
MODULE_PARM_DESC(rx_copybreak, "EPIC/100 copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(full_duplex, "EPIC/100 full duplex setting(s) (1)");

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the SMC "EPIC/100", the SMC
single-chip Ethernet controllers for PCI.  This chip is used on
the SMC EtherPower II boards.

II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS will assign the
PCI INTA signal to a (preferably otherwise unused) system IRQ line.
Note: Kernel versions earlier than 1.3.73 do not support shared PCI
interrupt lines.

III. Driver operation

IIIa. Ring buffers

IVb. References

http://www.smsc.com/main/datasheets/83c171.pdf
http://www.smsc.com/main/datasheets/83c175.pdf
http://scyld.com/expert/NWay.html
http://www.national.com/pf/DP/DP83840A.html

IVc. Errata

*/


enum pci_id_flags_bits {
        /* Set PCI command register bits before calling probe1(). */
        PCI_USES_IO=1, PCI_USES_MEM=2, PCI_USES_MASTER=4,
        /* Read and map the single following PCI BAR. */
        PCI_ADDR0=0<<4, PCI_ADDR1=1<<4, PCI_ADDR2=2<<4, PCI_ADDR3=3<<4,
        PCI_ADDR_64BITS=0x100, PCI_NO_ACPI_WAKE=0x200, PCI_NO_MIN_LATENCY=0x400,
};

enum chip_capability_flags { MII_PWRDWN=1, TYPE2_INTR=2, NO_MII=4 };

#define EPIC_TOTAL_SIZE 0x100
#define USE_IO_OPS 1
#ifdef USE_IO_OPS
#define EPIC_IOTYPE PCI_USES_MASTER|PCI_USES_IO|PCI_ADDR0
#else
#define EPIC_IOTYPE PCI_USES_MASTER|PCI_USES_MEM|PCI_ADDR1
#endif

typedef enum {
	SMSC_83C170_0,
	SMSC_83C170,
	SMSC_83C175,
} chip_t;


struct epic_chip_info {
	const char *name;
	enum pci_id_flags_bits pci_flags;
        int io_size;                            /* Needed for I/O region check or ioremap(). */
        int drv_flags;                          /* Driver use, intended as capability flags. */
};


/* indexed by chip_t */
static struct epic_chip_info pci_id_tbl[] = {
	{ "SMSC EPIC/100 83c170",
	 EPIC_IOTYPE, EPIC_TOTAL_SIZE, TYPE2_INTR | NO_MII | MII_PWRDWN },
	{ "SMSC EPIC/100 83c170",
	 EPIC_IOTYPE, EPIC_TOTAL_SIZE, TYPE2_INTR },
	{ "SMSC EPIC/C 83c175",
	 EPIC_IOTYPE, EPIC_TOTAL_SIZE, TYPE2_INTR | MII_PWRDWN },
};


static struct pci_device_id epic_pci_tbl[] __devinitdata = {
	{ 0x10B8, 0x0005, 0x1092, 0x0AB4, 0, 0, SMSC_83C170_0 },
	{ 0x10B8, 0x0005, PCI_ANY_ID, PCI_ANY_ID, 0, 0, SMSC_83C170 },
	{ 0x10B8, 0x0006, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_NETWORK_ETHERNET << 8, 0xffff00, SMSC_83C175 },
	{ 0,}
};
MODULE_DEVICE_TABLE (pci, epic_pci_tbl);

	
#ifndef USE_IO_OPS
#undef inb
#undef inw
#undef inl
#undef outb
#undef outw
#undef outl
#define inb readb
#define inw readw
#define inl readl
#define outb writeb
#define outw writew
#define outl writel
#endif

/* Offsets to registers, using the (ugh) SMC names. */
enum epic_registers {
  COMMAND=0, INTSTAT=4, INTMASK=8, GENCTL=0x0C, NVCTL=0x10, EECTL=0x14,
  PCIBurstCnt=0x18,
  TEST1=0x1C, CRCCNT=0x20, ALICNT=0x24, MPCNT=0x28,	/* Rx error counters. */
  MIICtrl=0x30, MIIData=0x34, MIICfg=0x38,
  LAN0=64,						/* MAC address. */
  MC0=80,						/* Multicast filter table. */
  RxCtrl=96, TxCtrl=112, TxSTAT=0x74,
  PRxCDAR=0x84, RxSTAT=0xA4, EarlyRx=0xB0, PTxCDAR=0xC4, TxThresh=0xDC,
};

/* Interrupt register bits, using my own meaningful names. */
enum IntrStatus {
	TxIdle=0x40000, RxIdle=0x20000, IntrSummary=0x010000,
	PCIBusErr170=0x7000, PCIBusErr175=0x1000, PhyEvent175=0x8000,
	RxStarted=0x0800, RxEarlyWarn=0x0400, CntFull=0x0200, TxUnderrun=0x0100,
	TxEmpty=0x0080, TxDone=0x0020, RxError=0x0010,
	RxOverflow=0x0008, RxFull=0x0004, RxHeader=0x0002, RxDone=0x0001,
};
enum CommandBits {
	StopRx=1, StartRx=2, TxQueued=4, RxQueued=8,
	StopTxDMA=0x20, StopRxDMA=0x40, RestartTx=0x80,
};

static u16 media2miictl[16] = {
	0, 0x0C00, 0x0C00, 0x2000,  0x0100, 0x2100, 0, 0,
	0, 0, 0, 0,  0, 0, 0, 0 };

/* The EPIC100 Rx and Tx buffer descriptors. */

struct epic_tx_desc {
	u32 txstatus;
	u32 bufaddr;
	u32 buflength;
	u32 next;
};

struct epic_rx_desc {
	u32 rxstatus;
	u32 bufaddr;
	u32 buflength;
	u32 next;
};

enum desc_status_bits {
	DescOwn=0x8000,
};

#define PRIV_ALIGN	15 	/* Required alignment mask */
struct epic_private {
	struct epic_rx_desc *rx_ring;
	struct epic_tx_desc *tx_ring;
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];

	dma_addr_t tx_ring_dma;
	dma_addr_t rx_ring_dma;

	/* Ring pointers. */
	spinlock_t lock;				/* Group with Tx control cache line. */
	unsigned int cur_tx, dirty_tx;

	unsigned int cur_rx, dirty_rx;
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */

	struct pci_dev *pci_dev;			/* PCI bus location. */
	int chip_id, chip_flags;

	struct net_device_stats stats;
	struct timer_list timer;			/* Media selection timer. */
	int tx_threshold;
	unsigned char mc_filter[8];
	signed char phys[4];				/* MII device addresses. */
	u16 advertising;					/* NWay media advertisement */
	int mii_phy_cnt;
	struct mii_if_info mii;
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
};

static int epic_open(struct net_device *dev);
static int read_eeprom(long ioaddr, int location);
static int mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int loc, int val);
static void epic_restart(struct net_device *dev);
static void epic_timer(unsigned long data);
static void epic_tx_timeout(struct net_device *dev);
static void epic_init_ring(struct net_device *dev);
static int epic_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int epic_rx(struct net_device *dev);
static void epic_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int epic_close(struct net_device *dev);
static struct net_device_stats *epic_get_stats(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);



static int __devinit epic_init_one (struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	static int card_idx = -1;
	long ioaddr;
	int chip_idx = (int) ent->driver_data;
	int irq;
	struct net_device *dev;
	struct epic_private *ep;
	int i, option = 0, duplex = 0;
	void *ring_space;
	dma_addr_t ring_dma;

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk (KERN_INFO "%s" KERN_INFO "%s" KERN_INFO "%s",
			version, version2, version3);
#endif
	
	card_idx++;
	
	i = pci_enable_device(pdev);
	if (i)
		return i;
	irq = pdev->irq;

	if (pci_resource_len(pdev, 0) < pci_id_tbl[chip_idx].io_size) {
		printk (KERN_ERR "card %d: no PCI region space\n", card_idx);
		return -ENODEV;
	}
	
	pci_set_master(pdev);

	dev = alloc_etherdev(sizeof (*ep));
	if (!dev) {
		printk (KERN_ERR "card %d: no memory for eth device\n", card_idx);
		return -ENOMEM;
	}
	SET_MODULE_OWNER(dev);

	if (pci_request_regions(pdev, DRV_NAME))
		goto err_out_free_netdev;

#ifdef USE_IO_OPS
	ioaddr = pci_resource_start (pdev, 0);
#else
	ioaddr = pci_resource_start (pdev, 1);
	ioaddr = (long) ioremap (ioaddr, pci_resource_len (pdev, 1));
	if (!ioaddr) {
		printk (KERN_ERR DRV_NAME " %d: ioremap failed\n", card_idx);
		goto err_out_free_res;
	}
#endif

	pci_set_drvdata(pdev, dev);
	ep = dev->priv;
	ep->mii.dev = dev;
	ep->mii.mdio_read = mdio_read;
	ep->mii.mdio_write = mdio_write;

	ring_space = pci_alloc_consistent(pdev, TX_TOTAL_SIZE, &ring_dma);
	if (!ring_space)
		goto err_out_iounmap;
	ep->tx_ring = (struct epic_tx_desc *)ring_space;
	ep->tx_ring_dma = ring_dma;

	ring_space = pci_alloc_consistent(pdev, RX_TOTAL_SIZE, &ring_dma);
	if (!ring_space)
		goto err_out_unmap_tx;
	ep->rx_ring = (struct epic_rx_desc *)ring_space;
	ep->rx_ring_dma = ring_dma;

	if (dev->mem_start) {
		option = dev->mem_start;
		duplex = (dev->mem_start & 16) ? 1 : 0;
	} else if (card_idx >= 0  &&  card_idx < MAX_UNITS) {
		if (options[card_idx] >= 0)
			option = options[card_idx];
		if (full_duplex[card_idx] >= 0)
			duplex = full_duplex[card_idx];
	}

	dev->base_addr = ioaddr;
	dev->irq = irq;

	spin_lock_init (&ep->lock);

	/* Bring the chip out of low-power mode. */
	outl(0x4200, ioaddr + GENCTL);
	/* Magic?!  If we don't set this bit the MII interface won't work. */
	outl(0x0008, ioaddr + TEST1);

	/* Turn on the MII transceiver. */
	outl(0x12, ioaddr + MIICfg);
	if (chip_idx == 1)
		outl((inl(ioaddr + NVCTL) & ~0x003C) | 0x4800, ioaddr + NVCTL);
	outl(0x0200, ioaddr + GENCTL);

	/* Note: the '175 does not have a serial EEPROM. */
	for (i = 0; i < 3; i++)
		((u16 *)dev->dev_addr)[i] = le16_to_cpu(inw(ioaddr + LAN0 + i*4));

	if (debug > 2) {
		printk(KERN_DEBUG DRV_NAME "(%s): EEPROM contents\n",
		       pdev->slot_name);
		for (i = 0; i < 64; i++)
			printk(" %4.4x%s", read_eeprom(ioaddr, i),
				   i % 16 == 15 ? "\n" : "");
	}

	ep->pci_dev = pdev;
	ep->chip_id = chip_idx;
	ep->chip_flags = pci_id_tbl[chip_idx].drv_flags;

	/* Find the connected MII xcvrs.
	   Doing this in open() would allow detecting external xcvrs later, but
	   takes much time and no cards have external MII. */
	{
		int phy, phy_idx = 0;
		for (phy = 1; phy < 32 && phy_idx < sizeof(ep->phys); phy++) {
			int mii_status = mdio_read(dev, phy, MII_BMSR);
			if (mii_status != 0xffff  &&  mii_status != 0x0000) {
				ep->phys[phy_idx++] = phy;
				printk(KERN_INFO DRV_NAME "(%s): MII transceiver #%d control "
					   "%4.4x status %4.4x.\n",
					   pdev->slot_name, phy, mdio_read(dev, phy, 0), mii_status);
			}
		}
		ep->mii_phy_cnt = phy_idx;
		if (phy_idx != 0) {
			phy = ep->phys[0];
			ep->mii.advertising = mdio_read(dev, phy, MII_ADVERTISE);
			printk(KERN_INFO DRV_NAME "(%s): Autonegotiation advertising %4.4x link "
				   "partner %4.4x.\n",
				   pdev->slot_name, ep->mii.advertising, mdio_read(dev, phy, 5));
		} else if ( ! (ep->chip_flags & NO_MII)) {
			printk(KERN_WARNING DRV_NAME "(%s): ***WARNING***: No MII transceiver found!\n",
			       pdev->slot_name);
			/* Use the known PHY address of the EPII. */
			ep->phys[0] = 3;
		}
		ep->mii.phy_id = ep->phys[0];
	}

	/* Turn off the MII xcvr (175 only!), leave the chip in low-power mode. */
	if (ep->chip_flags & MII_PWRDWN)
		outl(inl(ioaddr + NVCTL) & ~0x483C, ioaddr + NVCTL);
	outl(0x0008, ioaddr + GENCTL);

	/* The lower four bits are the media type. */
	if (duplex) {
		ep->mii.duplex_lock = ep->mii.full_duplex = 1;
		printk(KERN_INFO DRV_NAME "(%s):  Forced full duplex operation requested.\n",
		       pdev->slot_name);
	}
	dev->if_port = ep->default_port = option;

	/* The Epic-specific entries in the device structure. */
	dev->open = &epic_open;
	dev->hard_start_xmit = &epic_start_xmit;
	dev->stop = &epic_close;
	dev->get_stats = &epic_get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &netdev_ioctl;
	dev->watchdog_timeo = TX_TIMEOUT;
	dev->tx_timeout = &epic_tx_timeout;

	i = register_netdev(dev);
	if (i)
		goto err_out_unmap_tx;

	printk(KERN_INFO "%s: %s at %#lx, IRQ %d, ",
		   dev->name, pci_id_tbl[chip_idx].name, ioaddr, dev->irq);
	for (i = 0; i < 5; i++)
		printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x.\n", dev->dev_addr[i]);

	return 0;

err_out_unmap_tx:
	pci_free_consistent(pdev, TX_TOTAL_SIZE, ep->tx_ring, ep->tx_ring_dma);
err_out_iounmap:
#ifndef USE_IO_OPS
	iounmap(ioaddr);
err_out_free_res:
#endif
	pci_release_regions(pdev);
err_out_free_netdev:
	kfree(dev);
	return -ENODEV;
}

/* Serial EEPROM section. */

/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x04	/* EEPROM shift clock. */
#define EE_CS			0x02	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x08	/* EEPROM chip data in. */
#define EE_WRITE_0		0x01
#define EE_WRITE_1		0x09
#define EE_DATA_READ	0x10	/* EEPROM chip data out. */
#define EE_ENB			(0x0001 | EE_CS)

/* Delay between EEPROM clock transitions.
   This serves to flush the operation to the PCI bus.
 */

#define eeprom_delay()	inl(ee_addr)

/* The EEPROM commands include the alway-set leading bit. */
#define EE_WRITE_CMD	(5 << 6)
#define EE_READ64_CMD	(6 << 6)
#define EE_READ256_CMD	(6 << 8)
#define EE_ERASE_CMD	(7 << 6)

static int read_eeprom(long ioaddr, int location)
{
	int i;
	int retval = 0;
	long ee_addr = ioaddr + EECTL;
	int read_cmd = location |
		(inl(ee_addr) & 0x40 ? EE_READ64_CMD : EE_READ256_CMD);

	outl(EE_ENB & ~EE_CS, ee_addr);
	outl(EE_ENB, ee_addr);

	/* Shift the read command bits out. */
	for (i = 12; i >= 0; i--) {
		short dataval = (read_cmd & (1 << i)) ? EE_WRITE_1 : EE_WRITE_0;
		outl(EE_ENB | dataval, ee_addr);
		eeprom_delay();
		outl(EE_ENB | dataval | EE_SHIFT_CLK, ee_addr);
		eeprom_delay();
	}
	outl(EE_ENB, ee_addr);

	for (i = 16; i > 0; i--) {
		outl(EE_ENB | EE_SHIFT_CLK, ee_addr);
		eeprom_delay();
		retval = (retval << 1) | ((inl(ee_addr) & EE_DATA_READ) ? 1 : 0);
		outl(EE_ENB, ee_addr);
		eeprom_delay();
	}

	/* Terminate the EEPROM access. */
	outl(EE_ENB & ~EE_CS, ee_addr);
	return retval;
}

#define MII_READOP		1
#define MII_WRITEOP		2
static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	long ioaddr = dev->base_addr;
	int read_cmd = (phy_id << 9) | (location << 4) | MII_READOP;
	int i;

	outl(read_cmd, ioaddr + MIICtrl);
	/* Typical operation takes 25 loops. */
	for (i = 400; i > 0; i--) {
		barrier();
		if ((inl(ioaddr + MIICtrl) & MII_READOP) == 0) {
			/* Work around read failure bug. */
			if (phy_id == 1 && location < 6
				&& inw(ioaddr + MIIData) == 0xffff) {
				outl(read_cmd, ioaddr + MIICtrl);
				continue;
			}
			return inw(ioaddr + MIIData);
		}
	}
	return 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int loc, int value)
{
	long ioaddr = dev->base_addr;
	int i;

	outw(value, ioaddr + MIIData);
	outl((phy_id << 9) | (loc << 4) | MII_WRITEOP, ioaddr + MIICtrl);
	for (i = 10000; i > 0; i--) { 
		barrier();
		if ((inl(ioaddr + MIICtrl) & MII_WRITEOP) == 0)
			break;
	}
	return;
}


static int epic_open(struct net_device *dev)
{
	struct epic_private *ep = dev->priv;
	long ioaddr = dev->base_addr;
	int i;
	int retval;

	/* Soft reset the chip. */
	outl(0x4001, ioaddr + GENCTL);

	if ((retval = request_irq(dev->irq, &epic_interrupt, SA_SHIRQ, dev->name, dev)))
		return retval;

	epic_init_ring(dev);

	outl(0x4000, ioaddr + GENCTL);
	/* This magic is documented in SMSC app note 7.15 */
	outl(0x0008, ioaddr + TEST1);

	/* Pull the chip out of low-power mode, enable interrupts, and set for
	   PCI read multiple.  The MIIcfg setting and strange write order are
	   required by the details of which bits are reset and the transceiver
	   wiring on the Ositech CardBus card.
	*/

	outl(0x12, ioaddr + MIICfg);
	if (ep->chip_flags & MII_PWRDWN)
		outl((inl(ioaddr + NVCTL) & ~0x003C) | 0x4800, ioaddr + NVCTL);

#if defined(__powerpc__) || defined(__sparc__)		/* Big endian */
	outl(0x4432 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
	inl(ioaddr + GENCTL);
	outl(0x0432 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
#else
	outl(0x4412 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
	inl(ioaddr + GENCTL);
	outl(0x0412 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
#endif

	for (i = 0; i < 3; i++)
		outl(cpu_to_le16(((u16*)dev->dev_addr)[i]), ioaddr + LAN0 + i*4);

	ep->tx_threshold = TX_FIFO_THRESH;
	outl(ep->tx_threshold, ioaddr + TxThresh);

	if (media2miictl[dev->if_port & 15]) {
		if (ep->mii_phy_cnt)
			mdio_write(dev, ep->phys[0], MII_BMCR, media2miictl[dev->if_port&15]);
		if (dev->if_port == 1) {
			if (debug > 1)
				printk(KERN_INFO "%s: Using the 10base2 transceiver, MII "
					   "status %4.4x.\n",
					   dev->name, mdio_read(dev, ep->phys[0], MII_BMSR));
		}
	} else {
		int mii_lpa = mdio_read(dev, ep->phys[0], MII_LPA);
		if (mii_lpa != 0xffff) {
			if ((mii_lpa & LPA_100FULL) || (mii_lpa & 0x01C0) == LPA_10FULL)
				ep->mii.full_duplex = 1;
			else if (! (mii_lpa & LPA_LPACK))
				mdio_write(dev, ep->phys[0], MII_BMCR, BMCR_ANENABLE|BMCR_ANRESTART);
			if (debug > 1)
				printk(KERN_INFO "%s: Setting %s-duplex based on MII xcvr %d"
					   " register read of %4.4x.\n", dev->name,
					   ep->mii.full_duplex ? "full" : "half",
					   ep->phys[0], mii_lpa);
		}
	}

	outl(ep->mii.full_duplex ? 0x7F : 0x79, ioaddr + TxCtrl);
	outl(ep->rx_ring_dma, ioaddr + PRxCDAR);
	outl(ep->tx_ring_dma, ioaddr + PTxCDAR);

	/* Start the chip's Rx process. */
	set_rx_mode(dev);
	outl(StartRx | RxQueued, ioaddr + COMMAND);

	netif_start_queue(dev);

	/* Enable interrupts by setting the interrupt mask. */
	outl((ep->chip_flags & TYPE2_INTR ? PCIBusErr175 : PCIBusErr170)
		 | CntFull | TxUnderrun | TxDone | TxEmpty
		 | RxError | RxOverflow | RxFull | RxHeader | RxDone,
		 ioaddr + INTMASK);

	if (debug > 1)
		printk(KERN_DEBUG "%s: epic_open() ioaddr %lx IRQ %d status %4.4x "
			   "%s-duplex.\n",
			   dev->name, ioaddr, dev->irq, (int)inl(ioaddr + GENCTL),
			   ep->mii.full_duplex ? "full" : "half");

	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	init_timer(&ep->timer);
	ep->timer.expires = jiffies + 3*HZ;
	ep->timer.data = (unsigned long)dev;
	ep->timer.function = &epic_timer;				/* timer handler */
	add_timer(&ep->timer);

	return 0;
}

/* Reset the chip to recover from a PCI transaction error.
   This may occur at interrupt time. */
static void epic_pause(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct epic_private *ep = dev->priv;

	netif_stop_queue (dev);
	
	/* Disable interrupts by clearing the interrupt mask. */
	outl(0x00000000, ioaddr + INTMASK);
	/* Stop the chip's Tx and Rx DMA processes. */
	outw(StopRx | StopTxDMA | StopRxDMA, ioaddr + COMMAND);

	/* Update the error counts. */
	if (inw(ioaddr + COMMAND) != 0xffff) {
		ep->stats.rx_missed_errors += inb(ioaddr + MPCNT);
		ep->stats.rx_frame_errors += inb(ioaddr + ALICNT);
		ep->stats.rx_crc_errors += inb(ioaddr + CRCCNT);
	}

	/* Remove the packets on the Rx queue. */
	epic_rx(dev);
}

static void epic_restart(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct epic_private *ep = dev->priv;
	int i;

	/* Soft reset the chip. */
	outl(0x4001, ioaddr + GENCTL);

	printk(KERN_DEBUG "%s: Restarting the EPIC chip, Rx %d/%d Tx %d/%d.\n",
		   dev->name, ep->cur_rx, ep->dirty_rx, ep->dirty_tx, ep->cur_tx);
	udelay(1);

	/* This magic is documented in SMSC app note 7.15 */
	for (i = 16; i > 0; i--)
		outl(0x0008, ioaddr + TEST1);

#if defined(__powerpc__) || defined(__sparc__)		/* Big endian */
	outl(0x0432 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
#else
	outl(0x0412 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
#endif
	outl(dev->if_port == 1 ? 0x13 : 0x12, ioaddr + MIICfg);
	if (ep->chip_flags & MII_PWRDWN)
		outl((inl(ioaddr + NVCTL) & ~0x003C) | 0x4800, ioaddr + NVCTL);

	for (i = 0; i < 3; i++)
		outl(cpu_to_le16(((u16*)dev->dev_addr)[i]), ioaddr + LAN0 + i*4);

	ep->tx_threshold = TX_FIFO_THRESH;
	outl(ep->tx_threshold, ioaddr + TxThresh);
	outl(ep->mii.full_duplex ? 0x7F : 0x79, ioaddr + TxCtrl);
	outl(ep->rx_ring_dma + (ep->cur_rx%RX_RING_SIZE)*
		sizeof(struct epic_rx_desc), ioaddr + PRxCDAR);
	outl(ep->tx_ring_dma + (ep->dirty_tx%TX_RING_SIZE)*
		 sizeof(struct epic_tx_desc), ioaddr + PTxCDAR);

	/* Start the chip's Rx process. */
	set_rx_mode(dev);
	outl(StartRx | RxQueued, ioaddr + COMMAND);

	/* Enable interrupts by setting the interrupt mask. */
	outl((ep->chip_flags & TYPE2_INTR ? PCIBusErr175 : PCIBusErr170)
		 | CntFull | TxUnderrun | TxDone | TxEmpty
		 | RxError | RxOverflow | RxFull | RxHeader | RxDone,
		 ioaddr + INTMASK);
	printk(KERN_DEBUG "%s: epic_restart() done, cmd status %4.4x, ctl %4.4x"
		   " interrupt %4.4x.\n",
		   dev->name, (int)inl(ioaddr + COMMAND), (int)inl(ioaddr + GENCTL),
		   (int)inl(ioaddr + INTSTAT));
	return;
}

static void check_media(struct net_device *dev)
{
	struct epic_private *ep = dev->priv;
	long ioaddr = dev->base_addr;
	int mii_lpa = ep->mii_phy_cnt ? mdio_read(dev, ep->phys[0], MII_LPA) : 0;
	int negotiated = mii_lpa & ep->mii.advertising;
	int duplex = (negotiated & 0x0100) || (negotiated & 0x01C0) == 0x0040;

	if (ep->mii.duplex_lock)
		return;
	if (mii_lpa == 0xffff)		/* Bogus read */
		return;
	if (ep->mii.full_duplex != duplex) {
		ep->mii.full_duplex = duplex;
		printk(KERN_INFO "%s: Setting %s-duplex based on MII #%d link"
			   " partner capability of %4.4x.\n", dev->name,
			   ep->mii.full_duplex ? "full" : "half", ep->phys[0], mii_lpa);
		outl(ep->mii.full_duplex ? 0x7F : 0x79, ioaddr + TxCtrl);
	}
}

static void epic_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct epic_private *ep = dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 5*HZ;

	if (debug > 3) {
		printk(KERN_DEBUG "%s: Media monitor tick, Tx status %8.8x.\n",
			   dev->name, (int)inl(ioaddr + TxSTAT));
		printk(KERN_DEBUG "%s: Other registers are IntMask %4.4x "
			   "IntStatus %4.4x RxStatus %4.4x.\n",
			   dev->name, (int)inl(ioaddr + INTMASK),
			   (int)inl(ioaddr + INTSTAT), (int)inl(ioaddr + RxSTAT));
	}

	check_media(dev);

	ep->timer.expires = jiffies + next_tick;
	add_timer(&ep->timer);
}

static void epic_tx_timeout(struct net_device *dev)
{
	struct epic_private *ep = dev->priv;
	long ioaddr = dev->base_addr;

	if (debug > 0) {
		printk(KERN_WARNING "%s: Transmit timeout using MII device, "
			   "Tx status %4.4x.\n",
			   dev->name, (int)inw(ioaddr + TxSTAT));
		if (debug > 1) {
			printk(KERN_DEBUG "%s: Tx indices: dirty_tx %d, cur_tx %d.\n",
				   dev->name, ep->dirty_tx, ep->cur_tx);
		}
	}
	if (inw(ioaddr + TxSTAT) & 0x10) {		/* Tx FIFO underflow. */
		ep->stats.tx_fifo_errors++;
		outl(RestartTx, ioaddr + COMMAND);
	} else {
		epic_restart(dev);
		outl(TxQueued, dev->base_addr + COMMAND);
	}

	dev->trans_start = jiffies;
	ep->stats.tx_errors++;
	if (!ep->tx_full)
		netif_wake_queue(dev);
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void epic_init_ring(struct net_device *dev)
{
	struct epic_private *ep = dev->priv;
	int i;

	ep->tx_full = 0;
	ep->lock = (spinlock_t) SPIN_LOCK_UNLOCKED;
	ep->dirty_tx = ep->cur_tx = 0;
	ep->cur_rx = ep->dirty_rx = 0;
	ep->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);

	/* Initialize all Rx descriptors. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		ep->rx_ring[i].rxstatus = 0;
		ep->rx_ring[i].buflength = cpu_to_le32(ep->rx_buf_sz);
		ep->rx_ring[i].next = ep->rx_ring_dma + 
				      (i+1)*sizeof(struct epic_rx_desc);
		ep->rx_skbuff[i] = 0;
	}
	/* Mark the last entry as wrapping the ring. */
	ep->rx_ring[i-1].next = ep->rx_ring_dma;

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(ep->rx_buf_sz);
		ep->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;			/* Mark as being used by this device. */
		skb_reserve(skb, 2);	/* 16 byte align the IP header. */
		ep->rx_ring[i].bufaddr = pci_map_single(ep->pci_dev, 
			skb->tail, ep->rx_buf_sz, PCI_DMA_FROMDEVICE);
		ep->rx_ring[i].rxstatus = cpu_to_le32(DescOwn);
	}
	ep->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	/* The Tx buffer descriptor is filled in as needed, but we
	   do need to clear the ownership bit. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		ep->tx_skbuff[i] = 0;
		ep->tx_ring[i].txstatus = 0x0000;
		ep->tx_ring[i].next = ep->tx_ring_dma + 
			(i+1)*sizeof(struct epic_tx_desc);
	}
	ep->tx_ring[i-1].next = ep->tx_ring_dma;
	return;
}

static int epic_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct epic_private *ep = dev->priv;
	int entry, free_count;
	u32 ctrl_word;
	long flags;

	/* Caution: the write order is important here, set the field with the
	   "ownership" bit last. */

	/* Calculate the next Tx descriptor entry. */
	spin_lock_irqsave(&ep->lock, flags);
	free_count = ep->cur_tx - ep->dirty_tx;
	entry = ep->cur_tx % TX_RING_SIZE;

	ep->tx_skbuff[entry] = skb;
	ep->tx_ring[entry].bufaddr = pci_map_single(ep->pci_dev, skb->data, 
		 			            skb->len, PCI_DMA_TODEVICE);
	if (free_count < TX_QUEUE_LEN/2) {/* Typical path */
		ctrl_word = cpu_to_le32(0x100000); /* No interrupt */
	} else if (free_count == TX_QUEUE_LEN/2) {
		ctrl_word = cpu_to_le32(0x140000); /* Tx-done intr. */
	} else if (free_count < TX_QUEUE_LEN - 1) {
		ctrl_word = cpu_to_le32(0x100000); /* No Tx-done intr. */
	} else {
		/* Leave room for an additional entry. */
		ctrl_word = cpu_to_le32(0x140000); /* Tx-done intr. */
		ep->tx_full = 1;
	}
	ep->tx_ring[entry].buflength = ctrl_word | cpu_to_le32(skb->len);
	ep->tx_ring[entry].txstatus =
		((skb->len >= ETH_ZLEN ? skb->len : ETH_ZLEN) << 16)
		| cpu_to_le32(DescOwn);

	ep->cur_tx++;
	if (ep->tx_full)
		netif_stop_queue(dev);

	spin_unlock_irqrestore(&ep->lock, flags);
	/* Trigger an immediate transmit demand. */
	outl(TxQueued, dev->base_addr + COMMAND);

	dev->trans_start = jiffies;
	if (debug > 4)
		printk(KERN_DEBUG "%s: Queued Tx packet size %d to slot %d, "
			   "flag %2.2x Tx status %8.8x.\n",
			   dev->name, (int)skb->len, entry, ctrl_word,
			   (int)inl(dev->base_addr + TxSTAT));

	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void epic_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *dev = dev_instance;
	struct epic_private *ep = dev->priv;
	long ioaddr = dev->base_addr;
	int status, boguscnt = max_interrupt_work;

	do {
		status = inl(ioaddr + INTSTAT);
		/* Acknowledge all of the current interrupt sources ASAP. */
		outl(status & 0x00007fff, ioaddr + INTSTAT);

		if (debug > 4)
			printk(KERN_DEBUG "%s: Interrupt, status=%#8.8x new "
				   "intstat=%#8.8x.\n",
				   dev->name, status, (int)inl(ioaddr + INTSTAT));

		if ((status & IntrSummary) == 0)
			break;

		if (status & (RxDone | RxStarted | RxEarlyWarn | RxOverflow))
			epic_rx(dev);

		if (status & (TxEmpty | TxDone)) {
			unsigned int dirty_tx, cur_tx;

			/* Note: if this lock becomes a problem we can narrow the locked
			   region at the cost of occasionally grabbing the lock more
			   times. */
			spin_lock(&ep->lock);
			cur_tx = ep->cur_tx;
			dirty_tx = ep->dirty_tx;
			for (; cur_tx - dirty_tx > 0; dirty_tx++) {
				struct sk_buff *skb;
				int entry = dirty_tx % TX_RING_SIZE;
				int txstatus = le32_to_cpu(ep->tx_ring[entry].txstatus);

				if (txstatus & DescOwn)
					break;			/* It still hasn't been Txed */

				if ( ! (txstatus & 0x0001)) {
					/* There was an major error, log it. */
#ifndef final_version
					if (debug > 1)
						printk(KERN_DEBUG "%s: Transmit error, Tx status %8.8x.\n",
							   dev->name, txstatus);
#endif
					ep->stats.tx_errors++;
					if (txstatus & 0x1050) ep->stats.tx_aborted_errors++;
					if (txstatus & 0x0008) ep->stats.tx_carrier_errors++;
					if (txstatus & 0x0040) ep->stats.tx_window_errors++;
					if (txstatus & 0x0010) ep->stats.tx_fifo_errors++;
#ifdef ETHER_STATS
					if (txstatus & 0x1000) ep->stats.collisions16++;
#endif
				} else {
#ifdef ETHER_STATS
					if ((txstatus & 0x0002) != 0) ep->stats.tx_deferred++;
#endif
					ep->stats.collisions += (txstatus >> 8) & 15;
					ep->stats.tx_packets++;
					ep->stats.tx_bytes += ep->tx_skbuff[entry]->len;
				}

				/* Free the original skb. */
				skb = ep->tx_skbuff[entry];
				pci_unmap_single(ep->pci_dev, ep->tx_ring[entry].bufaddr, 
						 skb->len, PCI_DMA_TODEVICE);
				dev_kfree_skb_irq(skb);
				ep->tx_skbuff[entry] = 0;
			}

#ifndef final_version
			if (cur_tx - dirty_tx > TX_RING_SIZE) {
				printk(KERN_WARNING "%s: Out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
					   dev->name, dirty_tx, cur_tx, ep->tx_full);
				dirty_tx += TX_RING_SIZE;
			}
#endif
			ep->dirty_tx = dirty_tx;
			if (ep->tx_full
				&& cur_tx - dirty_tx < TX_QUEUE_LEN - 4) {
				/* The ring is no longer full, allow new TX entries. */
				ep->tx_full = 0;
				spin_unlock(&ep->lock);
				netif_wake_queue(dev);
			} else
				spin_unlock(&ep->lock);
		}

		/* Check uncommon events all at once. */
		if (status & (CntFull | TxUnderrun | RxOverflow | RxFull |
					  PCIBusErr170 | PCIBusErr175)) {
			if (status == 0xffffffff) /* Chip failed or removed (CardBus). */
				break;
			/* Always update the error counts to avoid overhead later. */
			ep->stats.rx_missed_errors += inb(ioaddr + MPCNT);
			ep->stats.rx_frame_errors += inb(ioaddr + ALICNT);
			ep->stats.rx_crc_errors += inb(ioaddr + CRCCNT);

			if (status & TxUnderrun) { /* Tx FIFO underflow. */
				ep->stats.tx_fifo_errors++;
				outl(ep->tx_threshold += 128, ioaddr + TxThresh);
				/* Restart the transmit process. */
				outl(RestartTx, ioaddr + COMMAND);
			}
			if (status & RxOverflow) {		/* Missed a Rx frame. */
				ep->stats.rx_errors++;
			}
			if (status & (RxOverflow | RxFull))
				outw(RxQueued, ioaddr + COMMAND);
			if (status & PCIBusErr170) {
				printk(KERN_ERR "%s: PCI Bus Error!  EPIC status %4.4x.\n",
					   dev->name, status);
				epic_pause(dev);
				epic_restart(dev);
			}
			/* Clear all error sources. */
			outl(status & 0x7f18, ioaddr + INTSTAT);
		}
		if (--boguscnt < 0) {
			printk(KERN_ERR "%s: Too much work at interrupt, "
				   "IntrStatus=0x%8.8x.\n",
				   dev->name, status);
			/* Clear all interrupt sources. */
			outl(0x0001ffff, ioaddr + INTSTAT);
			break;
		}
	} while (1);

	if (debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, intr_status=%#4.4x.\n",
			   dev->name, status);

	return;
}

static int epic_rx(struct net_device *dev)
{
	struct epic_private *ep = dev->priv;
	int entry = ep->cur_rx % RX_RING_SIZE;
	int rx_work_limit = ep->dirty_rx + RX_RING_SIZE - ep->cur_rx;
	int work_done = 0;

	if (debug > 4)
		printk(KERN_DEBUG " In epic_rx(), entry %d %8.8x.\n", entry,
			   ep->rx_ring[entry].rxstatus);
	/* If we own the next entry, it's a new packet. Send it up. */
	while ((ep->rx_ring[entry].rxstatus & cpu_to_le32(DescOwn)) == 0) {
		int status = le32_to_cpu(ep->rx_ring[entry].rxstatus);

		if (debug > 4)
			printk(KERN_DEBUG "  epic_rx() status was %8.8x.\n", status);
		if (--rx_work_limit < 0)
			break;
		if (status & 0x2006) {
			if (debug > 2)
				printk(KERN_DEBUG "%s: epic_rx() error status was %8.8x.\n",
					   dev->name, status);
			if (status & 0x2000) {
				printk(KERN_WARNING "%s: Oversized Ethernet frame spanned "
					   "multiple buffers, status %4.4x!\n", dev->name, status);
				ep->stats.rx_length_errors++;
			} else if (status & 0x0006)
				/* Rx Frame errors are counted in hardware. */
				ep->stats.rx_errors++;
		} else {
			/* Malloc up new buffer, compatible with net-2e. */
			/* Omit the four octet CRC from the length. */
			short pkt_len = (status >> 16) - 4;
			struct sk_buff *skb;

			pci_dma_sync_single(ep->pci_dev, ep->rx_ring[entry].bufaddr, 
					    ep->rx_buf_sz, PCI_DMA_FROMDEVICE);
			if (pkt_len > PKT_BUF_SZ - 4) {
				printk(KERN_ERR "%s: Oversized Ethernet frame, status %x "
					   "%d bytes.\n",
					   dev->name, status, pkt_len);
				pkt_len = 1514;
			}
			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
#if 1 /* HAS_IP_COPYSUM */
				eth_copy_and_sum(skb, ep->rx_skbuff[entry]->tail, pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len), ep->rx_skbuff[entry]->tail,
					   pkt_len);
#endif
			} else {
				pci_unmap_single(ep->pci_dev, 
					ep->rx_ring[entry].bufaddr, 
					ep->rx_buf_sz, PCI_DMA_FROMDEVICE);
				skb_put(skb = ep->rx_skbuff[entry], pkt_len);
				ep->rx_skbuff[entry] = NULL;
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			ep->stats.rx_packets++;
			ep->stats.rx_bytes += pkt_len;
		}
		work_done++;
		entry = (++ep->cur_rx) % RX_RING_SIZE;
	}

	/* Refill the Rx ring buffers. */
	for (; ep->cur_rx - ep->dirty_rx > 0; ep->dirty_rx++) {
		entry = ep->dirty_rx % RX_RING_SIZE;
		if (ep->rx_skbuff[entry] == NULL) {
			struct sk_buff *skb;
			skb = ep->rx_skbuff[entry] = dev_alloc_skb(ep->rx_buf_sz);
			if (skb == NULL)
				break;
			skb->dev = dev;			/* Mark as being used by this device. */
			skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
			ep->rx_ring[entry].bufaddr = pci_map_single(ep->pci_dev, 
				skb->tail, ep->rx_buf_sz, PCI_DMA_FROMDEVICE);
			work_done++;
		}
		ep->rx_ring[entry].rxstatus = cpu_to_le32(DescOwn);
	}
	return work_done;
}

static int epic_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct epic_private *ep = dev->priv;
	struct sk_buff *skb;
	int i;

	netif_stop_queue(dev);

	if (debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %2.2x.\n",
			   dev->name, (int)inl(ioaddr + INTSTAT));

	del_timer_sync(&ep->timer);
	epic_pause(dev);
	free_irq(dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		skb = ep->rx_skbuff[i];
		ep->rx_skbuff[i] = 0;
		ep->rx_ring[i].rxstatus = 0;		/* Not owned by Epic chip. */
		ep->rx_ring[i].buflength = 0;
		if (skb) {
			pci_unmap_single(ep->pci_dev, ep->rx_ring[i].bufaddr, 
				 	 ep->rx_buf_sz, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(skb);
		}
		ep->rx_ring[i].bufaddr = 0xBADF00D0; /* An invalid address. */
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		skb = ep->tx_skbuff[i];
		ep->tx_skbuff[i] = 0;
		if (!skb)
			continue;
		pci_unmap_single(ep->pci_dev, ep->tx_ring[i].bufaddr, 
				 skb->len, PCI_DMA_TODEVICE);
		dev_kfree_skb(skb);
	}

	/* Green! Leave the chip in low-power mode. */
	outl(0x0008, ioaddr + GENCTL);

	return 0;
}

static struct net_device_stats *epic_get_stats(struct net_device *dev)
{
	struct epic_private *ep = dev->priv;
	long ioaddr = dev->base_addr;

	if (netif_running(dev)) {
		/* Update the error counts. */
		ep->stats.rx_missed_errors += inb(ioaddr + MPCNT);
		ep->stats.rx_frame_errors += inb(ioaddr + ALICNT);
		ep->stats.rx_crc_errors += inb(ioaddr + CRCCNT);
	}

	return &ep->stats;
}

/* Set or clear the multicast filter for this adaptor.
   Note that we only use exclusion around actually queueing the
   new frame, not around filling ep->setup_frame.  This is non-deterministic
   when re-entered but still correct. */

static void set_rx_mode(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct epic_private *ep = dev->priv;
	unsigned char mc_filter[8];		 /* Multicast hash filter */
	int i;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		outl(0x002C, ioaddr + RxCtrl);
		/* Unconditionally log net taps. */
		printk(KERN_INFO "%s: Promiscuous mode enabled.\n", dev->name);
		memset(mc_filter, 0xff, sizeof(mc_filter));
	} else if ((dev->mc_count > 0)  ||  (dev->flags & IFF_ALLMULTI)) {
		/* There is apparently a chip bug, so the multicast filter
		   is never enabled. */
		/* Too many to filter perfectly -- accept all multicasts. */
		memset(mc_filter, 0xff, sizeof(mc_filter));
		outl(0x000C, ioaddr + RxCtrl);
	} else if (dev->mc_count == 0) {
		outl(0x0004, ioaddr + RxCtrl);
		return;
	} else {					/* Never executed, for now. */
		struct dev_mc_list *mclist;

		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next)
			set_bit(ether_crc_le(ETH_ALEN, mclist->dmi_addr) & 0x3f,
					mc_filter);
	}
	/* ToDo: perhaps we need to stop the Tx and Rx process here? */
	if (memcmp(mc_filter, ep->mc_filter, sizeof(mc_filter))) {
		for (i = 0; i < 4; i++)
			outw(((u16 *)mc_filter)[i], ioaddr + MC0 + i*4);
		memcpy(ep->mc_filter, mc_filter, sizeof(mc_filter));
	}
	return;
}

static int netdev_ethtool_ioctl (struct net_device *dev, void *useraddr)
{
	struct epic_private *np = dev->priv;
	u32 ethcmd;

	if (copy_from_user (&ethcmd, useraddr, sizeof (ethcmd)))
		return -EFAULT;

	switch (ethcmd) {
	case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo info = { ETHTOOL_GDRVINFO };
		strcpy (info.driver, DRV_NAME);
		strcpy (info.version, DRV_VERSION);
		strcpy (info.bus_info, np->pci_dev->slot_name);
		if (copy_to_user (useraddr, &info, sizeof (info)))
			return -EFAULT;
		return 0;
	}

	/* get settings */
	case ETHTOOL_GSET: {
		struct ethtool_cmd ecmd = { ETHTOOL_GSET };
		spin_lock_irq(&np->lock);
		mii_ethtool_gset(&np->mii, &ecmd);
		spin_unlock_irq(&np->lock);
		if (copy_to_user(useraddr, &ecmd, sizeof(ecmd)))
			return -EFAULT;
		return 0;
	}
	/* set settings */
	case ETHTOOL_SSET: {
		int r;
		struct ethtool_cmd ecmd;
		if (copy_from_user(&ecmd, useraddr, sizeof(ecmd)))
			return -EFAULT;
		spin_lock_irq(&np->lock);
		r = mii_ethtool_sset(&np->mii, &ecmd);
		spin_unlock_irq(&np->lock);
		return r;
	}
	/* restart autonegotiation */
	case ETHTOOL_NWAY_RST: {
		return mii_nway_restart(&np->mii);
	}
	/* get link status */
	case ETHTOOL_GLINK: {
		struct ethtool_value edata = {ETHTOOL_GLINK};
		edata.data = mii_link_ok(&np->mii);
		if (copy_to_user(useraddr, &edata, sizeof(edata)))
			return -EFAULT;
		return 0;
	}

	/* get message-level */
	case ETHTOOL_GMSGLVL: {
		struct ethtool_value edata = {ETHTOOL_GMSGLVL};
		edata.data = debug;
		if (copy_to_user(useraddr, &edata, sizeof(edata)))
			return -EFAULT;
		return 0;
	}
	/* set message-level */
	case ETHTOOL_SMSGLVL: {
		struct ethtool_value edata;
		if (copy_from_user(&edata, useraddr, sizeof(edata)))
			return -EFAULT;
		debug = edata.data;
		return 0;
	}
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct epic_private *ep = dev->priv;
	long ioaddr = dev->base_addr;
	struct mii_ioctl_data *data = (struct mii_ioctl_data *)&rq->ifr_data;

	switch(cmd) {
	case SIOCETHTOOL:
		return netdev_ethtool_ioctl(dev, (void *) rq->ifr_data);

	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
	case SIOCDEVPRIVATE:		/* for binary compat, remove in 2.5 */
		data->phy_id = ep->phys[0] & 0x1f;
		/* Fall Through */

	case SIOCGMIIREG:		/* Read MII PHY register. */
	case SIOCDEVPRIVATE+1:		/* for binary compat, remove in 2.5 */
		if (! netif_running(dev)) {
			outl(0x0200, ioaddr + GENCTL);
			outl((inl(ioaddr + NVCTL) & ~0x003C) | 0x4800, ioaddr + NVCTL);
		}
		data->val_out = mdio_read(dev, data->phy_id & 0x1f, data->reg_num & 0x1f);
#if 0					/* Just leave on if the ioctl() is ever used. */
		if (! netif_running(dev)) {
			outl(0x0008, ioaddr + GENCTL);
			outl((inl(ioaddr + NVCTL) & ~0x483C) | 0x0000, ioaddr + NVCTL);
		}
#endif
		return 0;

	case SIOCSMIIREG:		/* Write MII PHY register. */
	case SIOCDEVPRIVATE+2:		/* for binary compat, remove in 2.5 */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (! netif_running(dev)) {
			outl(0x0200, ioaddr + GENCTL);
			outl((inl(ioaddr + NVCTL) & ~0x003C) | 0x4800, ioaddr + NVCTL);
		}
		if (data->phy_id == ep->phys[0]) {
			u16 value = data->val_in;
			switch (data->reg_num) {
			case 0:
				/* Check for autonegotiation on or reset. */
				ep->mii.duplex_lock = (value & 0x9000) ? 0 : 1;
				if (ep->mii.duplex_lock)
					ep->mii.full_duplex = (value & 0x0100) ? 1 : 0;
				break;
			case 4: ep->mii.advertising = value; break;
			}
			/* Perhaps check_duplex(dev), depending on chip semantics. */
		}
		mdio_write(dev, data->phy_id & 0x1f, data->reg_num & 0x1f, data->val_in);
#if 0					/* Leave on if the ioctl() is used. */
		if (! netif_running(dev)) {
			outl(0x0008, ioaddr + GENCTL);
			outl((inl(ioaddr + NVCTL) & ~0x483C) | 0x0000, ioaddr + NVCTL);
		}
#endif
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}


static void __devexit epic_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct epic_private *ep = dev->priv;
	
	pci_free_consistent(pdev, TX_TOTAL_SIZE, ep->tx_ring, ep->tx_ring_dma);
	pci_free_consistent(pdev, RX_TOTAL_SIZE, ep->rx_ring, ep->rx_ring_dma);
	unregister_netdev(dev);
#ifndef USE_IO_OPS
	iounmap((void*) dev->base_addr);
#endif
	pci_release_regions(pdev);
	kfree(dev);
	pci_set_drvdata(pdev, NULL);
	/* pci_power_off(pdev, -1); */
}


#ifdef CONFIG_PM

static int epic_suspend (struct pci_dev *pdev, u32 state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	long ioaddr = dev->base_addr;

	if (!netif_running(dev))
		return 0;
	epic_pause(dev);
	/* Put the chip into low-power mode. */
	outl(0x0008, ioaddr + GENCTL);
	/* pci_power_off(pdev, -1); */
	return 0;
}


static int epic_resume (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (!netif_running(dev))
		return 0;
	epic_restart(dev);
	/* pci_power_on(pdev); */
	return 0;
}

#endif /* CONFIG_PM */


static struct pci_driver epic_driver = {
	name:		DRV_NAME,
	id_table:	epic_pci_tbl,
	probe:		epic_init_one,
	remove:		__devexit_p(epic_remove_one),
#ifdef CONFIG_PM
	suspend:	epic_suspend,
	resume:		epic_resume,
#endif /* CONFIG_PM */
};


static int __init epic_init (void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk (KERN_INFO "%s" KERN_INFO "%s" KERN_INFO "%s",
		version, version2, version3);
#endif

	return pci_module_init (&epic_driver);
}


static void __exit epic_cleanup (void)
{
	pci_unregister_driver (&epic_driver);
}


module_init(epic_init);
module_exit(epic_cleanup);
