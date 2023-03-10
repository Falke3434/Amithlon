/*
    dmfe.c: Version 1.36p1 2001-05-12 for Linux kernel 2.4.x

    A Davicom DM9102/DM9102A/DM9102A+DM9801/DM9102A+DM9802 NIC fast
    ethernet driver for Linux.
    Copyright (C) 1997  Sten Wang

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    DAVICOM Web-Site: www.davicom.com.tw

    Author: Sten Wang, 886-3-5798797-8517, E-mail: sten_wang@davicom.com.tw
    Maintainer: Tobias Ringstrom <tori@unhappy.mine.nu>

    (C)Copyright 1997-1998 DAVICOM Semiconductor,Inc. All Rights Reserved.

    Marcelo Tosatti <marcelo@conectiva.com.br> :
    Made it compile in 2.3 (device to net_device)

    Alan Cox <alan@redhat.com> :
    Cleaned up for kernel merge.
    Removed the back compatibility support
    Reformatted, fixing spelling etc as I went
    Removed IRQ 0-15 assumption

    Jeff Garzik <jgarzik@mandrakesoft.com> :
    Updated to use new PCI driver API.
    Resource usage cleanups.
    Report driver version to user.

    Tobias Ringstrom <tori@unhappy.mine.nu> :
    Cleaned up and added SMP safety.  Thanks go to Jeff Garzik,
	Andrew Morton and Frank Davis for the SMP safety fixes.

    TODO

    Implement pci_driver::suspend() and pci_driver::resume()
    power management methods.

    Check and fix on 64bit and big endian boxes.

    Test and make sure PCI latency is now correct for all cases.
*/

#define DMFE_VERSION "1.36p1 (May 12, 2001)"

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/spinlock.h>

#include <asm/processor.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#if BITS_PER_LONG == 64
#error FIXME: driver does not support 64-bit platforms
#endif


/* Board/System/Debug information/definition ---------------- */
#define PCI_DM9132_ID   0x91321282      /* Davicom DM9132 ID */
#define PCI_DM9102_ID   0x91021282      /* Davicom DM9102 ID */
#define PCI_DM9100_ID   0x91001282      /* Davicom DM9100 ID */
#define PCI_DM9009_ID   0x90091282      /* Davicom DM9009 ID */

#define DMFE_SUCC       0
#define DM9102_IO_SIZE  0x80
#define DM9102A_IO_SIZE 0x100
#define TX_MAX_SEND_CNT 0x1             /* Maximum tx packet per time */
#define TX_DESC_CNT     0x10            /* Allocated Tx descriptors */
#define RX_DESC_CNT     0x20            /* Allocated Rx descriptors */
#define TX_FREE_DESC_CNT (TX_DESC_CNT - 2)	/* Max TX packet count */
#define TX_WAKE_DESC_CNT (TX_DESC_CNT - 3)	/* TX wakeup count */
#define DESC_ALL_CNT    (TX_DESC_CNT + RX_DESC_CNT)
#define TX_BUF_ALLOC    0x600
#define RX_ALLOC_SIZE   0x620
#define DM910X_RESET    1
#define CR0_DEFAULT     0x00E00000      /* TX & RX burst mode */
#define CR6_DEFAULT     0x00080000      /* HD */
#define CR7_DEFAULT     0x180c1
#define CR15_DEFAULT    0x06            /* TxJabber RxWatchdog */
#define TDES0_ERR_MASK  0x4302          /* TXJT, LC, EC, FUE */
#define MAX_PACKET_SIZE 1514
#define DMFE_MAX_MULTICAST 14
#define RX_COPY_SIZE	100
#define MAX_CHECK_PACKET 0x8000
#define DM9801_NOISE_FLOOR 8
#define DM9802_NOISE_FLOOR 5

#define DMFE_10MHF      0
#define DMFE_100MHF     1
#define DMFE_10MFD      4
#define DMFE_100MFD     5
#define DMFE_AUTO       8
#define DMFE_1M_HPNA    0x10

#define DMFE_TXTH_72	0x400000	/* TX TH 72 byte */
#define DMFE_TXTH_96	0x404000	/* TX TH 96 byte */
#define DMFE_TXTH_128	0x0000		/* TX TH 128 byte */
#define DMFE_TXTH_256	0x4000		/* TX TH 256 byte */
#define DMFE_TXTH_512	0x8000		/* TX TH 512 byte */
#define DMFE_TXTH_1K	0xC000		/* TX TH 1K  byte */

#define DMFE_TIMER_WUT  (jiffies + HZ * 1)/* timer wakeup time : 1 second */
#define DMFE_TX_TIMEOUT ((3*HZ)/2)	/* tx packet time-out time 1.5 s" */
#define DMFE_TX_KICK 	(HZ/2)	/* tx packet Kick-out time 0.5 s" */

#define DMFE_DBUG(dbug_now, msg, value) if (dmfe_debug || (dbug_now)) printk(KERN_ERR "<DMFE>: %s %lx\n", (msg), (long) (value))

#define SHOW_MEDIA_TYPE(mode) printk(KERN_ERR "<DMFE>: Change Speed to %sMhz %s duplex\n",mode & 1 ?"100":"10", mode & 4 ? "full":"half");


/* CR9 definition: SROM/MII */
#define CR9_SROM_READ   0x4800
#define CR9_SRCS        0x1
#define CR9_SRCLK       0x2
#define CR9_CRDOUT      0x8
#define SROM_DATA_0     0x0
#define SROM_DATA_1     0x4
#define PHY_DATA_1      0x20000
#define PHY_DATA_0      0x00000
#define MDCLKH          0x10000

#define PHY_POWER_DOWN	0x800

#define SROM_V41_CODE   0x14

#define SROM_CLK_WRITE(data, ioaddr) outl(data|CR9_SROM_READ|CR9_SRCS,ioaddr);udelay(5);outl(data|CR9_SROM_READ|CR9_SRCS|CR9_SRCLK,ioaddr);udelay(5);outl(data|CR9_SROM_READ|CR9_SRCS,ioaddr);udelay(5);

#define __CHK_IO_SIZE(pci_id, dev_rev) ( ((pci_id)==PCI_DM9132_ID) || ((dev_rev) >= 0x02000030) ) ? DM9102A_IO_SIZE: DM9102_IO_SIZE
#define CHK_IO_SIZE(pci_dev, dev_rev) __CHK_IO_SIZE(((pci_dev)->device << 16) | (pci_dev)->vendor, dev_rev)

/* Sten Check */
#define DEVICE net_device

/* Structure/enum declaration ------------------------------- */
struct tx_desc {
	u32 tdes0, tdes1, tdes2, tdes3;
	u32 tx_skb_ptr;
	u32 tx_buf_ptr;
	u32 next_tx_desc;
	u32 reserved;
};

struct rx_desc {
	u32 rdes0, rdes1, rdes2, rdes3;
	u32 rx_skb_ptr;
	u32 rx_buf_ptr;
	u32 next_rx_desc;
	u32 reserved;
};

struct dmfe_board_info {
	u32 chip_id;			/* Chip vendor/Device ID */
	u32 chip_revision;		/* Chip revision */
	struct DEVICE *next_dev;	/* next device */
	struct pci_dev * net_dev;	/* PCI device */
	spinlock_t lock;

	long ioaddr;			/* I/O base address */
	u32 cr0_data;
	u32 cr5_data;
	u32 cr6_data;
	u32 cr7_data;
	u32 cr15_data;

	/* pointer for memory physical address */
	dma_addr_t buf_pool_dma_ptr;	/* Tx buffer pool memory */
	dma_addr_t buf_pool_dma_start;	/* Tx buffer pool align dword */
	dma_addr_t desc_pool_dma_ptr;	/* descriptor pool memory */
	dma_addr_t first_tx_desc_dma;
	dma_addr_t first_rx_desc_dma;

	/* descriptor pointer */
	unsigned char *buf_pool_ptr;	/* Tx buffer pool memory */
	unsigned char *buf_pool_start;	/* Tx buffer pool align dword */
	unsigned char *desc_pool_ptr;	/* descriptor pool memory */
	struct tx_desc *first_tx_desc;
	struct tx_desc *tx_insert_ptr;
	struct tx_desc *tx_remove_ptr;
	struct rx_desc *first_rx_desc;
	struct rx_desc *rx_insert_ptr;
	struct rx_desc *rx_ready_ptr;	/* packet come pointer */
	unsigned long tx_packet_cnt;		/* transmitted packet count */
	unsigned long tx_queue_cnt;		/* wait to send packet count */
	unsigned long rx_avail_cnt;		/* available rx descriptor count */
	unsigned long interval_rx_cnt;		/* rx packet count a callback time */

	u16 HPNA_command;		/* For HPNA register 16 */
	u16 HPNA_timer;			/* For HPNA remote device check */
	u16 dbug_cnt;
	u16 NIC_capability;		/* NIC media capability */
	u16 PHY_reg4;			/* Saved Phyxcer register 4 value */

	u8 HPNA_present;		/* 0:none, 1:DM9801, 2:DM9802 */
	u8 chip_type;			/* Keep DM9102A chip type */
	u8 media_mode;			/* user specify media mode */
	u8 op_mode;			/* real work media mode */
	u8 phy_addr;
	u8 link_failed;			/* Ever link failed */
	u8 wait_reset;			/* Hardware failed, need to reset */
	u8 dm910x_chk_mode;		/* Operating mode check */
	u8 first_in_callback;		/* Flag to record state */
	struct timer_list timer;

	/* System defined statistic counter */
	struct net_device_stats stats;

	/* Driver defined statistic counter */
	unsigned long tx_fifo_underrun;
	unsigned long tx_loss_carrier;
	unsigned long tx_no_carrier;
	unsigned long tx_late_collision;
	unsigned long tx_excessive_collision;
	unsigned long tx_jabber_timeout;
	unsigned long reset_count;
	unsigned long reset_cr8;
	unsigned long reset_fatal;
	unsigned long reset_TXtimeout;

	/* NIC SROM data */
	unsigned char srom[128];
};

enum dmfe_offsets {
	DCR0 = 0x00, DCR1 = 0x08, DCR2 = 0x10, DCR3 = 0x18, DCR4 = 0x20,
	DCR5 = 0x28, DCR6 = 0x30, DCR7 = 0x38, DCR8 = 0x40, DCR9 = 0x48,
	DCR10 = 0x50, DCR11 = 0x58, DCR12 = 0x60, DCR13 = 0x68, DCR14 = 0x70,
	DCR15 = 0x78
};

enum dmfe_CR6_bits {
	CR6_RXSC = 0x2, CR6_PBF = 0x8, CR6_PM = 0x40, CR6_PAM = 0x80,
	CR6_FDM = 0x200, CR6_TXSC = 0x2000, CR6_STI = 0x100000,
	CR6_SFT = 0x200000, CR6_RXA = 0x40000000, CR6_NO_PURGE = 0x20000000
};

/* Global variable declaration ----------------------------- */
static int __devinitdata printed_version;
static char version[] __devinitdata =
	KERN_INFO "Davicom DM9xxx net driver, version " DMFE_VERSION "\n";

static int dmfe_debug;
static unsigned char dmfe_media_mode = DMFE_AUTO;
static u32 dmfe_cr6_user_set;

/* For module input parameter */
static int debug;
static u32 cr6set;
static unsigned char mode = 8;
static u8 chkmode = 1;
static u8 HPNA_mode;		/* Default: Low Power/High Speed */
static u8 HPNA_rx_cmd;		/* Default: Disable Rx remote command */
static u8 HPNA_tx_cmd;		/* Default: Don't issue remote command */
static u8 HPNA_NoiseFloor;	/* Default: HPNA NoiseFloor */
static u8 SF_mode;		/* Special Function: 1:VLAN, 2:RX Flow Control
				   4: TX pause packet */

unsigned long CrcTable[256] = {
	0x00000000L, 0x77073096L, 0xEE0E612CL, 0x990951BAL,
	0x076DC419L, 0x706AF48FL, 0xE963A535L, 0x9E6495A3L,
	0x0EDB8832L, 0x79DCB8A4L, 0xE0D5E91EL, 0x97D2D988L,
	0x09B64C2BL, 0x7EB17CBDL, 0xE7B82D07L, 0x90BF1D91L,
	0x1DB71064L, 0x6AB020F2L, 0xF3B97148L, 0x84BE41DEL,
	0x1ADAD47DL, 0x6DDDE4EBL, 0xF4D4B551L, 0x83D385C7L,
	0x136C9856L, 0x646BA8C0L, 0xFD62F97AL, 0x8A65C9ECL,
	0x14015C4FL, 0x63066CD9L, 0xFA0F3D63L, 0x8D080DF5L,
	0x3B6E20C8L, 0x4C69105EL, 0xD56041E4L, 0xA2677172L,
	0x3C03E4D1L, 0x4B04D447L, 0xD20D85FDL, 0xA50AB56BL,
	0x35B5A8FAL, 0x42B2986CL, 0xDBBBC9D6L, 0xACBCF940L,
	0x32D86CE3L, 0x45DF5C75L, 0xDCD60DCFL, 0xABD13D59L,
	0x26D930ACL, 0x51DE003AL, 0xC8D75180L, 0xBFD06116L,
	0x21B4F4B5L, 0x56B3C423L, 0xCFBA9599L, 0xB8BDA50FL,
	0x2802B89EL, 0x5F058808L, 0xC60CD9B2L, 0xB10BE924L,
	0x2F6F7C87L, 0x58684C11L, 0xC1611DABL, 0xB6662D3DL,
	0x76DC4190L, 0x01DB7106L, 0x98D220BCL, 0xEFD5102AL,
	0x71B18589L, 0x06B6B51FL, 0x9FBFE4A5L, 0xE8B8D433L,
	0x7807C9A2L, 0x0F00F934L, 0x9609A88EL, 0xE10E9818L,
	0x7F6A0DBBL, 0x086D3D2DL, 0x91646C97L, 0xE6635C01L,
	0x6B6B51F4L, 0x1C6C6162L, 0x856530D8L, 0xF262004EL,
	0x6C0695EDL, 0x1B01A57BL, 0x8208F4C1L, 0xF50FC457L,
	0x65B0D9C6L, 0x12B7E950L, 0x8BBEB8EAL, 0xFCB9887CL,
	0x62DD1DDFL, 0x15DA2D49L, 0x8CD37CF3L, 0xFBD44C65L,
	0x4DB26158L, 0x3AB551CEL, 0xA3BC0074L, 0xD4BB30E2L,
	0x4ADFA541L, 0x3DD895D7L, 0xA4D1C46DL, 0xD3D6F4FBL,
	0x4369E96AL, 0x346ED9FCL, 0xAD678846L, 0xDA60B8D0L,
	0x44042D73L, 0x33031DE5L, 0xAA0A4C5FL, 0xDD0D7CC9L,
	0x5005713CL, 0x270241AAL, 0xBE0B1010L, 0xC90C2086L,
	0x5768B525L, 0x206F85B3L, 0xB966D409L, 0xCE61E49FL,
	0x5EDEF90EL, 0x29D9C998L, 0xB0D09822L, 0xC7D7A8B4L,
	0x59B33D17L, 0x2EB40D81L, 0xB7BD5C3BL, 0xC0BA6CADL,
	0xEDB88320L, 0x9ABFB3B6L, 0x03B6E20CL, 0x74B1D29AL,
	0xEAD54739L, 0x9DD277AFL, 0x04DB2615L, 0x73DC1683L,
	0xE3630B12L, 0x94643B84L, 0x0D6D6A3EL, 0x7A6A5AA8L,
	0xE40ECF0BL, 0x9309FF9DL, 0x0A00AE27L, 0x7D079EB1L,
	0xF00F9344L, 0x8708A3D2L, 0x1E01F268L, 0x6906C2FEL,
	0xF762575DL, 0x806567CBL, 0x196C3671L, 0x6E6B06E7L,
	0xFED41B76L, 0x89D32BE0L, 0x10DA7A5AL, 0x67DD4ACCL,
	0xF9B9DF6FL, 0x8EBEEFF9L, 0x17B7BE43L, 0x60B08ED5L,
	0xD6D6A3E8L, 0xA1D1937EL, 0x38D8C2C4L, 0x4FDFF252L,
	0xD1BB67F1L, 0xA6BC5767L, 0x3FB506DDL, 0x48B2364BL,
	0xD80D2BDAL, 0xAF0A1B4CL, 0x36034AF6L, 0x41047A60L,
	0xDF60EFC3L, 0xA867DF55L, 0x316E8EEFL, 0x4669BE79L,
	0xCB61B38CL, 0xBC66831AL, 0x256FD2A0L, 0x5268E236L,
	0xCC0C7795L, 0xBB0B4703L, 0x220216B9L, 0x5505262FL,
	0xC5BA3BBEL, 0xB2BD0B28L, 0x2BB45A92L, 0x5CB36A04L,
	0xC2D7FFA7L, 0xB5D0CF31L, 0x2CD99E8BL, 0x5BDEAE1DL,
	0x9B64C2B0L, 0xEC63F226L, 0x756AA39CL, 0x026D930AL,
	0x9C0906A9L, 0xEB0E363FL, 0x72076785L, 0x05005713L,
	0x95BF4A82L, 0xE2B87A14L, 0x7BB12BAEL, 0x0CB61B38L,
	0x92D28E9BL, 0xE5D5BE0DL, 0x7CDCEFB7L, 0x0BDBDF21L,
	0x86D3D2D4L, 0xF1D4E242L, 0x68DDB3F8L, 0x1FDA836EL,
	0x81BE16CDL, 0xF6B9265BL, 0x6FB077E1L, 0x18B74777L,
	0x88085AE6L, 0xFF0F6A70L, 0x66063BCAL, 0x11010B5CL,
	0x8F659EFFL, 0xF862AE69L, 0x616BFFD3L, 0x166CCF45L,
	0xA00AE278L, 0xD70DD2EEL, 0x4E048354L, 0x3903B3C2L,
	0xA7672661L, 0xD06016F7L, 0x4969474DL, 0x3E6E77DBL,
	0xAED16A4AL, 0xD9D65ADCL, 0x40DF0B66L, 0x37D83BF0L,
	0xA9BCAE53L, 0xDEBB9EC5L, 0x47B2CF7FL, 0x30B5FFE9L,
	0xBDBDF21CL, 0xCABAC28AL, 0x53B39330L, 0x24B4A3A6L,
	0xBAD03605L, 0xCDD70693L, 0x54DE5729L, 0x23D967BFL,
	0xB3667A2EL, 0xC4614AB8L, 0x5D681B02L, 0x2A6F2B94L,
	0xB40BBE37L, 0xC30C8EA1L, 0x5A05DF1BL, 0x2D02EF8DL
};

/* function declaration ------------------------------------- */
static int dmfe_open(struct DEVICE *);
static int dmfe_start_xmit(struct sk_buff *, struct DEVICE *);
static int dmfe_stop(struct DEVICE *);
static struct net_device_stats * dmfe_get_stats(struct DEVICE *);
static void dmfe_set_filter_mode(struct DEVICE *);
static int dmfe_do_ioctl(struct DEVICE *, struct ifreq *, int);
static u16 read_srom_word(long ,int);
static void dmfe_interrupt(int , void *, struct pt_regs *);
static void dmfe_descriptor_init(struct dmfe_board_info *, unsigned long);
static void allocated_rx_buffer(struct dmfe_board_info *);
static void update_cr6(u32, unsigned long);
static void send_filter_frame(struct DEVICE * ,int);
static void dm9132_id_table(struct DEVICE * ,int);
static u16 phy_read(unsigned long, u8, u8, u32);
static void phy_write(unsigned long, u8, u8, u16, u32);
static void phy_write_1bit(unsigned long, u32);
static u16 phy_read_1bit(unsigned long);
static u8 dmfe_sense_speed(struct dmfe_board_info *);
static void dmfe_process_mode(struct dmfe_board_info *);
static void dmfe_timer(unsigned long);
static void dmfe_rx_packet(struct DEVICE *, struct dmfe_board_info *);
static void dmfe_free_tx_pkt(struct DEVICE *, struct dmfe_board_info *);
static void dmfe_reused_skb(struct dmfe_board_info *, struct sk_buff *);
static void dmfe_dynamic_reset(struct DEVICE *);
static void dmfe_free_rxbuffer(struct dmfe_board_info *);
static void dmfe_init_dm910x(struct DEVICE *);
static unsigned long cal_CRC(unsigned char *, unsigned int, u8);
static void dmfe_parse_srom(struct dmfe_board_info *);
static void dmfe_program_DM9801(struct dmfe_board_info *, int);
static void dmfe_program_DM9802(struct dmfe_board_info *);
static void dmfe_HPNA_remote_cmd_chk(struct dmfe_board_info * );
static void dmfe_set_phyxcer(struct dmfe_board_info *);

/* DM910X network baord routine ---------------------------- */

/*
 *	Search DM910X board ,allocate space and register it
 */

static int __devinit dmfe_init_one (struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	unsigned long pci_iobase;
	u8 pci_irqline;
	struct dmfe_board_info *db;	/* board information structure */
	int i;
	struct net_device *dev;
	u32 dev_rev, pci_pmr;

	if (!printed_version++)
		printk(version);

	DMFE_DBUG(0, "dmfe_init_one()", 0);

	/* Enable Master/IO access, Disable memory access */
	i = pci_enable_device(pdev);
	if (i)
		return i;
	pci_set_master(pdev);

	pci_iobase = pci_resource_start(pdev, 0);
	pci_irqline = pdev->irq;

	/* iobase check */
	if (pci_iobase == 0) {
		printk(KERN_ERR "<DMFE>: I/O base is zero\n");
		return -ENODEV;
	}

#if 0	/* pci_{enable_device,set_master} sets minimum latency for us now */

	/* Set Latency Timer 80h */
	/* FIXME: setting values > 32 breaks some SiS 559x stuff.
	   Need a PCI quirk.. */

	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x80);
#endif

	/* Read Chip revision */
	pci_read_config_dword(pdev, PCI_REVISION_ID, &dev_rev);

	/* Init network device */
	dev = alloc_etherdev(sizeof(*db));
	if (dev == NULL)
		return -ENOMEM;
	SET_MODULE_OWNER(dev);

	/* IO range check */
	if (pci_resource_len (pdev, 0) < (CHK_IO_SIZE(pdev, dev_rev)) ) {
		printk(KERN_ERR "<DMFE>: Allocated I/O size too small\n");
		goto err_out;
	}

	if (!request_region(pci_iobase,
			    pci_resource_len (pdev, 0),
			    dev->name)) {
		printk(KERN_ERR "<DMFE>: I/O conflict : IO=%lx Range=%x\n",
		       pci_iobase, CHK_IO_SIZE(pdev, dev_rev));
		goto err_out;
	}

	/* Init system & device */
	db = dev->priv;

	/* Allocated Tx/Rx descriptor memory */
	db->desc_pool_ptr = pci_alloc_consistent(pdev, sizeof(struct tx_desc) * DESC_ALL_CNT + 0x20, &db->desc_pool_dma_ptr);
	db->buf_pool_ptr = pci_alloc_consistent(pdev, TX_BUF_ALLOC * TX_DESC_CNT + 4, &db->buf_pool_dma_ptr);

	db->first_tx_desc = (struct tx_desc *)db->desc_pool_ptr;
	db->first_tx_desc_dma = db->desc_pool_dma_ptr;
	db->buf_pool_start = db->buf_pool_ptr;
	db->buf_pool_dma_start = db->buf_pool_dma_ptr;

	pdev->driver_data = dev;

	db->chip_id = ent->driver_data;
	db->ioaddr = pci_iobase;
	db->chip_revision = dev_rev;

	db->net_dev = pdev;

	dev->base_addr = pci_iobase;
	dev->irq = pci_irqline;
	pci_set_drvdata(pdev, dev);
	dev->open = &dmfe_open;
	dev->hard_start_xmit = &dmfe_start_xmit;
	dev->stop = &dmfe_stop;
	dev->get_stats = &dmfe_get_stats;
	dev->set_multicast_list = &dmfe_set_filter_mode;
	dev->do_ioctl = &dmfe_do_ioctl;
	spin_lock_init(&db->lock);

	pci_read_config_dword(pdev, 0x50, &pci_pmr);
	pci_pmr &= 0x70000;
	if ( (pci_pmr == 0x10000) && (dev_rev == 0x02000031) )
		db->chip_type = 1;	/* DM9102A E3 */
	else
		db->chip_type = 0;

	/* read 64 word srom data */
	for (i = 0; i < 64; i++)
		((u16 *) db->srom)[i] = read_srom_word(pci_iobase, i);

	/* Set Node address */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = db->srom[20 + i];

	i = register_netdev (dev);
	if (i) goto err_out;

	printk(KERN_INFO "%s: Davicom DM%04lx at 0x%lx,",
	       dev->name,
	       ent->driver_data >> 16,
	       pci_iobase);
	for (i = 0; i < 6; i++)
		printk("%c%02x", i ? ':' : ' ', dev->dev_addr[i]);
	printk(", IRQ %d\n", pci_irqline);

	return 0;

err_out:
	pci_set_drvdata(pdev, NULL);
	kfree(dev);
	return -ENODEV;
}


static void __exit dmfe_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct dmfe_board_info *db = dev->priv;

	DMFE_DBUG(0, "dmfe_remove_one()", 0);

 	if (dev) {
		pci_free_consistent(db->net_dev, sizeof(struct tx_desc) *
					DESC_ALL_CNT + 0x20, db->desc_pool_ptr,
 					db->desc_pool_dma_ptr);
		pci_free_consistent(db->net_dev, TX_BUF_ALLOC * TX_DESC_CNT + 4,
					db->buf_pool_ptr, db->buf_pool_dma_ptr);
		unregister_netdev(dev);
		release_region(dev->base_addr,
				CHK_IO_SIZE(pdev, db->chip_revision));
		kfree(dev);	/* free board information */
		pci_set_drvdata(pdev, NULL);
	}

	DMFE_DBUG(0, "dmfe_remove_one() exit", 0);
}


/*
 *	Open the interface.
 *	The interface is opened whenever "ifconfig" actives it.
 */

static int dmfe_open(struct DEVICE *dev)
{
	int ret;
	struct dmfe_board_info *db = dev->priv;

	DMFE_DBUG(0, "dmfe_open", 0);

	ret =  request_irq(dev->irq, &dmfe_interrupt, SA_SHIRQ, dev->name, dev);
	if (ret)
		return ret;

	/* system variable init */
	db->cr6_data = CR6_DEFAULT | dmfe_cr6_user_set;
	db->tx_packet_cnt = 0;
	db->tx_queue_cnt = 0;
	db->rx_avail_cnt = 0;
	db->link_failed = 1;
	db->wait_reset = 0;

	db->first_in_callback = 0;
	db->NIC_capability = 0xf;	/* All capability*/
	db->PHY_reg4 = 0x1e0;

	/* CR6 operation mode decision */
	if ( !chkmode || (db->chip_id == PCI_DM9132_ID) ||
		(db->chip_revision >= 0x02000030) ) {
    		db->cr6_data |= DMFE_TXTH_256;
		db->cr0_data = CR0_DEFAULT;
		db->dm910x_chk_mode=4;		/* Enter the normal mode */
 	} else {
		db->cr6_data |= CR6_SFT;	/* Store & Forward mode */
		db->cr0_data = 0;
		db->dm910x_chk_mode = 1;	/* Enter the check mode */
	}

	/* Initilize DM910X board */
	dmfe_init_dm910x(dev);

	/* Active System Interface */
	netif_wake_queue(dev);

	/* set and active a timer process */
	init_timer(&db->timer);
	db->timer.expires = DMFE_TIMER_WUT + HZ * 2;
	db->timer.data = (unsigned long)dev;
	db->timer.function = &dmfe_timer;
	add_timer(&db->timer);

	return 0;
}


/*	Initilize DM910X board
 *	Reset DM910X board
 *	Initilize TX/Rx descriptor chain structure
 *	Send the set-up frame
 *	Enable Tx/Rx machine
 */

static void dmfe_init_dm910x(struct DEVICE *dev)
{
	struct dmfe_board_info *db = dev->priv;
	unsigned long ioaddr = db->ioaddr;

	DMFE_DBUG(0, "dmfe_init_dm910x()", 0);

	/* Reset DM910x MAC controller */
	outl(DM910X_RESET, ioaddr + DCR0);	/* RESET MAC */
	udelay(100);
	outl(db->cr0_data, ioaddr + DCR0);
	udelay(5);

	/* Phy addr : DM910(A)2/DM9132/9801, phy address = 1 */
	db->phy_addr = 1;

	/* Parser SROM and media mode */
	dmfe_parse_srom(db);
	db->media_mode = dmfe_media_mode;

	/* RESET Phyxcer Chip by GPR port bit 7 */
	outl(0x180, ioaddr + DCR12);		/* Let bit 7 output port */
	if (db->chip_id == PCI_DM9009_ID) {
		outl(0x80, ioaddr + DCR12);	/* Issue RESET signal */
		mdelay(300);			/* Delay 300 ms */
	}
	outl(0x0, ioaddr + DCR12);	/* Clear RESET signal */

	/* Process Phyxcer Media Mode */
	if ( !(db->media_mode & 0x10) )	/* Force 1M mode */
		dmfe_set_phyxcer(db);

	/* Media Mode Process */
	if ( !(db->media_mode & DMFE_AUTO) )
		db->op_mode = db->media_mode; 	/* Force Mode */

	/* Initiliaze Transmit/Receive decriptor and CR3/4 */
	dmfe_descriptor_init(db, ioaddr);

	/* Init CR6 to program DM910x operation */
	update_cr6(db->cr6_data, ioaddr);

	/* Send setup frame */
	if (db->chip_id == PCI_DM9132_ID)
		dm9132_id_table(dev, dev->mc_count);	/* DM9132 */
	else
		send_filter_frame(dev, dev->mc_count);	/* DM9102/DM9102A */

	/* Init CR7, interrupt active bit */
	db->cr7_data = CR7_DEFAULT;
	outl(db->cr7_data, ioaddr + DCR7);

	/* Init CR15, Tx jabber and Rx watchdog timer */
	outl(db->cr15_data, ioaddr + DCR15);

	/* Enable DM910X Tx/Rx function */
	db->cr6_data |= CR6_RXSC | CR6_TXSC | 0x40000;
	update_cr6(db->cr6_data, ioaddr);
}


/*
 *	Hardware start transmission.
 *	Send a packet to media from the upper layer.
 */

static int dmfe_start_xmit(struct sk_buff *skb, struct DEVICE *dev)
{
	struct dmfe_board_info *db = dev->priv;
	struct tx_desc *txptr;
	unsigned long flags;

	DMFE_DBUG(0, "dmfe_start_xmit", 0);

	/* Resource flag check */
	netif_stop_queue(dev);

	/* Too large packet check */
	if (skb->len > MAX_PACKET_SIZE) {
		printk(KERN_ERR "<DMFE>: big packet = %d\n", (u16)skb->len);
		dev_kfree_skb(skb);
		return 0;
	}

	spin_lock_irqsave(&db->lock, flags);

	/* No Tx resource check, it never happen nromally */
	if (db->tx_queue_cnt >= TX_FREE_DESC_CNT) {
		spin_unlock_irqrestore(&db->lock, flags);
		printk(KERN_ERR "<DMFE>: No Tx resource %ld\n", db->tx_queue_cnt);
		return 1;
	}

	/* Disable NIC interrupt */
	outl(0, dev->base_addr + DCR7);

	/* transmit this packet */
	txptr = db->tx_insert_ptr;
	memcpy( (char *) txptr->tx_buf_ptr, (char *) skb->data, skb->len);
	txptr->tdes1 = cpu_to_le32(0xe1000000 | skb->len);

	/* Point to next transmit free descriptor */
	db->tx_insert_ptr = (struct tx_desc *) txptr->next_tx_desc;

	/* Transmit Packet Process */
	if ( (!db->tx_queue_cnt) && (db->tx_packet_cnt < TX_MAX_SEND_CNT) ) {
		txptr->tdes0 = cpu_to_le32(0x80000000);	/* Set owner bit */
		db->tx_packet_cnt++;			/* Ready to send */
		outl(0x1, dev->base_addr + DCR1);	/* Issue Tx polling */
		dev->trans_start = jiffies;		/* saved time stamp */
	} else {
		db->tx_queue_cnt++;			/* queue TX packet */
		outl(0x1, dev->base_addr + DCR1);	/* Issue Tx polling */
	}

	/* Tx resource check */
	if ( db->tx_queue_cnt < TX_FREE_DESC_CNT )
		netif_wake_queue(dev);

	/* free this SKB */
	dev_kfree_skb(skb);

	/* Restore CR7 to enable interrupt */
	spin_unlock_irqrestore(&db->lock, flags);
	outl(db->cr7_data, dev->base_addr + DCR7);

	return 0;
}


/*
 *	Stop the interface.
 *	The interface is stopped when it is brought.
 */

static int dmfe_stop(struct DEVICE *dev)
{
	struct dmfe_board_info *db = dev->priv;
	unsigned long ioaddr = dev->base_addr;

	DMFE_DBUG(0, "dmfe_stop", 0);

	/* disable system */
	netif_stop_queue(dev);

	/* deleted timer */
	del_timer_sync(&db->timer);

	/* Reset & stop DM910X board */
	outl(DM910X_RESET, ioaddr + DCR0);
	udelay(5);
	phy_write(db->ioaddr, db->phy_addr, 0, 0x8000, db->chip_id);

	/* free interrupt */
	free_irq(dev->irq, dev);

	/* free allocated rx buffer */
	dmfe_free_rxbuffer(db);

#if 0
	/* show statistic counter */
	printk("<DMFE>: FU:%lx EC:%lx LC:%lx NC:%lx LOC:%lx TXJT:%lx RESET:%lx RCR8:%lx FAL:%lx TT:%lx\n",
		db->tx_fifo_underrun, db->tx_excessive_collision,
		db->tx_late_collision, db->tx_no_carrier, db->tx_loss_carrier,
		db->tx_jabber_timeout, db->reset_count, db->reset_cr8,
		db->reset_fatal, db->reset_TXtimeout);
#endif

	return 0;
}


/*
 *	DM9102 insterrupt handler
 *	receive the packet to upper layer, free the transmitted packet
 */

static void dmfe_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct DEVICE *dev = dev_id;
	struct dmfe_board_info *db = (struct dmfe_board_info *) dev->priv;
	unsigned long ioaddr = dev->base_addr;
	unsigned long flags;

	DMFE_DBUG(0, "dmfe_interrupt()", 0);

	if (!dev) {
		DMFE_DBUG(1, "dmfe_interrupt() without DEVICE arg", 0);
		return;
	}

	spin_lock_irqsave(&db->lock, flags);

	/* Got DM910X status */
	db->cr5_data = inl(ioaddr + DCR5);
	outl(db->cr5_data, ioaddr + DCR5);
	if ( !(db->cr5_data & 0xc1) ) {
		spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	/* Disable all interrupt in CR7 to solve the interrupt edge problem */
	outl(0, ioaddr + DCR7);

	/* Check system status */
	if (db->cr5_data & 0x2000) {
		/* system bus error happen */
		DMFE_DBUG(1, "System bus error happen. CR5=", db->cr5_data);
		db->reset_fatal++;
		db->wait_reset = 1;	/* Need to RESET */
		spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	 /* Received the coming packet */
	if ( (db->cr5_data & 0x40) && db->rx_avail_cnt )
		dmfe_rx_packet(dev, db);

	/* reallocated rx descriptor buffer */
	if (db->rx_avail_cnt<RX_DESC_CNT)
		allocated_rx_buffer(db);

	/* Free the transmitted descriptor */
	if ( db->cr5_data & 0x01)
		dmfe_free_tx_pkt(dev, db);

	/* Mode Check */
	if (db->dm910x_chk_mode & 0x2) {
		db->dm910x_chk_mode = 0x4;
		db->cr6_data |= 0x100;
		update_cr6(db->cr6_data, db->ioaddr);
	}

	/* Restore CR7 to enable interrupt mask */
	outl(db->cr7_data, ioaddr + DCR7);

	spin_unlock_irqrestore(&db->lock, flags);
}


/*
 *	Free TX resource after TX complete
 */

static void dmfe_free_tx_pkt(struct DEVICE *dev, struct dmfe_board_info * db)
{
	struct tx_desc *txptr;
	unsigned long ioaddr = dev->base_addr;

	txptr = db->tx_remove_ptr;
	while(db->tx_packet_cnt) {
		/* printk("<DMFE>: tdes0=%x\n", txptr->tdes0); */
		if (txptr->tdes0 & 0x80000000)
			break;

		/* A packet sent completed */
		db->tx_packet_cnt--;
		db->stats.tx_packets++;

		/* Transmit statistic counter */
		if ( txptr->tdes0 != 0x7fffffff ) {
			/* printk("<DMFE>: tdes0=%x\n", txptr->tdes0); */
			db->stats.collisions += (txptr->tdes0 >> 3) & 0xf;
			db->stats.tx_bytes += txptr->tdes1 & 0x7ff;
			if (txptr->tdes0 & TDES0_ERR_MASK) {
				db->stats.tx_errors++;

				if (txptr->tdes0 & 0x0002) {	/* UnderRun */
					db->tx_fifo_underrun++;
					if ( !(db->cr6_data & CR6_SFT) ) {
						db->cr6_data = db->cr6_data | CR6_SFT;
						update_cr6(db->cr6_data, db->ioaddr);
					}
				}
				if (txptr->tdes0 & 0x0100)
					db->tx_excessive_collision++;
				if (txptr->tdes0 & 0x0200)
					db->tx_late_collision++;
				if (txptr->tdes0 & 0x0400)
					db->tx_no_carrier++;
				if (txptr->tdes0 & 0x0800)
					db->tx_loss_carrier++;
				if (txptr->tdes0 & 0x4000)
					db->tx_jabber_timeout++;
			}
		}

    		txptr = (struct tx_desc *) txptr->next_tx_desc;
	}/* End of while */

	/* Update TX remove pointer to next */
	db->tx_remove_ptr = (struct tx_desc *) txptr;

	/* Send the Tx packet in queue */
	if ( (db->tx_packet_cnt < TX_MAX_SEND_CNT) && db->tx_queue_cnt ) {
		txptr->tdes0 = cpu_to_le32(0x80000000);	/* Set owner bit */
		db->tx_packet_cnt++;			/* Ready to send */
		db->tx_queue_cnt--;
		outl(0x1, ioaddr + DCR1);		/* Issue Tx polling */
		dev->trans_start = jiffies;		/* saved time stamp */
	}

	/* Resource available check */
	if ( db->tx_queue_cnt < TX_WAKE_DESC_CNT )
		netif_wake_queue(dev);	/* Active upper layer, send again */
}


/*
 *	Receive the come packet and pass to upper layer
 */

static void dmfe_rx_packet(struct DEVICE *dev, struct dmfe_board_info * db)
{
	struct rx_desc *rxptr;
	struct sk_buff *skb;
	int rxlen;

	rxptr = db->rx_ready_ptr;

	while(db->rx_avail_cnt) {
		if (rxptr->rdes0 & 0x80000000)	/* packet owner check */
			break;

		db->rx_avail_cnt--;
		db->interval_rx_cnt++;

		if ( (rxptr->rdes0 & 0x300) != 0x300) {
			/* A packet without First/Last flag */
			/* reused this SKB */
			DMFE_DBUG(0, "Reused SK buffer, rdes0", rxptr->rdes0);
			dmfe_reused_skb(db, (struct sk_buff *) rxptr->rx_skb_ptr);
		} else {
			/* A packet with First/Last flag */
			rxlen = ( (rxptr->rdes0 >> 16) & 0x3fff) - 4;

			/* error summary bit check */
			if (rxptr->rdes0 & 0x8000) {
				/* This is a error packet */
				//printk("<DMFE>: rdes0: %lx\n", rxptr->rdes0);
				db->stats.rx_errors++;
				if (rxptr->rdes0 & 1)
					db->stats.rx_fifo_errors++;
				if (rxptr->rdes0 & 2)
					db->stats.rx_crc_errors++;
				if (rxptr->rdes0 & 0x80)
					db->stats.rx_length_errors++;
			}

			if ( !(rxptr->rdes0 & 0x8000) ||
				((db->cr6_data & CR6_PM) && (rxlen>6)) ) {
				skb = (struct sk_buff *) rxptr->rx_skb_ptr;

				/* Received Packet CRC check need or not */
				if ( (db->dm910x_chk_mode & 1) &&
					(cal_CRC(skb->tail, rxlen, 1) !=
					(*(unsigned long *) (skb->tail+rxlen) )
					) ) {
					/* Found a error received packet */
					dmfe_reused_skb(db, (struct sk_buff *) rxptr->rx_skb_ptr);
					db->dm910x_chk_mode = 3;
				} else {
					/* Good packet, send to upper layer */
					/* Shorst packet used new SKB */
					if ( (rxlen < RX_COPY_SIZE) &&
						( (skb = dev_alloc_skb(rxlen + 2) )
						!= NULL) ) {
						/* size less than COPY_SIZE, allocated a rxlen SKB */
						skb->dev = dev;
						skb_reserve(skb, 2); /* 16byte align */
						memcpy(skb_put(skb, rxlen), ((struct sk_buff *) rxptr->rx_skb_ptr)->tail, rxlen);
						dmfe_reused_skb(db, (struct sk_buff *) rxptr->rx_skb_ptr);
					} else {
						skb->dev = dev;
						skb_put(skb, rxlen);
					}
					skb->protocol = eth_type_trans(skb, dev);
					netif_rx(skb);
					dev->last_rx = jiffies;
					db->stats.rx_packets++;
					db->stats.rx_bytes += rxlen;
				}
			} else {
				/* Reuse SKB buffer when the packet is error */
				DMFE_DBUG(0, "Reused SK buffer, rdes0", rxptr->rdes0);
				dmfe_reused_skb(db, (struct sk_buff *) rxptr->rx_skb_ptr);
			}
		}

		rxptr = (struct rx_desc *) rxptr->next_rx_desc;
	}

	db->rx_ready_ptr = rxptr;
}


/*
 *	Get statistics from driver.
 */

static struct net_device_stats * dmfe_get_stats(struct DEVICE *dev)
{
	struct dmfe_board_info *db = (struct dmfe_board_info *)dev->priv;

	DMFE_DBUG(0, "dmfe_get_stats", 0);
	return &db->stats;
}


/*
 * Set DM910X multicast address
 */

static void dmfe_set_filter_mode(struct DEVICE * dev)
{
	struct dmfe_board_info *db = dev->priv;
	unsigned long flags;

	DMFE_DBUG(0, "dmfe_set_filter_mode()", 0);
	spin_lock_irqsave(&db->lock, flags);

	if (dev->flags & IFF_PROMISC) {
		DMFE_DBUG(0, "Enable PROM Mode", 0);
		db->cr6_data |= CR6_PM | CR6_PBF;
		update_cr6(db->cr6_data, db->ioaddr);
		spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	if (dev->flags & IFF_ALLMULTI || dev->mc_count > DMFE_MAX_MULTICAST) {
		DMFE_DBUG(0, "Pass all multicast address", dev->mc_count);
		db->cr6_data &= ~(CR6_PM | CR6_PBF);
		db->cr6_data |= CR6_PAM;
		spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	DMFE_DBUG(0, "Set multicast address", dev->mc_count);
	if (db->chip_id == PCI_DM9132_ID)
		dm9132_id_table(dev, dev->mc_count);	/* DM9132 */
	else
		send_filter_frame(dev, dev->mc_count); 	/* DM9102/DM9102A */
	spin_unlock_irqrestore(&db->lock, flags);
}


/*
 *	Process the upper socket ioctl command
 */

static int dmfe_do_ioctl(struct DEVICE *dev, struct ifreq *ifr, int cmd)
{
	DMFE_DBUG(0, "dmfe_do_ioctl()", 0);
	return 0;
}


/*
 *	A periodic timer routine
 *	Dynamic media sense, allocated Rx buffer...
 */

static void dmfe_timer(unsigned long data)
{
	u32 tmp_cr8;
	unsigned char tmp_cr12;
	struct DEVICE *dev = (struct DEVICE *) data;
	struct dmfe_board_info *db = (struct dmfe_board_info *) dev->priv;
 	unsigned long flags;

	DMFE_DBUG(0, "dmfe_timer()", 0);
	spin_lock_irqsave(&db->lock, flags);

	/* Media mode process when Link OK before enter this route */
	if (db->first_in_callback == 0) {
		db->first_in_callback = 1;
		if (db->chip_type && (db->chip_id==PCI_DM9102_ID)) {
			db->cr6_data &= ~0x40000;
			update_cr6(db->cr6_data, db->ioaddr);
			phy_write(db->ioaddr, db->phy_addr, 0, 0x1000, db->chip_id);
			db->cr6_data |= 0x40000;
			update_cr6(db->cr6_data, db->ioaddr);
			db->timer.expires = DMFE_TIMER_WUT + HZ * 2;
			add_timer(&db->timer);
			spin_unlock_irqrestore(&db->lock, flags);
			return;
		}
	}


	/* Operating Mode Check */
	if ( (db->dm910x_chk_mode & 0x1) &&
		(db->stats.rx_packets > MAX_CHECK_PACKET) )
		db->dm910x_chk_mode = 0x4;

	/* Dynamic reset DM910X : system error or transmit time-out */
	tmp_cr8 = inl(db->ioaddr + DCR8);
	if ( (db->interval_rx_cnt==0) && (tmp_cr8) ) {
		db->reset_cr8++;
		db->wait_reset = 1;
	}
	db->interval_rx_cnt = 0;

	/* TX polling kick monitor */
	if ( db->tx_packet_cnt &&
		((jiffies - dev->trans_start) > DMFE_TX_KICK) ) {
		outl(0x1, dev->base_addr + DCR1);   /* Tx polling again */

		/* TX Timeout */
		if ( (jiffies - dev->trans_start) > DMFE_TX_TIMEOUT ) {
			db->reset_TXtimeout++;
			db->wait_reset = 1;
			printk(KERN_WARNING "%s: Tx timeout - resetting\n",
			       dev->name);
		}
	}

	if (db->wait_reset) {
		DMFE_DBUG(0, "Dynamic Reset device", db->tx_packet_cnt);
		db->reset_count++;
		dmfe_dynamic_reset(dev);
		db->first_in_callback = 0;
		db->timer.expires = DMFE_TIMER_WUT;
		add_timer(&db->timer);
		spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	/* Link status check, Dynamic media type change */
	if (db->chip_id == PCI_DM9132_ID)
		tmp_cr12 = inb(db->ioaddr + DCR9 + 3);	/* DM9132 */
	else
		tmp_cr12 = inb(db->ioaddr + DCR12);	/* DM9102/DM9102A */

	if ( ((db->chip_id == PCI_DM9102_ID) &&
		(db->chip_revision == 0x02000030)) ||
		((db->chip_id == PCI_DM9132_ID) &&
		(db->chip_revision == 0x02000010)) ) {
		/* DM9102A Chip */
		if (tmp_cr12 & 2)
			tmp_cr12 = 0x0;		/* Link failed */
		else
			tmp_cr12 = 0x3;	/* Link OK */
	}

	if ( !(tmp_cr12 & 0x3) && !db->link_failed ) {
		/* Link Failed */
		DMFE_DBUG(0, "Link Failed", tmp_cr12);
		db->link_failed = 1;

		/* For Force 10/100M Half/Full mode: Enable Auto-Nego mode */
		/* AUTO or force 1M Homerun/Longrun don't need */
		if ( !(db->media_mode & 0x38) )
			phy_write(db->ioaddr, db->phy_addr, 0, 0x1000, db->chip_id);

		/* AUTO mode, if INT phyxcer link failed, select EXT device */
		if (db->media_mode & DMFE_AUTO) {
			/* 10/100M link failed, used 1M Home-Net */
			db->cr6_data|=0x00040000;	/* bit18=1, MII */
			db->cr6_data&=~0x00000200;	/* bit9=0, HD mode */
			update_cr6(db->cr6_data, db->ioaddr);
		}
	} else
		if ((tmp_cr12 & 0x3) && db->link_failed) {
			DMFE_DBUG(0, "Link link OK", tmp_cr12);
			db->link_failed = 0;

			/* Auto Sense Speed */
			if ( (db->media_mode & DMFE_AUTO) &&
				dmfe_sense_speed(db) )
				db->link_failed = 1;
			dmfe_process_mode(db);
			/* SHOW_MEDIA_TYPE(db->op_mode); */
		}

	/* HPNA remote command check */
	if (db->HPNA_command & 0xf00) {
		db->HPNA_timer--;
		if (!db->HPNA_timer)
			dmfe_HPNA_remote_cmd_chk(db);
	}

	/* Timer active again */
	db->timer.expires = DMFE_TIMER_WUT;
	add_timer(&db->timer);
	spin_unlock_irqrestore(&db->lock, flags);
}


/*
 *	Dynamic reset the DM910X board
 *	Stop DM910X board
 *	Free Tx/Rx allocated memory
 *	Reset DM910X board
 *	Re-initilize DM910X board
 */

static void dmfe_dynamic_reset(struct DEVICE *dev)
{
	struct dmfe_board_info *db = dev->priv;

	DMFE_DBUG(0, "dmfe_dynamic_reset()", 0);

	/* Sopt MAC controller */
	db->cr6_data &= ~(CR6_RXSC | CR6_TXSC);	/* Disable Tx/Rx */
	update_cr6(db->cr6_data, dev->base_addr);
	outl(0, dev->base_addr + DCR7);		/* Disable Interrupt */
	outl(inl(dev->base_addr + DCR5), dev->base_addr + DCR5);

	/* Disable upper layer interface */
	netif_stop_queue(dev);

	/* Free Rx Allocate buffer */
	dmfe_free_rxbuffer(db);

	/* system variable init */
	db->tx_packet_cnt = 0;
	db->tx_queue_cnt = 0;
	db->rx_avail_cnt = 0;
	db->link_failed = 1;
	db->wait_reset = 0;

	/* Re-initilize DM910X board */
	dmfe_init_dm910x(dev);

	/* Restart upper layer interface */
	netif_wake_queue(dev);
}


/*
 *	free all allocated rx buffer
 */

static void dmfe_free_rxbuffer(struct dmfe_board_info * db)
{
	DMFE_DBUG(0, "dmfe_free_rxbuffer()", 0);

	/* free allocated rx buffer */
	while (db->rx_avail_cnt) {
		dev_kfree_skb( (void *) (db->rx_ready_ptr->rx_skb_ptr) );
		db->rx_ready_ptr = (struct rx_desc *) db->rx_ready_ptr->next_rx_desc;
		db->rx_avail_cnt--;
	}
}


/*
 *	Reused the SK buffer
 */

static void dmfe_reused_skb(struct dmfe_board_info *db, struct sk_buff * skb)
{
	struct rx_desc *rxptr = db->rx_insert_ptr;

	if (!(rxptr->rdes0 & 0x80000000)) {
		rxptr->rx_skb_ptr = (u32) skb;
		rxptr->rdes2 = cpu_to_le32( pci_map_single(db->net_dev, skb->tail, RX_ALLOC_SIZE, PCI_DMA_FROMDEVICE) );
		rxptr->rdes0 = cpu_to_le32(0x80000000);
		db->rx_avail_cnt++;
		db->rx_insert_ptr = (struct rx_desc *) rxptr->next_rx_desc;
	} else
		DMFE_DBUG(0, "SK Buffer reused method error", db->rx_avail_cnt);
}


/*
 *	Initialize transmit/Receive descriptor
 *	Using Chain structure, and allocated Tx/Rx buffer
 */

static void dmfe_descriptor_init(struct dmfe_board_info *db, unsigned long ioaddr)
{
	struct tx_desc *tmp_tx;
	struct rx_desc *tmp_rx;
	unsigned char *tmp_buf;
	dma_addr_t tmp_tx_dma, tmp_rx_dma;
	dma_addr_t tmp_buf_dma;
	int i;

	DMFE_DBUG(0, "dmfe_descriptor_init()", 0);

	/* tx descriptor start pointer */
	db->tx_insert_ptr = db->first_tx_desc;
	db->tx_remove_ptr = db->first_tx_desc;
	outl(db->first_tx_desc_dma, ioaddr + DCR4);     /* TX DESC address */

	/* rx descriptor start pointer */
	db->first_rx_desc = (struct rx_desc *) ( (u32) db->first_tx_desc + sizeof(struct rx_desc) * TX_DESC_CNT );
	db->first_rx_desc_dma = ( db->first_tx_desc_dma + sizeof(struct rx_desc) * TX_DESC_CNT);
	db->rx_insert_ptr = db->first_rx_desc;
	db->rx_ready_ptr = db->first_rx_desc;
	outl(db->first_rx_desc_dma, ioaddr + DCR3);	/* RX DESC address */

	/* Init Transmit chain */
	tmp_buf = db->buf_pool_start;
	tmp_buf_dma = db->buf_pool_dma_start;
	tmp_tx_dma = db->first_tx_desc_dma;
	for (tmp_tx = db->first_tx_desc, i = 0; i < TX_DESC_CNT; i++, tmp_tx++) {
		tmp_tx->tx_buf_ptr = (u32) tmp_buf;
		tmp_tx->tdes0 = cpu_to_le32(0);
		tmp_tx->tdes1 = cpu_to_le32(0x81000000);	/* IC, chain */
		tmp_tx->tdes2 = cpu_to_le32(tmp_buf_dma);
		tmp_tx_dma += sizeof(struct tx_desc);
		tmp_tx->tdes3 = cpu_to_le32(tmp_tx_dma);
		tmp_tx->next_tx_desc = (u32) ((u32) tmp_tx + sizeof(struct tx_desc));
		tmp_buf = (unsigned char *) ((u32) tmp_buf + TX_BUF_ALLOC);
		tmp_buf_dma = tmp_buf_dma + TX_BUF_ALLOC;
	}
	(--tmp_tx)->tdes3 = cpu_to_le32(db->first_tx_desc_dma);
	tmp_tx->next_tx_desc = (u32) db->first_tx_desc;

	 /* Init Receive descriptor chain */
	tmp_rx_dma=db->first_rx_desc_dma;
	for (tmp_rx = db->first_rx_desc, i = 0; i < RX_DESC_CNT; i++, tmp_rx++) {
		tmp_rx->rdes0 = cpu_to_le32(0);
		tmp_rx->rdes1 = cpu_to_le32(0x01000600);
		tmp_rx_dma += sizeof(struct rx_desc);
		tmp_rx->rdes3 = cpu_to_le32(tmp_rx_dma);
		tmp_rx->next_rx_desc = (u32) ((u32) tmp_rx + sizeof(struct rx_desc));
	}
	(--tmp_rx)->rdes3 = cpu_to_le32(db->first_rx_desc_dma);
	tmp_rx->next_rx_desc = (u32) db->first_rx_desc;

	/* pre-allocated Rx buffer */
	allocated_rx_buffer(db);
}


/*
 *	Update CR6 value
 *	Firstly stop DM910X , then written value and start
 */

static void update_cr6(u32 cr6_data, unsigned long ioaddr)
{
	u32 cr6_tmp;

	cr6_tmp = cr6_data & ~0x2002;           /* stop Tx/Rx */
	outl(cr6_tmp, ioaddr + DCR6);
	udelay(5);
	outl(cr6_data, ioaddr + DCR6);
	udelay(5);
}


/*
 *	Send a setup frame for DM9132
 *	This setup frame initilize DM910X addres filter mode
*/

static void dm9132_id_table(struct DEVICE *dev, int mc_cnt)
{
	struct dev_mc_list *mcptr;
	u16 * addrptr;
	unsigned long ioaddr = dev->base_addr+0xc0;		/* ID Table */
	u32 hash_val;
	u16 i, hash_table[4];

	DMFE_DBUG(0, "dm9132_id_table()", 0);

	/* Node address */
	addrptr = (u16 *) dev->dev_addr;
	outw(addrptr[0], ioaddr);
	ioaddr += 4;
	outw(addrptr[1], ioaddr);
	ioaddr += 4;
	outw(addrptr[2], ioaddr);
	ioaddr += 4;

	/* Clear Hash Table */
	for (i = 0; i < 4; i++)
		hash_table[i] = 0x0;

	/* broadcast address */
	hash_table[3] = 0x8000;

	/* the multicast address in Hash Table : 64 bits */
	for (mcptr = dev->mc_list, i = 0; i < mc_cnt; i++, mcptr = mcptr->next) {
		hash_val = cal_CRC( (char *) mcptr->dmi_addr, 6, 0) & 0x3f;
		hash_table[hash_val / 16] |= (u16) 1 << (hash_val % 16);
	}

	/* Write the hash table to MAC MD table */
	for (i = 0; i < 4; i++, ioaddr += 4)
		outw(hash_table[i], ioaddr);
}


/*
 *	Send a setup frame for DM9102/DM9102A
 *	This setup frame initilize DM910X addres filter mode
 */

static void send_filter_frame(struct DEVICE *dev, int mc_cnt)
{
	struct dmfe_board_info *db = dev->priv;
	struct dev_mc_list *mcptr;
	struct tx_desc *txptr;
	u16 * addrptr;
	u32 * suptr;
	int i;

	DMFE_DBUG(0, "send_filter_frame()", 0);

	txptr = db->tx_insert_ptr;
	suptr = (u32 *) txptr->tx_buf_ptr;

	/* Node address */
	addrptr = (u16 *) dev->dev_addr;
	*suptr++ = addrptr[0];
	*suptr++ = addrptr[1];
	*suptr++ = addrptr[2];

	/* broadcast address */
	*suptr++ = 0xffff;
	*suptr++ = 0xffff;
	*suptr++ = 0xffff;

	/* fit the multicast address */
	for (mcptr = dev->mc_list, i = 0; i < mc_cnt; i++, mcptr = mcptr->next) {
		addrptr = (u16 *) mcptr->dmi_addr;
		*suptr++ = addrptr[0];
		*suptr++ = addrptr[1];
		*suptr++ = addrptr[2];
	}

	for (; i<14; i++) {
		*suptr++ = 0xffff;
		*suptr++ = 0xffff;
		*suptr++ = 0xffff;
	}

	/* prepare the setup frame */
	db->tx_insert_ptr = (struct tx_desc *) txptr->next_tx_desc;
	txptr->tdes1 = cpu_to_le32(0x890000c0);

	/* Resource Check and Send the setup packet */
	if (!db->tx_packet_cnt) {
		/* Resource Empty */
		db->tx_packet_cnt++;
		txptr->tdes0 = cpu_to_le32(0x80000000);
		update_cr6(db->cr6_data | 0x2000, dev->base_addr);
		outl(0x1, dev->base_addr + DCR1);	/* Issue Tx polling */
		update_cr6(db->cr6_data, dev->base_addr);
		dev->trans_start = jiffies;
	} else
		db->tx_queue_cnt++;	/* Put in TX queue */
}


/*
 *	Allocate rx buffer,
 *	As possible as allocated maxiumn Rx buffer
 */

static void allocated_rx_buffer(struct dmfe_board_info *db)
{
	struct rx_desc *rxptr;
	struct sk_buff *skb;

	rxptr = db->rx_insert_ptr;

	while(db->rx_avail_cnt < RX_DESC_CNT) {
		if ( ( skb = dev_alloc_skb(RX_ALLOC_SIZE) ) == NULL )
			break;
		rxptr->rx_skb_ptr = (u32) skb; /* FIXME */
		rxptr->rdes2 = cpu_to_le32( pci_map_single(db->net_dev, skb->tail, RX_ALLOC_SIZE, PCI_DMA_FROMDEVICE) );
		rxptr->rdes0 = cpu_to_le32(0x80000000);
		rxptr = (struct rx_desc *) rxptr->next_rx_desc;
		db->rx_avail_cnt++;
	}

	db->rx_insert_ptr = rxptr;
}


/*
 *	Read one word data from the serial ROM
 */

static u16 read_srom_word(long ioaddr, int offset)
{
	int i;
	u16 srom_data = 0;
	long cr9_ioaddr = ioaddr + DCR9;

	outl(CR9_SROM_READ, cr9_ioaddr);
	outl(CR9_SROM_READ | CR9_SRCS, cr9_ioaddr);

	/* Send the Read Command 110b */
	SROM_CLK_WRITE(SROM_DATA_1, cr9_ioaddr);
	SROM_CLK_WRITE(SROM_DATA_1, cr9_ioaddr);
	SROM_CLK_WRITE(SROM_DATA_0, cr9_ioaddr);

	/* Send the offset */
	for (i = 5; i >= 0; i--) {
		srom_data = (offset & (1 << i)) ? SROM_DATA_1 : SROM_DATA_0;
		SROM_CLK_WRITE(srom_data, cr9_ioaddr);
	}

	outl(CR9_SROM_READ | CR9_SRCS, cr9_ioaddr);

	for (i = 16; i > 0; i--) {
		outl(CR9_SROM_READ | CR9_SRCS | CR9_SRCLK, cr9_ioaddr);
		udelay(5);
		srom_data = (srom_data << 1) | ((inl(cr9_ioaddr) & CR9_CRDOUT) ? 1 : 0);
		outl(CR9_SROM_READ | CR9_SRCS, cr9_ioaddr);
		udelay(5);
	}

	outl(CR9_SROM_READ, cr9_ioaddr);
	return srom_data;
}


/*
 *	Auto sense the media mode
 */

static u8 dmfe_sense_speed(struct dmfe_board_info * db)
{
	u8 ErrFlag = 0;
	u16 phy_mode;

	/* CR6 bit18=0, select 10/100M */
	update_cr6( (db->cr6_data & ~0x40000), db->ioaddr);

	phy_mode = phy_read(db->ioaddr, db->phy_addr, 1, db->chip_id);
	phy_mode = phy_read(db->ioaddr, db->phy_addr, 1, db->chip_id);

	if ( (phy_mode & 0x24) == 0x24 ) {
		if (db->chip_id == PCI_DM9132_ID)	/* DM9132 */
			phy_mode = phy_read(db->ioaddr, db->phy_addr, 7, db->chip_id) & 0xf000;
		else 				/* DM9102/DM9102A */
			phy_mode = phy_read(db->ioaddr, db->phy_addr, 17, db->chip_id) & 0xf000;
		/* printk("<DMFE>: Phy_mode %x ",phy_mode); */
		switch (phy_mode) {
		case 0x1000: db->op_mode = DMFE_10MHF; break;
		case 0x2000: db->op_mode = DMFE_10MFD; break;
		case 0x4000: db->op_mode = DMFE_100MHF; break;
		case 0x8000: db->op_mode = DMFE_100MFD; break;
		default: db->op_mode = DMFE_10MHF;
			ErrFlag = 1;
			break;
		}
	} else {
		db->op_mode = DMFE_10MHF;
		DMFE_DBUG(0, "Link Failed :", phy_mode);
		ErrFlag = 1;
	}

	return ErrFlag;
}


/*
 *	Set 10/100 phyxcer capability
 *	AUTO mode : phyxcer register4 is NIC capability
 *	Force mode: phyxcer register4 is the force media
 */

static void dmfe_set_phyxcer(struct dmfe_board_info *db)
{
	u16 phy_reg;

	/* Select 10/100M phyxcer */
	db->cr6_data &= ~0x40000;
	update_cr6(db->cr6_data, db->ioaddr);

	/* DM9009 Chip: Phyxcer reg18 bit12=0 */
	if (db->chip_id == PCI_DM9009_ID) {
		phy_reg = phy_read(db->ioaddr, db->phy_addr, 18, db->chip_id) & ~0x1000;
		phy_write(db->ioaddr, db->phy_addr, 18, phy_reg, db->chip_id);
	}

	/* Phyxcer capability setting */
	phy_reg = phy_read(db->ioaddr, db->phy_addr, 4, db->chip_id) & ~0x01e0;

	if (db->media_mode & DMFE_AUTO) {
		/* AUTO Mode */
		phy_reg |= db->PHY_reg4;
	} else {
		/* Force Mode */
		switch(db->media_mode) {
		case DMFE_10MHF: phy_reg |= 0x20; break;
		case DMFE_10MFD: phy_reg |= 0x40; break;
		case DMFE_100MHF: phy_reg |= 0x80; break;
		case DMFE_100MFD: phy_reg |= 0x100; break;
		}
		if (db->chip_id == PCI_DM9009_ID) phy_reg &= 0x61;
	}

  	/* Write new capability to Phyxcer Reg4 */
	if ( !(phy_reg & 0x01e0)) {
		phy_reg|=db->PHY_reg4;
		db->media_mode|=DMFE_AUTO;
	}
	phy_write(db->ioaddr, db->phy_addr, 4, phy_reg, db->chip_id);

 	/* Restart Auto-Negotiation */
	if ( db->chip_type && (db->chip_id == PCI_DM9102_ID) )
		phy_write(db->ioaddr, db->phy_addr, 0, 0x1800, db->chip_id);
	if ( !db->chip_type )
		phy_write(db->ioaddr, db->phy_addr, 0, 0x1200, db->chip_id);
}


/*
 *	Process op-mode
 *	AUTO mode : PHY controller in Auto-negotiation Mode
 *	Force mode: PHY controller in force mode with HUB
 *			N-way force capability with SWITCH
 */

static void dmfe_process_mode(struct dmfe_board_info *db)
{
	u16 phy_reg;

	/* Full Duplex Mode Check */
	if (db->op_mode & 0x4)
		db->cr6_data |= CR6_FDM;	/* Set Full Duplex Bit */
	else
		db->cr6_data &= ~CR6_FDM;	/* Clear Full Duplex Bit */

	/* Transciver Selection */
	if (db->op_mode & 0x10)		/* 1M HomePNA */
		db->cr6_data |= 0x40000;/* External MII select */
	else
		db->cr6_data &= ~0x40000;/* Internal 10/100 transciver */

	update_cr6(db->cr6_data, db->ioaddr);

	/* 10/100M phyxcer force mode need */
	if ( !(db->media_mode & 0x18)) {
		/* Forece Mode */
		phy_reg = phy_read(db->ioaddr, db->phy_addr, 6, db->chip_id);
		if ( !(phy_reg & 0x1) ) {
			/* parter without N-Way capability */
			phy_reg = 0x0;
			switch(db->op_mode) {
			case DMFE_10MHF: phy_reg = 0x0; break;
			case DMFE_10MFD: phy_reg = 0x100; break;
			case DMFE_100MHF: phy_reg = 0x2000; break;
			case DMFE_100MFD: phy_reg = 0x2100; break;
			}
			phy_write(db->ioaddr, db->phy_addr, 0, phy_reg, db->chip_id);
       			if ( db->chip_type && (db->chip_id == PCI_DM9102_ID) )
				mdelay(20);
			phy_write(db->ioaddr, db->phy_addr, 0, phy_reg, db->chip_id);
		}
	}
}


/*
 *	Write a word to Phy register
 */

static void phy_write(unsigned long iobase, u8 phy_addr, u8 offset, u16 phy_data, u32 chip_id)
{
	u16 i;
	unsigned long ioaddr;

	if (chip_id == PCI_DM9132_ID) {
		ioaddr = iobase + 0x80 + offset * 4;
		outw(phy_data, ioaddr);
	} else {
		/* DM9102/DM9102A Chip */
		ioaddr = iobase + DCR9;

		/* Send 33 synchronization clock to Phy controller */
		for (i = 0; i < 35; i++)
			phy_write_1bit(ioaddr, PHY_DATA_1);

		/* Send start command(01) to Phy */
		phy_write_1bit(ioaddr, PHY_DATA_0);
		phy_write_1bit(ioaddr, PHY_DATA_1);

		/* Send write command(01) to Phy */
		phy_write_1bit(ioaddr, PHY_DATA_0);
		phy_write_1bit(ioaddr, PHY_DATA_1);

		/* Send Phy addres */
		for (i = 0x10; i > 0; i = i >> 1)
			phy_write_1bit(ioaddr, phy_addr & i ? PHY_DATA_1 : PHY_DATA_0);

		/* Send register addres */
		for (i = 0x10; i > 0; i = i >> 1)
			phy_write_1bit(ioaddr, offset & i ? PHY_DATA_1 : PHY_DATA_0);

		/* written trasnition */
		phy_write_1bit(ioaddr, PHY_DATA_1);
		phy_write_1bit(ioaddr, PHY_DATA_0);

		/* Write a word data to PHY controller */
		for ( i = 0x8000; i > 0; i >>= 1)
			phy_write_1bit(ioaddr, phy_data & i ? PHY_DATA_1 : PHY_DATA_0);
	}
}


/*
 *	Read a word data from phy register
 */

static u16 phy_read(unsigned long iobase, u8 phy_addr, u8 offset, u32 chip_id)
{
	int i;
	u16 phy_data;
	unsigned long ioaddr;

	if (chip_id == PCI_DM9132_ID) {
		/* DM9132 Chip */
		ioaddr = iobase + 0x80 + offset * 4;
		phy_data = inw(ioaddr);
	} else {
		/* DM9102/DM9102A Chip */
		ioaddr = iobase + DCR9;

		/* Send 33 synchronization clock to Phy controller */
		for (i = 0; i < 35; i++)
			phy_write_1bit(ioaddr, PHY_DATA_1);

		/* Send start command(01) to Phy */
		phy_write_1bit(ioaddr, PHY_DATA_0);
		phy_write_1bit(ioaddr, PHY_DATA_1);

		/* Send read command(10) to Phy */
		phy_write_1bit(ioaddr, PHY_DATA_1);
		phy_write_1bit(ioaddr, PHY_DATA_0);

		/* Send Phy addres */
		for (i = 0x10; i > 0; i = i >> 1)
			phy_write_1bit(ioaddr, phy_addr & i ? PHY_DATA_1 : PHY_DATA_0);

		/* Send register addres */
		for (i = 0x10; i > 0; i = i >> 1)
			phy_write_1bit(ioaddr, offset & i ? PHY_DATA_1 : PHY_DATA_0);

		/* Skip transition state */
		phy_read_1bit(ioaddr);

		/* read 16bit data */
		for (phy_data = 0, i = 0; i < 16; i++) {
			phy_data <<= 1;
			phy_data |= phy_read_1bit(ioaddr);
		}
	}

	return phy_data;
}


/*
 *	Write one bit data to Phy Controller
 */

static void phy_write_1bit(unsigned long ioaddr, u32 phy_data)
{
	outl(phy_data, ioaddr);			/* MII Clock Low */
	udelay(1);
	outl(phy_data | MDCLKH, ioaddr);	/* MII Clock High */
	udelay(1);
	outl(phy_data, ioaddr);			/* MII Clock Low */
	udelay(1);
}


/*
 *	Read one bit phy data from PHY controller
 */

static u16 phy_read_1bit(unsigned long ioaddr)
{
	u16 phy_data;

	outl(0x50000, ioaddr);
	udelay(1);
	phy_data = ( inl(ioaddr) >> 19 ) & 0x1;
	outl(0x40000, ioaddr);
	udelay(1);

	return phy_data;
}


/*
 *	Calculate the CRC valude of the Rx packet
 *	flag = 	1 : return the reverse CRC (for the received packet CRC)
 *		0 : return the normal CRC (for Hash Table index)
 */

unsigned long cal_CRC(unsigned char * Data, unsigned int Len, u8 flag)
{
	unsigned long Crc = 0xffffffff;

	while (Len--) {
		Crc = CrcTable[(Crc ^ *Data++) & 0xFF] ^ (Crc >> 8);
	}

	if (flag)
		return ~Crc;
	else
		return Crc;
}


/*
 *	Parser SROM and media mode
 */

static void dmfe_parse_srom(struct dmfe_board_info * db)
{
	char * srom = db->srom;
	int dmfe_mode, tmp_reg;

	DMFE_DBUG(0, "dmfe_parse_srom() ", 0);

	/* Init CR15 */
	db->cr15_data = CR15_DEFAULT;

	/* Check SROM Version */
	if ( ( (int) srom[18] & 0xff) == SROM_V41_CODE) {
		/* SROM V4.01 */
		/* Get NIC support media mode */
		db->NIC_capability = *(u16 *) (&srom[34]);
		db->PHY_reg4 = 0;
		for (tmp_reg = 1; tmp_reg < 0x10; tmp_reg <<= 1) {
			switch( db->NIC_capability & tmp_reg ) {
			case 0x1: db->PHY_reg4 |= 0x0020; break;
			case 0x2: db->PHY_reg4 |= 0x0040; break;
			case 0x4: db->PHY_reg4 |= 0x0080; break;
			case 0x8: db->PHY_reg4 |= 0x0100; break;
			}
		}

		/* Media Mode Force or not check */
		dmfe_mode = *( (int *) &srom[34]) & *( (int *) &srom[36] );
		switch(dmfe_mode) {
		case 0x4: dmfe_media_mode = DMFE_100MHF; break;	/* 100MHF */
		case 0x2: dmfe_media_mode = DMFE_10MFD; break;	/* 10MFD */
		case 0x8: dmfe_media_mode = DMFE_100MFD; break;	/* 100MFD */
		case 0x100:
		case 0x200: dmfe_media_mode = DMFE_1M_HPNA; break;/* HomePNA */
		}

		/* Special Function setting */
		/* VLAN function */
		if ( (SF_mode & 0x1) || (srom[43] & 0x80) )
			db->cr15_data |= 0x40;

		/* Flow Control */
		if ( (SF_mode & 0x2) || (srom[40] & 0x1) )
			db->cr15_data |= 0x400;

		/* TX pause packet */
		if ( (SF_mode & 0x4) || (srom[40] & 0xe) )
			db->cr15_data |= 0x9800;
	}

	/* Parse HPNA parameter */
	db->HPNA_command = 1;

	/* Accept remote command or not */
	if (HPNA_rx_cmd == 0)
		db->HPNA_command |= 0x8000;

	 /* Issue remote command & operation mode */
	if (HPNA_tx_cmd == 1)
		switch(HPNA_mode) {	/* Issue Remote Command */
		case 0: db->HPNA_command |= 0x0904; break;
		case 1: db->HPNA_command |= 0x0a00; break;
		case 2: db->HPNA_command |= 0x0506; break;
		case 3: db->HPNA_command |= 0x0602; break;
		}
	else
		switch(HPNA_mode) {	/* Don't Issue */
		case 0: db->HPNA_command |= 0x0004; break;
		case 1: db->HPNA_command |= 0x0000; break;
		case 2: db->HPNA_command |= 0x0006; break;
		case 3: db->HPNA_command |= 0x0002; break;
		}

	/* Check DM9801 or DM9802 present or not */
	db->HPNA_present = 0;
	update_cr6(db->cr6_data|0x40000, db->ioaddr);
	tmp_reg = phy_read(db->ioaddr, db->phy_addr, 3, db->chip_id);
	if ( ( tmp_reg & 0xfff0 ) == 0xb900 ) {
		/* DM9801 or DM9802 present */
		db->HPNA_timer = 8;
		if ( phy_read(db->ioaddr, db->phy_addr, 31, db->chip_id) == 0x4404) {
			/* DM9801 HomeRun */
			db->HPNA_present = 1;
			dmfe_program_DM9801(db, tmp_reg);
		} else {
			/* DM9802 LongRun */
			db->HPNA_present = 2;
			dmfe_program_DM9802(db);
		}
	}

}


/*
 *	Init HomeRun DM9801
 */

static void dmfe_program_DM9801(struct dmfe_board_info * db, int HPNA_rev)
{
	uint reg17, reg25;

	if ( !HPNA_NoiseFloor ) HPNA_NoiseFloor = DM9801_NOISE_FLOOR;
	switch(HPNA_rev) {
	case 0xb900: /* DM9801 E3 */
		db->HPNA_command |= 0x1000;
		reg25 = phy_read(db->ioaddr, db->phy_addr, 24, db->chip_id);
		reg25 = ( (reg25 + HPNA_NoiseFloor) & 0xff) | 0xf000;
		reg17 = phy_read(db->ioaddr, db->phy_addr, 17, db->chip_id);
		break;
	case 0xb901: /* DM9801 E4 */
		reg25 = phy_read(db->ioaddr, db->phy_addr, 25, db->chip_id);
		reg25 = (reg25 & 0xff00) + HPNA_NoiseFloor;
		reg17 = phy_read(db->ioaddr, db->phy_addr, 17, db->chip_id);
		reg17 = (reg17 & 0xfff0) + HPNA_NoiseFloor + 3;
		break;
	case 0xb902: /* DM9801 E5 */
	case 0xb903: /* DM9801 E6 */
	default:
		db->HPNA_command |= 0x1000;
		reg25 = phy_read(db->ioaddr, db->phy_addr, 25, db->chip_id);
		reg25 = (reg25 & 0xff00) + HPNA_NoiseFloor - 5;
		reg17 = phy_read(db->ioaddr, db->phy_addr, 17, db->chip_id);
		reg17 = (reg17 & 0xfff0) + HPNA_NoiseFloor;
		break;
	}
	phy_write(db->ioaddr, db->phy_addr, 16, db->HPNA_command, db->chip_id);
	phy_write(db->ioaddr, db->phy_addr, 17, reg17, db->chip_id);
	phy_write(db->ioaddr, db->phy_addr, 25, reg25, db->chip_id);
}


/*
 *	Init HomeRun DM9802
 */

static void dmfe_program_DM9802(struct dmfe_board_info * db)
{
	uint phy_reg;

	if ( !HPNA_NoiseFloor ) HPNA_NoiseFloor = DM9802_NOISE_FLOOR;
	phy_write(db->ioaddr, db->phy_addr, 16, db->HPNA_command, db->chip_id);
	phy_reg = phy_read(db->ioaddr, db->phy_addr, 25, db->chip_id);
	phy_reg = ( phy_reg & 0xff00) + HPNA_NoiseFloor;
	phy_write(db->ioaddr, db->phy_addr, 25, phy_reg, db->chip_id);
}


/*
 *	Check remote HPNA power and speed status. If not correct,
 *	issue command again.
*/

static void dmfe_HPNA_remote_cmd_chk(struct dmfe_board_info * db)
{
	uint phy_reg;

	/* Got remote device status */
	phy_reg = phy_read(db->ioaddr, db->phy_addr, 17, db->chip_id) & 0x60;
	switch(phy_reg) {
	case 0x00: phy_reg = 0x0a00;break; /* LP/LS */
	case 0x20: phy_reg = 0x0900;break; /* LP/HS */
	case 0x40: phy_reg = 0x0600;break; /* HP/LS */
	case 0x60: phy_reg = 0x0500;break; /* HP/HS */
	}

	/* Check remote device status match our setting ot not */
	if ( phy_reg != (db->HPNA_command & 0x0f00) ) {
		phy_write(db->ioaddr, db->phy_addr, 16, db->HPNA_command, db->chip_id);
		db->HPNA_timer=8;
	} else
		db->HPNA_timer=600;	/* Match, every 10 minutes, check */
}



static struct pci_device_id dmfe_pci_tbl[] __devinitdata = {
	{ 0x1282, 0x9132, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_DM9132_ID },
	{ 0x1282, 0x9102, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_DM9102_ID },
	{ 0x1282, 0x9100, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_DM9100_ID },
	{ 0x1282, 0x9009, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_DM9009_ID },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, dmfe_pci_tbl);


static struct pci_driver dmfe_driver = {
	name:		"dmfe",
	id_table:	dmfe_pci_tbl,
	probe:		dmfe_init_one,
	remove:		dmfe_remove_one,
};

MODULE_AUTHOR("Sten Wang, sten_wang@davicom.com.tw");
MODULE_DESCRIPTION("Davicom DM910X fast ethernet driver");
MODULE_LICENSE("GPL");

MODULE_PARM(debug, "i");
MODULE_PARM(mode, "i");
MODULE_PARM(cr6set, "i");
MODULE_PARM(chkmode, "i");
MODULE_PARM(HPNA_mode, "i");
MODULE_PARM(HPNA_rx_cmd, "i");
MODULE_PARM(HPNA_tx_cmd, "i");
MODULE_PARM(HPNA_NoiseFloor, "i");
MODULE_PARM(SF_mode, "i");
MODULE_PARM_DESC(debug, "Davicom DM9xxx enable debugging (0-1)");
MODULE_PARM_DESC(mode, "Davicom DM9xxx: Bit 0: 10/100Mbps, bit 2: duplex, bit 8: HomePNA");
MODULE_PARM_DESC(SF_mode, "Davicom DM9xxx special function (bit 0: VLAN, bit 1 Flow Control, bit 2: TX pause packet)");
                                                                                                                                
/*	Description:
 *	when user used insmod to add module, system invoked init_module()
 *	to initilize and register.
 */

static int __init dmfe_init_module(void)
{
	int rc;

	printk(version);
	printed_version = 1;

	DMFE_DBUG(0, "init_module() ", debug);

	if (debug)
		dmfe_debug = debug;	/* set debug flag */
	if (cr6set)
		dmfe_cr6_user_set = cr6set;

 	switch(mode) {
   	case DMFE_10MHF:
	case DMFE_100MHF:
	case DMFE_10MFD:
	case DMFE_100MFD:
	case DMFE_1M_HPNA:
		dmfe_media_mode = mode;
		break;
	default:dmfe_media_mode = DMFE_AUTO;
		break;
	}

	if (HPNA_mode > 4)
		HPNA_mode = 0;		/* Default: LP/HS */
	if (HPNA_rx_cmd > 1)
		HPNA_rx_cmd = 0;	/* Default: Ignored remote cmd */
	if (HPNA_tx_cmd > 1)
		HPNA_tx_cmd = 0;	/* Default: Don't issue remote cmd */
	if (HPNA_NoiseFloor > 15)
		HPNA_NoiseFloor = 0;

	rc = pci_module_init(&dmfe_driver);
	if (rc < 0)
		return rc;

	return 0;
}


/*
 *	Description:
 *	when user used rmmod to delete module, system invoked clean_module()
 *	to un-register all registered services.
 */

static void __exit dmfe_cleanup_module(void)
{
	DMFE_DBUG(0, "dmfe_clean_module() ", debug);
	pci_unregister_driver(&dmfe_driver);
}

module_init(dmfe_init_module);
module_exit(dmfe_cleanup_module);
