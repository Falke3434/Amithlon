/*
 * ether.c -- CDC 1.1 Ethernet gadget driver
 *
 * Copyright (C) 2003 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#define DEBUG 1
// #define VERBOSE

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/uts.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/moduleparam.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include <linux/usb_ch9.h>
#include <linux/usb_gadget.h>

#include <linux/random.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>

/*-------------------------------------------------------------------------*/

/*
 * "Communications Device Class" (CDC) Ethernet class driver
 *
 * CDC Ethernet is the standard USB solution for sending Ethernet frames
 * using USB.  Real hardware tends to use the same framing protocol but look
 * different for control features.  And Microsoft pushes their own approach
 * (RNDIS) instead of the standard.
 */

#define DRIVER_DESC		"CDC Ethernet Gadget"
#define DRIVER_VERSION		"29 April 2003"

static const char shortname [] = "ether";
static const char driver_desc [] = DRIVER_DESC;
static const char control_name [] = "Communications Control";
static const char data_name [] = "CDC Ethernet Data";

#define MIN_PACKET	sizeof(struct ethhdr)
#define	MAX_PACKET	ETH_DATA_LEN	/* biggest packet we'll rx/tx */
#define RX_EXTRA	20		/* guard against rx overflows */

/* FIXME allow high speed jumbograms */

/*-------------------------------------------------------------------------*/

struct eth_dev {
	spinlock_t		lock;
	struct usb_gadget	*gadget;
	struct usb_request	*req;		/* for control responses */

	u8			config;
	struct usb_ep		*in_ep, *out_ep, *status_ep;
	const struct usb_endpoint_descriptor
				*in, *out, *status;

	struct semaphore	mutex;
	struct net_device	net;
	struct net_device_stats	stats;
	atomic_t		tx_qlen;

	struct work_struct	work;
	unsigned long		todo;
#define	WORK_RX_MEMORY		0
};

/*-------------------------------------------------------------------------*/

/* This driver keeps a variable number of requests queued, more at
 * high speeds.  (Numbers are just educated guesses, untuned.)
 * Shrink the queue if memory is tight, or make it bigger to
 * handle bigger traffic bursts between IRQs.
 */

static unsigned qmult = 4;

#define HS_FACTOR	15

#define qlen(gadget) \
	(qmult*((gadget->speed == USB_SPEED_HIGH) ? HS_FACTOR : 1))

/* defer IRQs on highspeed TX */
#define TX_DELAY	8


module_param (qmult, uint, S_IRUGO|S_IWUSR);


/*-------------------------------------------------------------------------*/

/* Thanks to NetChip Technologies for donating this product ID.
 *
 * DO NOT REUSE THESE IDs with any other driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */
#define DRIVER_VENDOR_NUM	0x0525		/* NetChip */
#define DRIVER_PRODUCT_NUM	0xa4a1		/* Linux-USB Ethernet Gadget */

/*-------------------------------------------------------------------------*/

/*
 * hardware-specific configuration, controlled by which device
 * controller driver was configured.
 *
 * CHIP ... hardware identifier
 * DRIVER_VERSION_NUM ... alerts the host side driver to differences
 * EP0_MAXPACKET ... controls packetization of control requests
 * EP_*_NAME ... which endpoints do we use for which purpose?
 * EP_*_NUM ... numbers for them (often limited by hardware)
 * HIGHSPEED ... define if ep0 and descriptors need high speed support
 * MAX_USB_POWER ... define if we use other than 100 mA bus current
 * SELFPOWER ... unless we can run on bus power, USB_CONFIG_ATT_SELFPOWER
 * WAKEUP ... if hardware supports remote wakeup AND we will issue the
 * 	usb_gadget_wakeup() call to initiate it, USB_CONFIG_ATT_WAKEUP
 *
 * hw_optimize(gadget) ... for any hardware tweaks we want to kick in
 * 	before we enable our endpoints
 *
 * add other defines for other portability issues, like hardware that
 * for some reason doesn't handle full speed bulk maxpacket of 64.
 */

/*
 * NetChip 2280, PCI based.
 *
 * use DMA with fat fifos for all data traffic, PIO for the status channel
 * where its 64 byte maxpacket ceiling is no issue.
 *
 * performance note:  only PIO needs per-usb-packet IRQs (ep0, ep-e, ep-f)
 * otherwise IRQs are per-Ethernet-packet unless TX_DELAY and chaining help.
 */
#ifdef	CONFIG_USB_ETH_NET2280
#define CHIP			"net2280"
#define DRIVER_VERSION_NUM	0x0101
#define EP0_MAXPACKET		64
static const char EP_OUT_NAME [] = "ep-a";
#define EP_OUT_NUM	2
static const char EP_IN_NAME [] = "ep-b";
#define EP_IN_NUM	2
static const char EP_STATUS_NAME [] = "ep-f";
#define EP_STATUS_NUM	3
#define HIGHSPEED
/* specific hardware configs could be bus-powered */
#define SELFPOWER USB_CONFIG_ATT_SELFPOWER
/* supports remote wakeup, but this driver doesn't */

extern int net2280_set_fifo_mode (struct usb_gadget *gadget, int mode);

static inline void hw_optimize (struct usb_gadget *gadget)
{
	/* we can have bigger ep-a/ep-b fifos (2KB each, 4 USB packets
	 * for highspeed bulk) because we're not using ep-c/ep-d.
	 */
	net2280_set_fifo_mode (gadget, 1);
}
#endif

/*
 * PXA-250 UDC:  widely used in second gen Linux-capable PDAs.
 *
 * no limitations except from set_interface: docs say "no" to a third
 * interface. and the interrupt-only endpoints don't toggle, so we'll
 * just use a bulk-capable one instead.
 */
#ifdef	CONFIG_USB_ETH_PXA250
#define CHIP			"pxa250"
#define DRIVER_VERSION_NUM	0x0103
#define EP0_MAXPACKET		16
static const char EP_OUT_NAME [] = "ep12out-bulk";
#define EP_OUT_NUM	12
static const char EP_IN_NAME [] = "ep11in-bulk";
#define EP_IN_NUM	11
static const char EP_STATUS_NAME [] = "ep6in-bulk";
#define EP_STATUS_NUM	6
/* doesn't support bus-powered operation */
#define SELFPOWER USB_CONFIG_ATT_SELFPOWER
/* supports remote wakeup, but this driver doesn't */

/* no hw optimizations to apply */
#define hw_optimize(g) do {} while (0);
#endif

/*
 * SA-1100 UDC:  widely used in first gen Linux-capable PDAs.
 *
 * can't have a notification endpoint, since there are only the two
 * bulk-capable ones.  the CDC spec allows that.
 */
#ifdef	CONFIG_USB_ETH_SA1100
#define CHIP			"sa1100"
#define DRIVER_VERSION_NUM	0x0105
#define EP0_MAXPACKET		8
static const char EP_OUT_NAME [] = "ep1out-bulk";
#define EP_OUT_NUM	1
static const char EP_IN_NAME [] = "ep2in-bulk";
#define EP_IN_NUM	2
// EP_STATUS_NUM is undefined
/* doesn't support bus-powered operation */
#define SELFPOWER USB_CONFIG_ATT_SELFPOWER
/* doesn't support remote wakeup? */

/* no hw optimizations to apply */
#define hw_optimize(g) do {} while (0);
#endif

/*-------------------------------------------------------------------------*/

#ifndef EP0_MAXPACKET
#	error Configure some USB peripheral controller driver!
#endif

/* power usage is config specific.
 * hardware that supports remote wakeup defaults to disabling it.
 */

#ifndef	SELFPOWER
/* default: say we rely on bus power */
#define SELFPOWER	0
/* else:
 * - SELFPOWER value must be USB_CONFIG_ATT_SELFPOWER
 * - MAX_USB_POWER may be nonzero.
 */
#endif

#ifndef	MAX_USB_POWER
/* any hub supports this steady state bus power consumption */
#define MAX_USB_POWER	100	/* mA */
#endif

#ifndef	WAKEUP
/* default: this driver won't do remote wakeup */
#define WAKEUP		0
/* else value must be USB_CONFIG_ATT_WAKEUP */
#endif

/*-------------------------------------------------------------------------*/

#define xprintk(d,level,fmt,args...) \
	dev_printk(level , &(d)->gadget->dev , fmt , ## args)

#ifdef DEBUG
#undef DEBUG
#define DEBUG(dev,fmt,args...) \
	xprintk(dev , KERN_DEBUG , fmt , ## args)
#else
#define DEBUG(dev,fmt,args...) \
	do { } while (0)
#endif /* DEBUG */

#ifdef VERBOSE
#define VDEBUG	DEBUG
#else
#define VDEBUG(dev,fmt,args...) \
	do { } while (0)
#endif /* DEBUG */

#define ERROR(dev,fmt,args...) \
	xprintk(dev , KERN_ERR , fmt , ## args)
#define WARN(dev,fmt,args...) \
	xprintk(dev , KERN_WARNING , fmt , ## args)
#define INFO(dev,fmt,args...) \
	xprintk(dev , KERN_INFO , fmt , ## args)

/*-------------------------------------------------------------------------*/

/* USB DRIVER HOOKUP (to the hardware driver, below us), mostly
 * ep0 implementation:  descriptors, config management, setup().
 * also optional class-specific notification interrupt transfer.
 */

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.  Notice how most of the cdc descriptors
 * add no value to simple (typical) configurations.
 */

#define STRING_MANUFACTURER		1
#define STRING_PRODUCT			2
#define STRING_ETHADDR			3
#define STRING_DATA			4
#define STRING_CONTROL			5

#define USB_BUFSIZ	256		/* holds our biggest descriptor */

/*
 * This device advertises one configuration.
 */
#define	CONFIG_CDC_ETHER	3

static const struct usb_device_descriptor
device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		__constant_cpu_to_le16 (0x0200),
	.bDeviceClass =		USB_CLASS_COMM,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	.bMaxPacketSize0 =	EP0_MAXPACKET,

	.idVendor =		__constant_cpu_to_le16 (DRIVER_VENDOR_NUM),
	.idProduct =		__constant_cpu_to_le16 (DRIVER_PRODUCT_NUM),
	.bcdDevice =		__constant_cpu_to_le16 (DRIVER_VERSION_NUM),
	.iManufacturer =	STRING_MANUFACTURER,
	.iProduct =		STRING_PRODUCT,
	.bNumConfigurations =	1,
};

static const struct usb_config_descriptor
eth_config = {
	.bLength =		sizeof eth_config,
	.bDescriptorType =	USB_DT_CONFIG,

	/* compute wTotalLength on the fly */
	.bNumInterfaces =	2,
	.bConfigurationValue =	CONFIG_CDC_ETHER,
	.iConfiguration =	STRING_PRODUCT,
	.bmAttributes =		USB_CONFIG_ATT_ONE | SELFPOWER | WAKEUP,
	.bMaxPower =		(MAX_USB_POWER + 1) / 2,
};

/* master comm interface optionally has a status notification endpoint */

static const struct usb_interface_descriptor
control_intf = {
	.bLength =		sizeof control_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bInterfaceNumber =	0,
#ifdef	EP_STATUS_NUM
	.bNumEndpoints =	1,
#else
	.bNumEndpoints =	0,
#endif
	.bInterfaceClass =	USB_CLASS_COMM,
	.bInterfaceSubClass =	6,	/* ethernet control model */
	.bInterfaceProtocol =	0,
	.iInterface =		STRING_CONTROL,
};

/* "Header Functional Descriptor" from CDC spec  5.2.3.1 */
struct header_desc {
	u8	bLength;
	u8	bDescriptorType;
	u8	bDescriptorSubType;

	u16	bcdCDC;
} __attribute__ ((packed));

static const struct header_desc header_desc = {
	.bLength =		sizeof header_desc,
	.bDescriptorType =	0x24,
	.bDescriptorSubType =	0,

	.bcdCDC =		__constant_cpu_to_le16 (0x0110),
};

/* "Union Functional Descriptor" from CDC spec 5.2.3.X */
struct union_desc {
	u8	bLength;
	u8	bDescriptorType;
	u8	bDescriptorSubType;

	u8	bMasterInterface0;
	u8	bSlaveInterface0;
	/* ... and there could be other slave interfaces */
} __attribute__ ((packed));

static const struct union_desc union_desc = {
	.bLength =		sizeof union_desc,
	.bDescriptorType =	0x24,
	.bDescriptorSubType =	6,

	.bMasterInterface0 =	0,	/* index of control interface */
	.bSlaveInterface0 =	1,	/* index of DATA interface */
};

/* "Ethernet Networking Functional Descriptor" from CDC spec 5.2.3.16 */
struct ether_desc {
	u8	bLength;
	u8	bDescriptorType;
	u8	bDescriptorSubType;

	u8	iMACAddress;
	u32	bmEthernetStatistics;
	u16	wMaxSegmentSize;
	u16	wNumberMCFilters;
	u8	bNumberPowerFilters;
} __attribute__ ((packed));

static const struct ether_desc ether_desc = {
	.bLength =		sizeof ether_desc,
	.bDescriptorType =	0x24,
	.bDescriptorSubType =	0x0f,

	/* this descriptor actually adds value, surprise! */
	.iMACAddress =		STRING_ETHADDR,
	.bmEthernetStatistics =	__constant_cpu_to_le32 (0), /* no statistics */
	.wMaxSegmentSize =	__constant_cpu_to_le16 (MAX_PACKET + ETH_HLEN),
	.wNumberMCFilters =	__constant_cpu_to_le16 (0),
	.bNumberPowerFilters =	0,
};

#ifdef	EP_STATUS_NUM

/* include the status endpoint if we can, even though it's optional.
 *
 * some drivers (like current Linux cdc-ether!) "need" it to exist even
 * if they ignore the connect/disconnect notifications that real aether
 * can provide.  more advanced cdc configurations might want to support
 * encapsulated commands.
 */
 
#define LOG2_STATUS_INTERVAL_MSEC	6
#define STATUS_BYTECOUNT		16	/* 8 byte header + data */
static const struct usb_endpoint_descriptor
fs_status_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	EP_STATUS_NUM | USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	__constant_cpu_to_le16 (STATUS_BYTECOUNT),
	.bInterval =		1 << LOG2_STATUS_INTERVAL_MSEC,
};
#endif

/* the default data interface has no endpoints ... */

static const struct usb_interface_descriptor
data_nop_intf = {
	.bLength =		sizeof data_nop_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bInterfaceNumber =	1,
	.bAlternateSetting =	0,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
	.iInterface =		STRING_DATA,
};

/* ... but the "real" data interface has two full speed bulk endpoints */

static const struct usb_interface_descriptor
data_intf = {
	.bLength =		sizeof data_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bInterfaceNumber =	1,
	.bAlternateSetting =	1,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
	.iInterface =		STRING_DATA,
};

static const struct usb_endpoint_descriptor
fs_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	EP_IN_NUM | USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16 (64),
};

static const struct usb_endpoint_descriptor
fs_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	EP_OUT_NUM,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16 (64),
};

#ifdef	HIGHSPEED

/*
 * usb 2.0 devices need to expose both high speed and full speed
 * descriptors, unless they only run at full speed.
 */

#ifdef	EP_STATUS_NUM
static const struct usb_endpoint_descriptor
hs_status_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	EP_STATUS_NUM | USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	__constant_cpu_to_le16 (STATUS_BYTECOUNT),
	.bInterval =		LOG2_STATUS_INTERVAL_MSEC + 3,
};
#endif

static const struct usb_endpoint_descriptor
hs_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	EP_IN_NUM | USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16 (512),
	.bInterval =		1,
};

static const struct usb_endpoint_descriptor
hs_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	EP_OUT_NUM,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16 (512),
	.bInterval =		1,
};

static const struct usb_qualifier_descriptor
dev_qualifier = {
	.bLength =		sizeof dev_qualifier,
	.bDescriptorType =	USB_DT_DEVICE_QUALIFIER,

	.bcdUSB =		__constant_cpu_to_le16 (0x0200),
	.bDeviceClass =		USB_CLASS_VENDOR_SPEC,

	/* assumes ep0 uses the same value for both speeds ... */
	.bMaxPacketSize0 =	EP0_MAXPACKET,

	.bNumConfigurations =	2,
};

/* maxpacket and other transfer characteristics vary by speed. */
#define ep_desc(g,hs,fs) (((g)->speed==USB_SPEED_HIGH)?(hs):(fs))

#else

/* if there's no high speed support, maxpacket doesn't change. */
#define ep_desc(g,hs,fs) fs

#endif	/* !HIGHSPEED */

/* address that the host will use ... usually assigned at random */
static char				ethaddr [2 * ETH_ALEN + 1];

/* static strings, in iso 8859/1 */
static struct usb_string		strings [] = {
	{ STRING_MANUFACTURER,	UTS_SYSNAME " " UTS_RELEASE "/" CHIP, },
	{ STRING_PRODUCT,	driver_desc, },
	{ STRING_ETHADDR,	ethaddr, },
	{ STRING_CONTROL,	control_name, },
	{ STRING_DATA,		data_name, },
	{  }		/* end of list */
};

static struct usb_gadget_strings	stringtab = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings,
};

/*
 * one config, two interfaces:  control, data.
 * complications: class descriptors, and an altsetting.
 */
static int
config_buf (enum usb_device_speed speed, u8 *buf, u8 type, unsigned index)
{
	const unsigned	config_len = USB_DT_CONFIG_SIZE
				+ 3 * USB_DT_INTERFACE_SIZE
				+ sizeof header_desc
				+ sizeof union_desc
				+ sizeof ether_desc
#ifdef	EP_STATUS_NUM
				+ USB_DT_ENDPOINT_SIZE
#endif
				+ 2 * USB_DT_ENDPOINT_SIZE;
#ifdef HIGHSPEED
	int		hs;
#endif
	/* a single configuration must always be index 0 */
	if (index > 0)
		return -EINVAL;
	if (config_len > USB_BUFSIZ)
		return -EDOM;

	/* config (or other speed config) */
	memcpy (buf, &eth_config, USB_DT_CONFIG_SIZE);
	buf [1] = type;
	((struct usb_config_descriptor *) buf)->wTotalLength
		= __constant_cpu_to_le16 (config_len);
	buf += USB_DT_CONFIG_SIZE;
#ifdef	HIGHSPEED
	hs = (speed == USB_SPEED_HIGH);
	if (type == USB_DT_OTHER_SPEED_CONFIG)
		hs = !hs;
#endif

	/* control interface, class descriptors, optional status endpoint */
	memcpy (buf, &control_intf, USB_DT_INTERFACE_SIZE);
	buf += USB_DT_INTERFACE_SIZE;

	memcpy (buf, &header_desc, sizeof header_desc);
	buf += sizeof header_desc;
	memcpy (buf, &union_desc, sizeof union_desc);
	buf += sizeof union_desc;
	memcpy (buf, &ether_desc, sizeof ether_desc);
	buf += sizeof ether_desc;

#ifdef	EP_STATUS_NUM
#ifdef	HIGHSPEED
	if (hs)
		memcpy (buf, &hs_status_desc, USB_DT_ENDPOINT_SIZE);
	else
#endif	/* HIGHSPEED */
		memcpy (buf, &fs_status_desc, USB_DT_ENDPOINT_SIZE);
	buf += USB_DT_ENDPOINT_SIZE;
#endif	/* EP_STATUS_NUM */

	/* default data altsetting has no endpoints */
	memcpy (buf, &data_nop_intf, USB_DT_INTERFACE_SIZE);
	buf += USB_DT_INTERFACE_SIZE;

	/* the "real" data interface has two endpoints */
	memcpy (buf, &data_intf, USB_DT_INTERFACE_SIZE);
	buf += USB_DT_INTERFACE_SIZE;
#ifdef HIGHSPEED
	if (hs) {
		memcpy (buf, &hs_source_desc, USB_DT_ENDPOINT_SIZE);
		buf += USB_DT_ENDPOINT_SIZE;
		memcpy (buf, &hs_sink_desc, USB_DT_ENDPOINT_SIZE);
		buf += USB_DT_ENDPOINT_SIZE;
	} else
#endif
	{
		memcpy (buf, &fs_source_desc, USB_DT_ENDPOINT_SIZE);
		buf += USB_DT_ENDPOINT_SIZE;
		memcpy (buf, &fs_sink_desc, USB_DT_ENDPOINT_SIZE);
		buf += USB_DT_ENDPOINT_SIZE;
	}

	return config_len;
}

/*-------------------------------------------------------------------------*/

static int
set_ether_config (struct eth_dev *dev, int gfp_flags)
{
	int			result = 0;
	struct usb_ep		*ep;
	struct usb_gadget	*gadget = dev->gadget;

	gadget_for_each_ep (ep, gadget) {
		const struct usb_endpoint_descriptor	*d;

		/* NOTE:  the host isn't allowed to use these two data
		 * endpoints in the default altsetting for the interface.
		 * so we don't activate them yet.
		 */

		/* one endpoint writes data back IN to the host */
		if (strcmp (ep->name, EP_IN_NAME) == 0) {
			d = ep_desc (gadget, &hs_source_desc, &fs_source_desc);
			ep->driver_data = dev;
			dev->in_ep = ep;
			dev->in = d;
			continue;

		/* one endpoint just reads OUT packets */
		} else if (strcmp (ep->name, EP_OUT_NAME) == 0) {
			d = ep_desc (gadget, &hs_sink_desc, &fs_sink_desc);
			ep->driver_data = dev;
			dev->out_ep = ep;
			dev->out = d;
			continue;

#ifdef	EP_STATUS_NUM
		/* optional status/notification endpoint */
		} else if (strcmp (ep->name, EP_STATUS_NAME) == 0) {
			d = ep_desc (gadget, &hs_status_desc, &fs_status_desc);
			result = usb_ep_enable (ep, d);
			if (result == 0) {
				ep->driver_data = dev;
				dev->status_ep = ep;
				dev->status = d;
				continue;
			}
#endif

		/* ignore any other endpoints */
		} else
			continue;

		/* stop on error */
		ERROR (dev, "can't enable %s, result %d\n", ep->name, result);
		break;
	}

	if (result == 0)
		DEBUG (dev, "qlen %d\n", qlen (gadget));

	/* caller is responsible for cleanup on error */
	return result;
}

static void eth_reset_config (struct eth_dev *dev)
{
	if (dev->config == 0)
		return;

	DEBUG (dev, "%s\n", __FUNCTION__);

	netif_stop_queue (&dev->net);
	netif_carrier_off (&dev->net);

	/* just disable endpoints, forcing completion of pending i/o.
	 * all our completion handlers free their requests in this case.
	 */
	if (dev->in_ep) {
		usb_ep_disable (dev->in_ep);
		dev->in_ep = 0;
	}
	if (dev->out_ep) {
		usb_ep_disable (dev->out_ep);
		dev->out_ep = 0;
	}
#ifdef	EP_STATUS_NUM
	if (dev->status_ep) {
		usb_ep_disable (dev->status_ep);
		dev->status_ep = 0;
	}
#endif
	dev->config = 0;
}

/* change our operational config.  must agree with the code
 * that returns config descriptors, and altsetting code.
 */
static int
eth_set_config (struct eth_dev *dev, unsigned number, int gfp_flags)
{
	int			result = 0;
	struct usb_gadget	*gadget = dev->gadget;

	if (number == dev->config)
		return 0;

#ifdef CONFIG_USB_ETH_SA1100
	if (dev->config && atomic_read (&dev->tx_qlen) != 0) {
		/* tx fifo is full, but we can't clear it...*/
		INFO (dev, "can't change configurations\n");
		return -ESPIPE;
	}
#endif
	eth_reset_config (dev);
	hw_optimize (gadget);

	switch (number) {
	case CONFIG_CDC_ETHER:
		result = set_ether_config (dev, gfp_flags);
		break;
	default:
		result = -EINVAL;
		/* FALL THROUGH */
	case 0:
		return result;
	}

	if (!result && (!dev->in_ep || !dev->out_ep))
		result = -ENODEV;
	if (result)
		eth_reset_config (dev);
	else {
		char *speed;

		switch (gadget->speed) {
		case USB_SPEED_FULL:	speed = "full"; break;
#ifdef HIGHSPEED
		case USB_SPEED_HIGH:	speed = "high"; break;
#endif
		default: 		speed = "?"; break;
		}

		dev->config = number;
		INFO (dev, "%s speed config #%d: %s\n", speed, number,
				driver_desc);
	}
	return result;
}

/*-------------------------------------------------------------------------*/

#ifdef	EP_STATUS_NUM

/* section 3.8.2 table 11 of the CDC spec lists Ethernet notifications */
#define CDC_NOTIFY_NETWORK_CONNECTION	0x00	/* required; 6.3.1 */
#define CDC_NOTIFY_SPEED_CHANGE		0x2a	/* required; 6.3.8 */

struct cdc_notification {
	u8	bmRequestType;
	u8	bNotificationType;
	u16	wValue;
	u16	wIndex;
	u16	wLength;

	/* SPEED_CHANGE data looks like this */
	u32	data [2];
};

static void eth_status_complete (struct usb_ep *ep, struct usb_request *req)
{
	struct cdc_notification	*event = req->buf;
	int			value = req->status;
	struct eth_dev		*dev = ep->driver_data;

	/* issue the second notification if host reads the first */
	if (event->bNotificationType == CDC_NOTIFY_NETWORK_CONNECTION
			&& value == 0) {
		event->bmRequestType = 0xA1;
		event->bNotificationType = CDC_NOTIFY_SPEED_CHANGE;
		event->wValue = __constant_cpu_to_le16 (0);
		event->wIndex = __constant_cpu_to_le16 (1);
		event->wLength = __constant_cpu_to_le16 (8);

		/* SPEED_CHANGE data is up/down speeds in bits/sec */
		event->data [0] = event->data [1] =
			(dev->gadget->speed == USB_SPEED_HIGH)
				? (13 * 512 * 8 * 1000 * 8)
				: (19 *  64 * 1 * 1000 * 8);

		req->length = 16;
		value = usb_ep_queue (ep, req, GFP_ATOMIC);
		DEBUG (dev, "send SPEED_CHANGE --> %d\n", value);
		if (value == 0)
			return;
	} else
		DEBUG (dev, "event %02x --> %d\n",
			event->bNotificationType, value);

	/* free when done */
	usb_ep_free_buffer (ep, req->buf, req->dma, 16);
	usb_ep_free_request (ep, req);
}

static void issue_start_status (struct eth_dev *dev)
{
	struct usb_request	*req;
	struct cdc_notification	*event;
	int			value;
 
	DEBUG (dev, "%s, flush old status first\n", __FUNCTION__);

	/* flush old status
	 *
	 * FIXME ugly idiom, maybe we'd be better with just
	 * a "cancel the whole queue" primitive since any
	 * unlink-one primitive has way too many error modes.
	 */
	usb_ep_disable (dev->status_ep);
	usb_ep_enable (dev->status_ep, dev->status);

	/* FIXME make these allocations static like dev->req */
	req = usb_ep_alloc_request (dev->status_ep, GFP_ATOMIC);
	if (req == 0) {
		DEBUG (dev, "status ENOMEM\n");
		return;
	}
	req->buf = usb_ep_alloc_buffer (dev->status_ep, 16,
				&dev->req->dma, GFP_ATOMIC);
	if (req->buf == 0) {
		DEBUG (dev, "status buf ENOMEM\n");
free_req:
		usb_ep_free_request (dev->status_ep, req);
		return;
	}

	/* 3.8.1 says to issue first NETWORK_CONNECTION, then
	 * a SPEED_CHANGE.  could be useful in some configs.
	 */
	event = req->buf;
	event->bmRequestType = 0xA1;
	event->bNotificationType = CDC_NOTIFY_NETWORK_CONNECTION;
	event->wValue = __constant_cpu_to_le16 (1);	/* connected */
	event->wIndex = __constant_cpu_to_le16 (1);
	event->wLength = 0;

	req->length = 8;
	req->complete = eth_status_complete;
	value = usb_ep_queue (dev->status_ep, req, GFP_ATOMIC);
	if (value < 0) {
		DEBUG (dev, "status buf queue --> %d\n", value);
		usb_ep_free_buffer (dev->status_ep,
				req->buf, dev->req->dma, 16);
		goto free_req;
	}
}

#endif

/*-------------------------------------------------------------------------*/

static void eth_setup_complete (struct usb_ep *ep, struct usb_request *req)
{
	if (req->status || req->actual != req->length)
		DEBUG ((struct eth_dev *) ep->driver_data,
				"setup complete --> %d, %d/%d\n",
				req->status, req->actual, req->length);
}

/* see section 3.8.2 table 10 of the CDC spec for more ethernet
 * requests, mostly for filters (multicast, pm) and statistics
 */
#define CDC_SET_ETHERNET_PACKET_FILTER	0x43	/* required */

static void eth_start (struct eth_dev *dev, int gfp_flags);

/*
 * The setup() callback implements all the ep0 functionality that's not
 * handled lower down.  CDC has a number of less-common features:
 *
 *  - two interfaces:  control, and ethernet data
 *  - data interface has two altsettings:  default, and active
 *  - class-specific descriptors for the control interface
 *  - a mandatory class-specific control request
 */
static int
eth_setup (struct usb_gadget *gadget, const struct usb_ctrlrequest *ctrl)
{
	struct eth_dev		*dev = get_gadget_data (gadget);
	struct usb_request	*req = dev->req;
	int			value = -EOPNOTSUPP;

	/* descriptors just go into the pre-allocated ep0 buffer,
	 * while config change events may enable network traffic.
	 */
	switch (ctrl->bRequest) {

	case USB_REQ_GET_DESCRIPTOR:
		if (ctrl->bRequestType != USB_DIR_IN)
			break;
		switch (ctrl->wValue >> 8) {

		case USB_DT_DEVICE:
			value = min (ctrl->wLength, (u16) sizeof device_desc);
			memcpy (req->buf, &device_desc, value);
			break;
#ifdef HIGHSPEED
		case USB_DT_DEVICE_QUALIFIER:
			value = min (ctrl->wLength, (u16) sizeof dev_qualifier);
			memcpy (req->buf, &dev_qualifier, value);
			break;

		case USB_DT_OTHER_SPEED_CONFIG:
			// FALLTHROUGH
#endif /* HIGHSPEED */
		case USB_DT_CONFIG:
			value = config_buf (gadget->speed, req->buf,
					ctrl->wValue >> 8,
					ctrl->wValue & 0xff);
			if (value >= 0)
				value = min (ctrl->wLength, (u16) value);
			break;

		case USB_DT_STRING:
			value = usb_gadget_get_string (&stringtab,
					ctrl->wValue & 0xff, req->buf);
			if (value >= 0)
				value = min (ctrl->wLength, (u16) value);
			break;
		}
		break;

	case USB_REQ_SET_CONFIGURATION:
		if (ctrl->bRequestType != 0)
			break;
		spin_lock (&dev->lock);
		value = eth_set_config (dev, ctrl->wValue, GFP_ATOMIC);
		spin_unlock (&dev->lock);
		break;
	case USB_REQ_GET_CONFIGURATION:
		if (ctrl->bRequestType != USB_DIR_IN)
			break;
		*(u8 *)req->buf = dev->config;
		value = min (ctrl->wLength, (u16) 1);
		break;

	case USB_REQ_SET_INTERFACE:
		if (ctrl->bRequestType != USB_RECIP_INTERFACE
				|| !dev->config
				|| ctrl->wIndex > 1)
			break;
		spin_lock (&dev->lock);
		switch (ctrl->wIndex) {
		case 0:		/* control/master intf */
			if (ctrl->wValue != 0)
				break;
#ifdef	EP_STATUS_NUM
			if (dev->status_ep) {
				usb_ep_disable (dev->status_ep);
				usb_ep_enable (dev->status_ep, dev->status);
			}
#endif
			value = 0;
			break;
		case 1:		/* data intf */
			if (ctrl->wValue > 1)
				break;
			usb_ep_disable (dev->in_ep);
			usb_ep_disable (dev->out_ep);

			/* CDC requires the data transfers not be done from
			 * the default interface setting ... also, setting
			 * the non-default interface clears filters etc.
			 */
			if (ctrl->wValue == 1) {
				usb_ep_enable (dev->in_ep, dev->in);
				usb_ep_enable (dev->out_ep, dev->out);
				netif_carrier_on (&dev->net);
#ifdef	EP_STATUS_NUM
				issue_start_status (dev);
#endif
				if (netif_running (&dev->net))
					eth_start (dev, GFP_ATOMIC);
			} else {
				netif_stop_queue (&dev->net);
				netif_carrier_off (&dev->net);
			}
			value = 0;
			break;
		}
		spin_unlock (&dev->lock);
		break;
	case USB_REQ_GET_INTERFACE:
		if (ctrl->bRequestType != (USB_DIR_IN|USB_RECIP_INTERFACE)
				|| !dev->config
				|| ctrl->wIndex > 1)
			break;

		/* if carrier is on, data interface is active. */
		*(u8 *)req->buf =
			((ctrl->wIndex == 1) && netif_carrier_ok (&dev->net))
				? 1
				: 0,
		value = min (ctrl->wLength, (u16) 1);
		break;

	case CDC_SET_ETHERNET_PACKET_FILTER:
		/* see 6.2.30: no data, wIndex = interface,
		 * wValue = packet filter bitmap
		 */
		if (ctrl->bRequestType != (USB_TYPE_CLASS|USB_RECIP_INTERFACE)
				|| ctrl->wLength != 0
				|| ctrl->wIndex > 1)
		DEBUG (dev, "NOP packet filter %04x\n", ctrl->wValue);
		/* NOTE: table 62 has 5 filter bits to reduce traffic,
		 * and we "must" support multicast and promiscuous.
		 * this NOP implements a bad filter...
		 */
		value = 0;
		break;

	default:
		VDEBUG (dev,
			"unknown control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			ctrl->wValue, ctrl->wIndex, ctrl->wLength);
	}

	/* respond with data transfer before status phase? */
	if (value >= 0) {
		req->length = value;
		value = usb_ep_queue (gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			DEBUG (dev, "ep_queue --> %d\n", value);
			req->status = 0;
			eth_setup_complete (gadget->ep0, req);
		}
	}

	/* host either stalls (value < 0) or reports success */
	return value;
}

static void
eth_disconnect (struct usb_gadget *gadget)
{
	struct eth_dev		*dev = get_gadget_data (gadget);
	unsigned long		flags;

	spin_lock_irqsave (&dev->lock, flags);
	netif_stop_queue (&dev->net);
	netif_carrier_off (&dev->net);
	eth_reset_config (dev);
	spin_unlock_irqrestore (&dev->lock, flags);

	/* next we may get setup() calls to enumerate new connections;
	 * or an unbind() during shutdown (including removing module).
	 */
}

/*-------------------------------------------------------------------------*/

/* NETWORK DRIVER HOOKUP (to the layer above this driver) */

static int eth_change_mtu (struct net_device *net, int new_mtu)
{
	struct eth_dev	*dev = (struct eth_dev *) net->priv;

	if (new_mtu <= MIN_PACKET || new_mtu > MAX_PACKET)
		return -ERANGE;
	/* no zero-length packet read wanted after mtu-sized packets */
	if (((new_mtu + sizeof (struct ethhdr)) % dev->in_ep->maxpacket) == 0)
		return -EDOM;
	net->mtu = new_mtu;
	return 0;
}

static struct net_device_stats *eth_get_stats (struct net_device *net)
{
	return &((struct eth_dev *) net->priv)->stats;
}

static int eth_ethtool_ioctl (struct net_device *net, void *useraddr)
{
	struct eth_dev	*dev = (struct eth_dev *) net->priv;
	u32		cmd;

	if (get_user (cmd, (u32 *)useraddr))
		return -EFAULT;
	switch (cmd) {

	case ETHTOOL_GDRVINFO: {	/* get driver info */
		struct ethtool_drvinfo		info;

		memset (&info, 0, sizeof info);
		info.cmd = ETHTOOL_GDRVINFO;
		strncpy (info.driver, shortname, sizeof info.driver);
		strncpy (info.version, DRIVER_VERSION, sizeof info.version);
		strncpy (info.fw_version, CHIP, sizeof info.fw_version);
		strncpy (info.bus_info, dev->gadget->dev.bus_id,
			sizeof info.bus_info);
		if (copy_to_user (useraddr, &info, sizeof (info)))
			return -EFAULT;
		return 0;
		}

	case ETHTOOL_GLINK: {		/* get link status */
		struct ethtool_value	edata = { ETHTOOL_GLINK };

		edata.data = (dev->gadget->speed != USB_SPEED_UNKNOWN);
		if (copy_to_user (useraddr, &edata, sizeof (edata)))
			return -EFAULT;
		return 0;
		}

	}
	/* Note that the ethtool user space code requires EOPNOTSUPP */
	return -EOPNOTSUPP;
}

static int eth_ioctl (struct net_device *net, struct ifreq *rq, int cmd)
{
	switch (cmd) {
	case SIOCETHTOOL:
		return eth_ethtool_ioctl (net, (void *)rq->ifr_data);
	default:
		return -EOPNOTSUPP;
	}
}

static void defer_kevent (struct eth_dev *dev, int flag)
{
	set_bit (flag, &dev->todo);
	if (!schedule_work (&dev->work))
		ERROR (dev, "kevent %d may have been dropped\n", flag);
	else
		DEBUG (dev, "kevent %d scheduled\n", flag);
}

static void rx_complete (struct usb_ep *ep, struct usb_request *req);

static int
rx_submit (struct eth_dev *dev, struct usb_request *req, int gfp_flags)
{
	struct sk_buff		*skb;
	int			retval = 0;
	size_t			size;

	size = (sizeof (struct ethhdr) + dev->net.mtu + RX_EXTRA);

	if ((skb = alloc_skb (size, gfp_flags)) == 0) {
		DEBUG (dev, "no rx skb\n");
		defer_kevent (dev, WORK_RX_MEMORY);
		usb_ep_free_request (dev->out_ep, req);
		return -ENOMEM;
	}

	req->buf = skb->data;
	req->length = size;
	req->complete = rx_complete;
	req->context = skb;

	if (netif_running (&dev->net)) {
		retval = usb_ep_queue (dev->out_ep, req, gfp_flags);
		if (retval == -ENOMEM)
			defer_kevent (dev, WORK_RX_MEMORY);
		if (retval)
			DEBUG (dev, "%s %d\n", __FUNCTION__, retval);
	} else {
		DEBUG (dev, "%s stopped\n", __FUNCTION__);
		retval = -ENOLINK;
	}
	if (retval) {
		DEBUG (dev, "rx submit --> %d\n", retval);
		dev_kfree_skb_any (skb);
		usb_ep_free_request (dev->out_ep, req);
	}
	return retval;
}

static void rx_complete (struct usb_ep *ep, struct usb_request *req)
{
	struct sk_buff	*skb = req->context;
	struct eth_dev	*dev = ep->driver_data;
	int		status = req->status;

	switch (status) {

	/* normal completion */
	case 0:
		skb_put (skb, req->actual);
		if (MIN_PACKET > skb->len
				|| skb->len > (MAX_PACKET + ETH_HLEN)) {
			dev->stats.rx_errors++;
			dev->stats.rx_length_errors++;
			DEBUG (dev, "rx length %d\n", skb->len);
			break;
		}

		skb->dev = &dev->net;
		skb->protocol = eth_type_trans (skb, &dev->net);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;

		/* no buffer copies needed, unless hardware can't
		 * use skb buffers.
		 */
		status = netif_rx (skb);
		skb = 0;
		break;

	/* software-driven interface shutdown */
	case -ECONNRESET:		// unlink
	case -ESHUTDOWN:		// disconnect etc
		VDEBUG (dev, "rx shutdown, code %d\n", status);
		usb_ep_free_request (dev->out_ep, req);
		req = 0;
		break;

	/* data overrun */
	case -EOVERFLOW:
		dev->stats.rx_over_errors++;
		// FALLTHROUGH
	    
	default:
		dev->stats.rx_errors++;
		DEBUG (dev, "rx status %d\n", status);
		break;
	}

	if (skb)
		dev_kfree_skb_any (skb);

	if (req)
		rx_submit (dev, req, GFP_ATOMIC);
}

static void eth_work (void *_dev)
{
	struct eth_dev		*dev = _dev;

	if (test_bit (WORK_RX_MEMORY, &dev->todo)) {
		struct usb_request	*req = 0;

		if (netif_running (&dev->net))
			req = usb_ep_alloc_request (dev->in_ep, GFP_KERNEL);
		else
			clear_bit (WORK_RX_MEMORY, &dev->todo);
		if (req != 0) {
			clear_bit (WORK_RX_MEMORY, &dev->todo);
			rx_submit (dev, req, GFP_KERNEL);
		}
	}

	if (dev->todo)
		DEBUG (dev, "work done, flags = 0x%lx\n", dev->todo);
}

static void tx_complete (struct usb_ep *ep, struct usb_request *req)
{
	struct sk_buff	*skb = req->context;
	struct eth_dev	*dev = ep->driver_data;

	if (req->status)
		dev->stats.tx_errors++;
	else
		dev->stats.tx_bytes += skb->len;
	dev->stats.tx_packets++;

	usb_ep_free_request (ep, req);
	dev_kfree_skb_any (skb);

	atomic_inc (&dev->tx_qlen);
	if (netif_carrier_ok (&dev->net))
		netif_wake_queue (&dev->net);
}

static int eth_start_xmit (struct sk_buff *skb, struct net_device *net)
{
	struct eth_dev		*dev = (struct eth_dev *) net->priv;
	int			length = skb->len;
	int			retval;
	struct usb_request	*req = 0;

	if (!(req = usb_ep_alloc_request (dev->in_ep, GFP_ATOMIC))) {
		DEBUG (dev, "no request\n");
		goto drop;
	}

	/* no buffer copies needed, unless the network stack did it
	 * or the hardware can't use skb buffers.
	 */
	req->buf = skb->data;
	req->context = skb;
	req->complete = tx_complete;

#ifdef	CONFIG_USB_ETH_SA1100
	/* don't demand zlp (req->zero) support from all hardware */
	if ((length % dev->in_ep->maxpacket) == 0)
		length++;
#else
	/* use zlp framing on tx for strict CDC-Ether conformance,
	 * though any robust network rx path ignores extra padding.
	 */
	req->zero = 1;
#endif
	req->length = length;

#ifdef	HIGHSPEED
	/* throttle highspeed IRQ rate back slightly */
	req->no_interrupt = (dev->gadget->speed == USB_SPEED_HIGH)
		? ((atomic_read (&dev->tx_qlen) % TX_DELAY) != 0)
		: 0;
#endif

	retval = usb_ep_queue (dev->in_ep, req, GFP_ATOMIC);
	switch (retval) {
	default:
		DEBUG (dev, "tx queue err %d\n", retval);
		break;
	case 0:
		net->trans_start = jiffies;
		if (atomic_dec_and_test (&dev->tx_qlen))
			netif_stop_queue (net);
	}

	if (retval) {
		DEBUG (dev, "drop, code %d\n", retval);
drop:
		dev->stats.tx_dropped++;
		dev_kfree_skb_any (skb);
		usb_ep_free_request (dev->in_ep, req);
	}
	return 0;
}

static void eth_start (struct eth_dev *dev, int gfp_flags)
{
	struct usb_request	*req;
	int			retval = 0;
	unsigned		i;
	int			size = qlen (dev->gadget);

	DEBUG (dev, "%s\n", __FUNCTION__);

	/* fill the rx queue */
	for (i = 0; retval == 0 && i < size; i++) {
		req = usb_ep_alloc_request (dev->in_ep, gfp_flags);
		if (req)
			retval = rx_submit (dev, req, gfp_flags);
		else if (i > 0)
			defer_kevent (dev, WORK_RX_MEMORY);
		else
			retval = -ENOMEM;
	}

	/* and open the tx floodgates */ 
	atomic_set (&dev->tx_qlen, size);
	netif_wake_queue (&dev->net);
}

static int eth_open (struct net_device *net)
{
	struct eth_dev		*dev = (struct eth_dev *) net->priv;

	DEBUG (dev, "%s\n", __FUNCTION__);
	down (&dev->mutex);
	if (netif_carrier_ok (&dev->net))
		eth_start (dev, GFP_KERNEL);
	up (&dev->mutex);
	return 0;
}

static int eth_stop (struct net_device *net)
{
	struct eth_dev		*dev = (struct eth_dev *) net->priv;

	DEBUG (dev, "%s\n", __FUNCTION__);
	down (&dev->mutex);
	netif_stop_queue (net);

	DEBUG (dev, "stop stats: rx/tx %ld/%ld, errs %ld/%ld\n",
		dev->stats.rx_packets, dev->stats.tx_packets, 
		dev->stats.rx_errors, dev->stats.tx_errors
		);

	/* ensure there are no more active requests */
	if (dev->gadget->speed != USB_SPEED_UNKNOWN) {
		usb_ep_disable (dev->in_ep);
		usb_ep_disable (dev->out_ep);
		if (netif_carrier_ok (&dev->net)) {
			DEBUG (dev, "host still using in/out endpoints\n");
			usb_ep_enable (dev->in_ep, dev->in);
			usb_ep_enable (dev->out_ep, dev->out);
		}
#ifdef	EP_STATUS_NUM
		usb_ep_disable (dev->status_ep);
		usb_ep_enable (dev->status_ep, dev->status);
#endif
	}

	up (&dev->mutex);
	return 0;
}

/*-------------------------------------------------------------------------*/

static void
eth_unbind (struct usb_gadget *gadget)
{
	struct eth_dev		*dev = get_gadget_data (gadget);

	DEBUG (dev, "unbind\n");
	down (&dev->mutex);

	/* we've already been disconnected ... no i/o is active */
	if (dev->req) {
		usb_ep_free_buffer (gadget->ep0,
				dev->req->buf, dev->req->dma,
				USB_BUFSIZ);
		usb_ep_free_request (gadget->ep0, dev->req);
	}

	unregister_netdev (&dev->net);
	up (&dev->mutex);

	/* assuming we used keventd, it must quiesce too */
	flush_scheduled_work ();

	kfree (dev);
	set_gadget_data (gadget, 0);
}

static int
eth_bind (struct usb_gadget *gadget)
{
	struct eth_dev		*dev;
	struct net_device	*net;
	u8			node_id [ETH_ALEN];

	/* just one upstream link at a time */
	if (ethaddr [0] != 0)
		return -ENODEV;

	dev = kmalloc (sizeof *dev, SLAB_KERNEL);
	if (!dev)
		return -ENOMEM;
	memset (dev, 0, sizeof *dev);
	spin_lock_init (&dev->lock);
	init_MUTEX_LOCKED (&dev->mutex);
	INIT_WORK (&dev->work, eth_work, dev);

	/* network device setup */
	net = &dev->net;
	SET_MODULE_OWNER (net);
	net->priv = dev;
	strcpy (net->name, "usb%d");
	ether_setup (net);

	/* one random address for the gadget device ... both of these could
	 * reasonably come from an id prom or a module parameter.
	 */
	get_random_bytes (net->dev_addr, ETH_ALEN);
	net->dev_addr [0] &= 0xfe;	// clear multicast bit
	net->dev_addr [0] |= 0x02;	// set local assignment bit (IEEE802)

	/* ... another address for the host, on the other end of the
	 * link, gets exported through CDC (see CDC spec table 41)
	 */
	get_random_bytes (node_id, sizeof node_id);
	node_id [0] &= 0xfe;	// clear multicast bit
	node_id [0] |= 0x02;    // set local assignment bit (IEEE802)
	snprintf (ethaddr, sizeof ethaddr, "%02X%02X%02X%02X%02X%02X",
		node_id [0], node_id [1], node_id [2],
		node_id [3], node_id [4], node_id [5]);

	net->change_mtu = eth_change_mtu;
	net->get_stats = eth_get_stats;
	net->hard_start_xmit = eth_start_xmit;
	net->open = eth_open;
	net->stop = eth_stop;
	// watchdog_timeo, tx_timeout ...
	// set_multicast_list
	net->do_ioctl = eth_ioctl;

	/* preallocate control response and buffer */
	dev->req = usb_ep_alloc_request (gadget->ep0, GFP_KERNEL);
	if (!dev->req)
		goto enomem;
	dev->req->complete = eth_setup_complete;
	dev->req->buf = usb_ep_alloc_buffer (gadget->ep0, USB_BUFSIZ,
				&dev->req->dma, GFP_KERNEL);
	if (!dev->req->buf) {
		usb_ep_free_request (gadget->ep0, dev->req);
		goto enomem;
	}

	/* finish hookup to lower layer ... */
	dev->gadget = gadget;
	set_gadget_data (gadget, dev);
	gadget->ep0->driver_data = dev;

	/* two kinds of host-initiated state changes:
	 *  - iff DATA transfer is active, carrier is "on"
	 *  - tx queueing enabled if open *and* carrier is "on"
	 */
	INFO (dev, "%s, host enet %s, version: " DRIVER_VERSION "\n",
			driver_desc, ethaddr);
	register_netdev (&dev->net);
	netif_stop_queue (&dev->net);
	netif_carrier_off (&dev->net);

	up (&dev->mutex);
	return 0;

enomem:
	eth_unbind (gadget);
	return -ENOMEM;
}

/*-------------------------------------------------------------------------*/

static struct usb_gadget_driver eth_driver = {
#ifdef HIGHSPEED
	.speed		= USB_SPEED_HIGH,
#else
	.speed		= USB_SPEED_FULL,
#endif
	.function	= (char *) driver_desc,
	.bind		= eth_bind,
	.unbind		= eth_unbind,

	.setup		= eth_setup,
	.disconnect	= eth_disconnect,

	.driver 	= {
		.name		= (char *) shortname,
		// .shutdown = ...
		// .suspend = ...
		// .resume = ...
	},
};

MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_AUTHOR ("David Brownell");
MODULE_LICENSE ("GPL");


static int __init init (void)
{
	return usb_gadget_register_driver (&eth_driver);
}
module_init (init);

static void __exit cleanup (void)
{
	usb_gadget_unregister_driver (&eth_driver);
}
module_exit (cleanup);

