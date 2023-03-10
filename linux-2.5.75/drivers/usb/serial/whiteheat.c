/*
 * USB ConnectTech WhiteHEAT driver
 *
 *	Copyright (C) 2002
 *	    Connect Tech Inc.
 *
 *	Copyright (C) 1999 - 2001
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * (10/09/2002) Stuart MacDonald (stuartm@connecttech.com)
 *	Upgrade to full working driver
 *
 * (05/30/2001) gkh
 *	switched from using spinlock to a semaphore, which fixes lots of problems.
 *
 * (04/08/2001) gb
 *	Identify version on module load.
 * 
 * 2001_Mar_19 gkh
 *	Fixed MOD_INC and MOD_DEC logic, the ability to open a port more 
 *	than once, and the got the proper usb_device_id table entries so
 *	the driver works again.
 *
 * (11/01/2000) Adam J. Richter
 *	usb_device_id table support
 * 
 * (10/05/2000) gkh
 *	Fixed bug with urb->dev not being set properly, now that the usb
 *	core needs it.
 * 
 * (10/03/2000) smd
 *	firmware is improved to guard against crap sent to device
 *	firmware now replies CMD_FAILURE on bad things
 *	read_callback fix you provided for private info struct
 *	command_finished now indicates success or fail
 *	setup_port struct now packed to avoid gcc padding
 *	firmware uses 1 based port numbering, driver now handles that
 *
 * (09/11/2000) gkh
 *	Removed DEBUG #ifdefs with call to usb_serial_debug_data
 *
 * (07/19/2000) gkh
 *	Added module_init and module_exit functions to handle the fact that this
 *	driver is a loadable module now.
 *	Fixed bug with port->minor that was found by Al Borchers
 *
 * (07/04/2000) gkh
 *	Added support for port settings. Baud rate can now be changed. Line signals
 *	are not transferred to and from the tty layer yet, but things seem to be 
 *	working well now.
 *
 * (05/04/2000) gkh
 *	First cut at open and close commands. Data can flow through the ports at
 *	default speeds now.
 *
 * (03/26/2000) gkh
 *	Split driver up into device specific pieces.
 * 
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/serial_reg.h>
#include <linux/serial.h>

#ifdef CONFIG_USB_SERIAL_DEBUG
	static int debug = 1;
#else
	static int debug;
#endif

#include "usb-serial.h"
#include "whiteheat_fw.h"		/* firmware for the ConnectTech WhiteHEAT device */
#include "whiteheat.h"			/* WhiteHEAT specific commands */

/*
 * Version Information
 */
#define DRIVER_VERSION "v2.0"
#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com>, Stuart MacDonald <stuartm@connecttech.com>"
#define DRIVER_DESC "USB ConnectTech WhiteHEAT driver"

#define CONNECT_TECH_VENDOR_ID		0x0710
#define CONNECT_TECH_FAKE_WHITE_HEAT_ID	0x0001
#define CONNECT_TECH_WHITE_HEAT_ID	0x8001

/*
   ID tables for whiteheat are unusual, because we want to different
   things for different versions of the device.  Eventually, this
   will be doable from a single table.  But, for now, we define two
   separate ID tables, and then a third table that combines them
   just for the purpose of exporting the autoloading information.
*/
static struct usb_device_id id_table_std [] = {
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_WHITE_HEAT_ID) },
	{ }						/* Terminating entry */
};

static struct usb_device_id id_table_prerenumeration [] = {
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_FAKE_WHITE_HEAT_ID) },
	{ }						/* Terminating entry */
};

static struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_WHITE_HEAT_ID) },
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_FAKE_WHITE_HEAT_ID) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);

static struct usb_driver whiteheat_driver = {
	.owner =	THIS_MODULE,
	.name =		"whiteheat",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
};

/* function prototypes for the Connect Tech WhiteHEAT prerenumeration device */
static int  whiteheat_firmware_download	(struct usb_serial *serial, const struct usb_device_id *id);
static int  whiteheat_firmware_attach	(struct usb_serial *serial);

/* function prototypes for the Connect Tech WhiteHEAT serial converter */
static int  whiteheat_attach		(struct usb_serial *serial);
static void whiteheat_shutdown		(struct usb_serial *serial);
static int  whiteheat_open		(struct usb_serial_port *port, struct file *filp);
static void whiteheat_close		(struct usb_serial_port *port, struct file *filp);
static int  whiteheat_write		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
static int  whiteheat_write_room	(struct usb_serial_port *port);
static int  whiteheat_ioctl		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
static void whiteheat_set_termios	(struct usb_serial_port *port, struct termios * old);
static int  whiteheat_tiocmget		(struct usb_serial_port *port, struct file *file);
static int  whiteheat_tiocmset		(struct usb_serial_port *port, struct file *file, unsigned int set, unsigned int clear);
static void whiteheat_break_ctl		(struct usb_serial_port *port, int break_state);
static int  whiteheat_chars_in_buffer	(struct usb_serial_port *port);
static void whiteheat_throttle		(struct usb_serial_port *port);
static void whiteheat_unthrottle	(struct usb_serial_port *port);
static void whiteheat_read_callback	(struct urb *urb, struct pt_regs *regs);
static void whiteheat_write_callback	(struct urb *urb, struct pt_regs *regs);

static struct usb_serial_device_type whiteheat_fake_device = {
	.owner =		THIS_MODULE,
	.name =			"Connect Tech - WhiteHEAT - (prerenumeration)",
	.short_name =		"whiteheatnofirm",
	.id_table =		id_table_prerenumeration,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		NUM_DONT_CARE,
	.num_bulk_out =		NUM_DONT_CARE,
	.num_ports =		1,
	.probe =		whiteheat_firmware_download,
	.attach =		whiteheat_firmware_attach,
};

static struct usb_serial_device_type whiteheat_device = {
	.owner =		THIS_MODULE,
	.name =			"Connect Tech - WhiteHEAT",
	.short_name =		"whiteheat",
	.id_table =		id_table_std,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		NUM_DONT_CARE,
	.num_bulk_out =		NUM_DONT_CARE,
	.num_ports =		4,
	.attach =		whiteheat_attach,
	.shutdown =		whiteheat_shutdown,
	.open =			whiteheat_open,
	.close =		whiteheat_close,
	.write =		whiteheat_write,
	.write_room =		whiteheat_write_room,
	.ioctl =		whiteheat_ioctl,
	.set_termios =		whiteheat_set_termios,
	.break_ctl =		whiteheat_break_ctl,
	.tiocmget =		whiteheat_tiocmget,
	.tiocmset =		whiteheat_tiocmset,
	.chars_in_buffer =	whiteheat_chars_in_buffer,
	.throttle =		whiteheat_throttle,
	.unthrottle =		whiteheat_unthrottle,
	.read_bulk_callback =	whiteheat_read_callback,
	.write_bulk_callback =	whiteheat_write_callback,
};


struct whiteheat_command_private {
	spinlock_t		lock;
	__u8			port_running;
	__u8			command_finished;
	wait_queue_head_t	wait_command;	/* for handling sleeping while waiting for a command to finish */
	__u8			result_buffer[64];
};


#define THROTTLED		0x01
#define ACTUALLY_THROTTLED	0x02

static int urb_pool_size = 8;

struct whiteheat_urb_wrap {
	struct list_head	list;
	struct urb		*urb;
};

struct whiteheat_private {
	spinlock_t		lock;
	__u8			flags;
	__u8			mcr;
	struct list_head	rx_urbs_free;
	struct list_head	rx_urbs_submitted;
	struct list_head	rx_urb_q;
	struct work_struct	rx_work;
	struct list_head	tx_urbs_free;
	struct list_head	tx_urbs_submitted;
};


/* local function prototypes */
static int start_command_port(struct usb_serial *serial);
static void stop_command_port(struct usb_serial *serial);
static void command_port_write_callback(struct urb *urb, struct pt_regs *regs);
static void command_port_read_callback(struct urb *urb, struct pt_regs *regs);

static int start_port_read(struct usb_serial_port *port);
static struct whiteheat_urb_wrap *urb_to_wrap(struct urb *urb, struct list_head *head);
static struct list_head *list_first(struct list_head *head);
static void rx_data_softint(void *private);

static int firm_send_command(struct usb_serial_port *port, __u8 command, __u8 *data, __u8 datasize);
static int firm_open(struct usb_serial_port *port);
static int firm_close(struct usb_serial_port *port);
static int firm_setup_port(struct usb_serial_port *port);
static int firm_set_rts(struct usb_serial_port *port, __u8 onoff);
static int firm_set_dtr(struct usb_serial_port *port, __u8 onoff);
static int firm_set_break(struct usb_serial_port *port, __u8 onoff);
static int firm_purge(struct usb_serial_port *port, __u8 rxtx);
static int firm_get_dtr_rts(struct usb_serial_port *port);
static int firm_report_tx_done(struct usb_serial_port *port);


#define COMMAND_PORT		4
#define COMMAND_TIMEOUT		(2*HZ)	/* 2 second timeout for a command */
#define CLOSING_DELAY		(30 * HZ)


/*****************************************************************************
 * Connect Tech's White Heat prerenumeration driver functions
 *****************************************************************************/

/* steps to download the firmware to the WhiteHEAT device:
 - hold the reset (by writing to the reset bit of the CPUCS register)
 - download the VEND_AX.HEX file to the chip using VENDOR_REQUEST-ANCHOR_LOAD
 - release the reset (by writing to the CPUCS register)
 - download the WH.HEX file for all addresses greater than 0x1b3f using
   VENDOR_REQUEST-ANCHOR_EXTERNAL_RAM_LOAD
 - hold the reset
 - download the WH.HEX file for all addresses less than 0x1b40 using
   VENDOR_REQUEST_ANCHOR_LOAD
 - release the reset
 - device renumerated itself and comes up as new device id with all
   firmware download completed.
*/
static int whiteheat_firmware_download (struct usb_serial *serial, const struct usb_device_id *id)
{
	int response;
	const struct whiteheat_hex_record *record;
	
	dbg("%s", __FUNCTION__);
	
	response = ezusb_set_reset (serial, 1);

	record = &whiteheat_loader[0];
	while (record->address != 0xffff) {
		response = ezusb_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa0);
		if (response < 0) {
			err("%s - ezusb_writememory failed for loader (%d %04X %p %d)",
				__FUNCTION__, response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}

	response = ezusb_set_reset (serial, 0);

	record = &whiteheat_firmware[0];
	while (record->address < 0x1b40) {
		++record;
	}
	while (record->address != 0xffff) {
		response = ezusb_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa3);
		if (response < 0) {
			err("%s - ezusb_writememory failed for first firmware step (%d %04X %p %d)", 
				__FUNCTION__, response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}
	
	response = ezusb_set_reset (serial, 1);

	record = &whiteheat_firmware[0];
	while (record->address < 0x1b40) {
		response = ezusb_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa0);
		if (response < 0) {
			err("%s - ezusb_writememory failed for second firmware step (%d %04X %p %d)", 
				__FUNCTION__, response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}

	response = ezusb_set_reset (serial, 0);

	return 0;
}


static int whiteheat_firmware_attach (struct usb_serial *serial)
{
	/* We want this device to fail to have a driver assigned to it */
	return 1;
}


/*****************************************************************************
 * Connect Tech's White Heat serial driver functions
 *****************************************************************************/
static int whiteheat_attach (struct usb_serial *serial)
{
	struct usb_serial_port *command_port;
	struct whiteheat_command_private *command_info;
	struct usb_serial_port *port;
	struct whiteheat_private *info;
	struct whiteheat_hw_info *hw_info;
	int pipe;
	int ret;
	int alen;
	__u8 command[2] = { WHITEHEAT_GET_HW_INFO, 0 };
	__u8 result[sizeof(*hw_info) + 1];
	int i;
	int j;
	struct urb *urb;
	int buf_size;
	struct whiteheat_urb_wrap *wrap;
	struct list_head *tmp;

	command_port = &serial->port[COMMAND_PORT];

	pipe = usb_sndbulkpipe (serial->dev, command_port->bulk_out_endpointAddress);
	/*
	 * When the module is reloaded the firmware is still there and
	 * the endpoints are still in the usb core unchanged. This is the
         * unlinking bug in disguise. Same for the call below.
         */
	usb_clear_halt(serial->dev, pipe);
	ret = usb_bulk_msg (serial->dev, pipe, command, sizeof(command), &alen, COMMAND_TIMEOUT);
	if (ret) {
		err("%s: Couldn't send command [%d]", serial->type->name, ret);
		goto no_firmware;
	} else if (alen != sizeof(command)) {
		err("%s: Send command incomplete [%d]", serial->type->name, alen);
		goto no_firmware;
	}

	pipe = usb_rcvbulkpipe (serial->dev, command_port->bulk_in_endpointAddress);
	/* See the comment on the usb_clear_halt() above */
	usb_clear_halt(serial->dev, pipe);
	ret = usb_bulk_msg (serial->dev, pipe, result, sizeof(result), &alen, COMMAND_TIMEOUT);
	if (ret) {
		err("%s: Couldn't get results [%d]", serial->type->name, ret);
		goto no_firmware;
	} else if (alen != sizeof(result)) {
		err("%s: Get results incomplete [%d]", serial->type->name, alen);
		goto no_firmware;
	} else if (result[0] != command[0]) {
		err("%s: Command failed [%d]", serial->type->name, result[0]);
		goto no_firmware;
	}

	hw_info = (struct whiteheat_hw_info *)&result[1];

	info("%s: Driver %s: Firmware v%d.%02d", serial->type->name,
	     DRIVER_VERSION, hw_info->sw_major_rev, hw_info->sw_minor_rev);

	for (i = 0; i < serial->num_ports; i++) {
		port = &serial->port[i];

		info = (struct whiteheat_private *)kmalloc(sizeof(struct whiteheat_private), GFP_KERNEL);
		if (info == NULL) {
			err("%s: Out of memory for port structures\n", serial->type->name);
			goto no_private;
		}

		spin_lock_init(&info->lock);
		info->flags = 0;
		info->mcr = 0;
		INIT_WORK(&info->rx_work, rx_data_softint, port);

		INIT_LIST_HEAD(&info->rx_urbs_free);
		INIT_LIST_HEAD(&info->rx_urbs_submitted);
		INIT_LIST_HEAD(&info->rx_urb_q);
		INIT_LIST_HEAD(&info->tx_urbs_free);
		INIT_LIST_HEAD(&info->tx_urbs_submitted);

		for (j = 0; j < urb_pool_size; j++) {
			urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!urb) {
				err("No free urbs available");
				goto no_rx_urb;
			}
			buf_size = port->read_urb->transfer_buffer_length;
			urb->transfer_buffer = kmalloc(buf_size, GFP_KERNEL);
			if (!urb->transfer_buffer) {
				err("Couldn't allocate urb buffer");
				goto no_rx_buf;
			}
			wrap = kmalloc(sizeof(*wrap), GFP_KERNEL);
			if (!wrap) {
				err("Couldn't allocate urb wrapper");
				goto no_rx_wrap;
			}
			usb_fill_bulk_urb(urb, serial->dev,
					usb_rcvbulkpipe(serial->dev,
						port->bulk_in_endpointAddress),
					urb->transfer_buffer, buf_size,
					whiteheat_read_callback, port);
			wrap->urb = urb;
			list_add(&wrap->list, &info->rx_urbs_free);

			urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!urb) {
				err("No free urbs available");
				goto no_tx_urb;
			}
			buf_size = port->write_urb->transfer_buffer_length;
			urb->transfer_buffer = kmalloc(buf_size, GFP_KERNEL);
			if (!urb->transfer_buffer) {
				err("Couldn't allocate urb buffer");
				goto no_tx_buf;
			}
			wrap = kmalloc(sizeof(*wrap), GFP_KERNEL);
			if (!wrap) {
				err("Couldn't allocate urb wrapper");
				goto no_tx_wrap;
			}
			usb_fill_bulk_urb(urb, serial->dev,
					usb_sndbulkpipe(serial->dev,
						port->bulk_out_endpointAddress),
					urb->transfer_buffer, buf_size,
					whiteheat_write_callback, port);
			wrap->urb = urb;
			list_add(&wrap->list, &info->tx_urbs_free);
		}

		usb_set_serial_port_data(port, info);
	}

	command_info = (struct whiteheat_command_private *)kmalloc(sizeof(struct whiteheat_command_private), GFP_KERNEL);
	if (command_info == NULL) {
		err("%s: Out of memory for port structures\n", serial->type->name);
		goto no_command_private;
	}

	spin_lock_init(&command_info->lock);
	command_info->port_running = 0;
	init_waitqueue_head(&command_info->wait_command);
	usb_set_serial_port_data(command_port, command_info);
	command_port->write_urb->complete = command_port_write_callback;
	command_port->read_urb->complete = command_port_read_callback;

	return 0;

no_firmware:
	/* Firmware likely not running */
	err("%s: Unable to retrieve firmware version, try replugging\n", serial->type->name);
	err("%s: If the firmware is not running (status led not blinking)\n", serial->type->name);
	err("%s: please contact support@connecttech.com\n", serial->type->name);
	return -ENODEV;

no_command_private:
	for (i = serial->num_ports - 1; i >= 0; i--) {
		port = &serial->port[i];
		info = usb_get_serial_port_data(port);
		for (j = urb_pool_size - 1; j >= 0; j--) {
			tmp = list_first(&info->tx_urbs_free);
			list_del(tmp);
			wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
			urb = wrap->urb;
			kfree(wrap);
no_tx_wrap:
			kfree(urb->transfer_buffer);
no_tx_buf:
			usb_free_urb(urb);
no_tx_urb:
			tmp = list_first(&info->rx_urbs_free);
			list_del(tmp);
			wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
			urb = wrap->urb;
			kfree(wrap);
no_rx_wrap:
			kfree(urb->transfer_buffer);
no_rx_buf:
			usb_free_urb(urb);
no_rx_urb:
			;
		}
		kfree(info);
no_private:
		;
	}
	return -ENOMEM;
}


static void whiteheat_shutdown (struct usb_serial *serial)
{
	struct usb_serial_port *command_port;
	struct usb_serial_port *port;
	struct whiteheat_private *info;
	struct whiteheat_urb_wrap *wrap;
	struct urb *urb;
	struct list_head *tmp;
	struct list_head *tmp2;
	int i;

	dbg("%s", __FUNCTION__);

	/* free up our private data for our command port */
	command_port = &serial->port[COMMAND_PORT];
	kfree (usb_get_serial_port_data(command_port));

	for (i = 0; i < serial->num_ports; i++) {
		port = &serial->port[i];
		info = usb_get_serial_port_data(port);
		list_for_each_safe(tmp, tmp2, &info->rx_urbs_free) {
			list_del(tmp);
			wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
			urb = wrap->urb;
			kfree(wrap);
			kfree(urb->transfer_buffer);
			usb_free_urb(urb);
		}
		list_for_each_safe(tmp, tmp2, &info->tx_urbs_free) {
			list_del(tmp);
			wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
			urb = wrap->urb;
			kfree(wrap);
			kfree(urb->transfer_buffer);
			usb_free_urb(urb);
		}
		kfree(info);
	}

	return;
}


static int whiteheat_open (struct usb_serial_port *port, struct file *filp)
{
	int		retval = 0;
	struct termios	old_term;

	dbg("%s - port %d", __FUNCTION__, port->number);

	retval = start_command_port(port->serial);
	if (retval)
		goto exit;

	if (port->tty)
		port->tty->low_latency = 1;

	/* send an open port command */
	retval = firm_open(port);
	if (retval) {
		stop_command_port(port->serial);
		goto exit;
	}

	retval = firm_purge(port, WHITEHEAT_PURGE_RX | WHITEHEAT_PURGE_TX);
	if (retval) {
		firm_close(port);
		stop_command_port(port->serial);
		goto exit;
	}

	old_term.c_cflag = ~port->tty->termios->c_cflag;
	old_term.c_iflag = ~port->tty->termios->c_iflag;
	whiteheat_set_termios(port, &old_term);

	/* Work around HCD bugs */
	usb_clear_halt(port->serial->dev, port->read_urb->pipe);
	usb_clear_halt(port->serial->dev, port->write_urb->pipe);

	/* Start reading from the device */
	retval = start_port_read(port);
	if (retval) {
		err("%s - failed submitting read urb, error %d", __FUNCTION__, retval);
		firm_close(port);
		stop_command_port(port->serial);
		goto exit;
	}

exit:
	dbg("%s - exit, retval = %d", __FUNCTION__, retval);
	return retval;
}


static void whiteheat_close(struct usb_serial_port *port, struct file * filp)
{
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct whiteheat_urb_wrap *wrap;
	struct urb *urb;
	struct list_head *tmp;
	struct list_head *tmp2;
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);
	
	/* filp is NULL when called from usb_serial_disconnect */
	if (filp && (tty_hung_up_p(filp))) {
		return;
	}

	port->tty->closing = 1;

/*
 * Not currently in use; tty_wait_until_sent() calls
 * serial_chars_in_buffer() which deadlocks on the second semaphore
 * acquisition. This should be fixed at some point. Greg's been
 * notified.
	if ((filp->f_flags & (O_NDELAY | O_NONBLOCK)) == 0) {
		tty_wait_until_sent(port->tty, CLOSING_DELAY);
	}
*/

	if (port->tty->driver->flush_buffer)
		port->tty->driver->flush_buffer(port->tty);
	if (port->tty->ldisc.flush_buffer)
		port->tty->ldisc.flush_buffer(port->tty);

	firm_report_tx_done(port);

	firm_close(port);

	/* shutdown our bulk reads and writes */
	spin_lock_irqsave(&info->lock, flags);
	list_for_each_safe(tmp, tmp2, &info->rx_urbs_submitted) {
		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		urb = wrap->urb;
		usb_unlink_urb(urb);
		list_del(tmp);
		list_add(tmp, &info->rx_urbs_free);
	}
	list_for_each_safe(tmp, tmp2, &info->rx_urb_q) {
		list_del(tmp);
		list_add(tmp, &info->rx_urbs_free);
	}
	list_for_each_safe(tmp, tmp2, &info->tx_urbs_submitted) {
		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		urb = wrap->urb;
		usb_unlink_urb(urb);
		list_del(tmp);
		list_add(tmp, &info->tx_urbs_free);
	}
	spin_unlock_irqrestore(&info->lock, flags);

	stop_command_port(port->serial);

	port->tty->closing = 0;
}


static int whiteheat_write(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial *serial = port->serial;
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct whiteheat_urb_wrap *wrap;
	struct urb *urb;
	int result;
	int bytes;
	int sent = 0;
	unsigned long flags;
	struct list_head *tmp;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (count == 0) {
		dbg("%s - write request of 0 bytes", __FUNCTION__);
		return (0);
	}

	while (count) {
		spin_lock_irqsave(&info->lock, flags);
		if (list_empty(&info->tx_urbs_free)) {
			spin_unlock_irqrestore(&info->lock, flags);
			break;
		}
		tmp = list_first(&info->tx_urbs_free);
		list_del(tmp);
		spin_unlock_irqrestore(&info->lock, flags);

		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		urb = wrap->urb;
		bytes = (count > port->bulk_out_size) ? port->bulk_out_size : count;
		if (from_user) {
			if (copy_from_user(urb->transfer_buffer, buf + sent, bytes))
				return -EFAULT;
		} else {
			memcpy (urb->transfer_buffer, buf + sent, bytes);
		}

		usb_serial_debug_data (__FILE__, __FUNCTION__, bytes, urb->transfer_buffer);

		urb->dev = serial->dev;
		urb->transfer_buffer_length = bytes;
		result = usb_submit_urb(urb, GFP_ATOMIC);
		if (result) {
			err("%s - failed submitting write urb, error %d", __FUNCTION__, result);
			sent = result;
			spin_lock_irqsave(&info->lock, flags);
			list_add(tmp, &info->tx_urbs_free);
			spin_unlock_irqrestore(&info->lock, flags);
			break;
		} else {
			sent += bytes;
			count -= bytes;
			spin_lock_irqsave(&info->lock, flags);
			list_add(tmp, &info->tx_urbs_submitted);
			spin_unlock_irqrestore(&info->lock, flags);
		}
	}

	return sent;
}


static int whiteheat_write_room(struct usb_serial_port *port)
{
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct list_head *tmp;
	int room = 0;
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);
	
	spin_lock_irqsave(&info->lock, flags);
	list_for_each(tmp, &info->tx_urbs_free)
		room++;
	spin_unlock_irqrestore(&info->lock, flags);
	room *= port->bulk_out_size;

	dbg("%s - returns %d", __FUNCTION__, room);
	return (room);
}


static int whiteheat_tiocmget (struct usb_serial_port *port, struct file *file)
{
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	unsigned int modem_signals = 0;

	dbg("%s - port %d", __FUNCTION__, port->number);

	firm_get_dtr_rts(port);
	if (info->mcr & UART_MCR_DTR)
		modem_signals |= TIOCM_DTR;
	if (info->mcr & UART_MCR_RTS)
		modem_signals |= TIOCM_RTS;

	return modem_signals;
}


static int whiteheat_tiocmset (struct usb_serial_port *port, struct file *file,
			       unsigned int set, unsigned int clear)
{
	struct whiteheat_private *info = usb_get_serial_port_data(port);

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (set & TIOCM_RTS)
		info->mcr |= UART_MCR_RTS;
	if (set & TIOCM_DTR)
		info->mcr |= UART_MCR_DTR;

	if (clear & TIOCM_RTS)
		info->mcr &= ~UART_MCR_RTS;
	if (clear & TIOCM_DTR)
		info->mcr &= ~UART_MCR_DTR;

	firm_set_dtr(port, info->mcr & UART_MCR_DTR);
	firm_set_rts(port, info->mcr & UART_MCR_RTS);
	return 0;
}


static int whiteheat_ioctl (struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct serial_struct serstruct;

	dbg("%s - port %d, cmd 0x%.4x", __FUNCTION__, port->number, cmd);

	switch (cmd) {
		case TIOCGSERIAL:
			memset(&serstruct, 0, sizeof(serstruct));
			serstruct.type = PORT_16654;
			serstruct.line = port->serial->minor;
			serstruct.port = port->number;
			serstruct.flags = ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
			serstruct.xmit_fifo_size = port->bulk_out_size;
			serstruct.custom_divisor = 0;
			serstruct.baud_base = 460800;
			serstruct.close_delay = CLOSING_DELAY;
			serstruct.closing_wait = CLOSING_DELAY;

			if (copy_to_user((void *)arg, &serstruct, sizeof(serstruct)))
				return -EFAULT;

			break;

		case TIOCSSERIAL:
			if (copy_from_user(&serstruct, (void *)arg, sizeof(serstruct)))
				return -EFAULT;

			/*
			 * For now this is ignored. dip sets the ASYNC_[V]HI flags
			 * but this isn't used by us at all. Maybe someone somewhere
			 * will need the custom_divisor setting.
			 */

			break;

		default:
			break;
	}

	return -ENOIOCTLCMD;
}


static void whiteheat_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{
	dbg("%s -port %d", __FUNCTION__, port->number);

	if ((!port->tty) || (!port->tty->termios)) {
		dbg("%s - no tty structures", __FUNCTION__);
		goto exit;
	}
	
	/* check that they really want us to change something */
	if (old_termios) {
		if ((port->tty->termios->c_cflag == old_termios->c_cflag) &&
		    (port->tty->termios->c_iflag == old_termios->c_iflag)) {
			dbg("%s - nothing to change...", __FUNCTION__);
			goto exit;
		}
	}

	firm_setup_port(port);

exit:
	return;
}


static void whiteheat_break_ctl(struct usb_serial_port *port, int break_state) {
	firm_set_break(port, break_state);
}


static int whiteheat_chars_in_buffer(struct usb_serial_port *port)
{
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct list_head *tmp;
	struct whiteheat_urb_wrap *wrap;
	int chars = 0;
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&info->lock, flags);
	list_for_each(tmp, &info->tx_urbs_submitted) {
		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		chars += wrap->urb->transfer_buffer_length;
	}
	spin_unlock_irqrestore(&info->lock, flags);

	dbg ("%s - returns %d", __FUNCTION__, chars);
	return (chars);
}


static void whiteheat_throttle (struct usb_serial_port *port)
{
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&info->lock, flags);
	info->flags |= THROTTLED;
	spin_unlock_irqrestore(&info->lock, flags);

	return;
}


static void whiteheat_unthrottle (struct usb_serial_port *port)
{
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	int actually_throttled;
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&info->lock, flags);
	actually_throttled = info->flags & ACTUALLY_THROTTLED;
	info->flags &= ~(THROTTLED | ACTUALLY_THROTTLED);
	spin_unlock_irqrestore(&info->lock, flags);

	if (actually_throttled)
		rx_data_softint(port);

	return;
}


/*****************************************************************************
 * Connect Tech's White Heat callback routines
 *****************************************************************************/
static void command_port_write_callback (struct urb *urb, struct pt_regs *regs)
{
	dbg("%s", __FUNCTION__);

	if (urb->status) {
		dbg ("nonzero urb status: %d", urb->status);
		return;
	}

	usb_serial_debug_data (__FILE__, __FUNCTION__, urb->actual_length, urb->transfer_buffer);

	return;
}


static void command_port_read_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *command_port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = get_usb_serial (command_port, __FUNCTION__);
	struct whiteheat_command_private *command_info;
	unsigned char *data = urb->transfer_buffer;
	int result;
	unsigned long flags;

	dbg("%s", __FUNCTION__);

	if (urb->status) {
		dbg("%s - nonzero urb status: %d", __FUNCTION__, urb->status);
		return;
	}

	if (!serial) {
		dbg("%s - bad serial pointer, exiting", __FUNCTION__);
		return;
	}
	
	usb_serial_debug_data (__FILE__, __FUNCTION__, urb->actual_length, data);

	command_info = usb_get_serial_port_data(command_port);
	if (!command_info) {
		dbg ("%s - command_info is NULL, exiting.", __FUNCTION__);
		return;
	}
	spin_lock_irqsave(&command_info->lock, flags);

	if (data[0] == WHITEHEAT_CMD_COMPLETE) {
		command_info->command_finished = WHITEHEAT_CMD_COMPLETE;
		wake_up_interruptible(&command_info->wait_command);
	} else if (data[0] == WHITEHEAT_CMD_FAILURE) {
		command_info->command_finished = WHITEHEAT_CMD_FAILURE;
		wake_up_interruptible(&command_info->wait_command);
	} else if (data[0] == WHITEHEAT_EVENT) {
		/* These are unsolicited reports from the firmware, hence no waiting command to wakeup */
		dbg("%s - event received", __FUNCTION__);
	} else if (data[0] == WHITEHEAT_GET_DTR_RTS) {
		memcpy(command_info->result_buffer, &data[1], urb->actual_length - 1);
		command_info->command_finished = WHITEHEAT_CMD_COMPLETE;
		wake_up_interruptible(&command_info->wait_command);
	} else {
		dbg("%s - bad reply from firmware", __FUNCTION__);
	}
	
	/* Continue trying to always read */
	command_port->read_urb->dev = serial->dev;
	result = usb_submit_urb(command_port->read_urb, GFP_ATOMIC);
	spin_unlock_irqrestore(&command_info->lock, flags);
	if (result)
		dbg("%s - failed resubmitting read urb, error %d", __FUNCTION__, result);
}


static void whiteheat_read_callback(struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = get_usb_serial (port, __FUNCTION__);
	struct whiteheat_urb_wrap *wrap;
	unsigned char *data = urb->transfer_buffer;
	struct whiteheat_private *info = usb_get_serial_port_data(port);

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock(&info->lock);
	wrap = urb_to_wrap(urb, &info->rx_urbs_submitted);
	if (!wrap) {
		spin_unlock(&info->lock);
		err("%s - Not my urb!", __FUNCTION__);
		return;
	}
	list_del(&wrap->list);
	spin_unlock(&info->lock);

	if (!serial) {
		dbg("%s - bad serial pointer, exiting", __FUNCTION__);
		spin_lock(&info->lock);
		list_add(&wrap->list, &info->rx_urbs_free);
		spin_unlock(&info->lock);
		return;
	}

	if (urb->status) {
		dbg("%s - nonzero read bulk status received: %d", __FUNCTION__, urb->status);
		spin_lock(&info->lock);
		list_add(&wrap->list, &info->rx_urbs_free);
		spin_unlock(&info->lock);
		return;
	}

	usb_serial_debug_data (__FILE__, __FUNCTION__, urb->actual_length, data);

	spin_lock(&info->lock);
	list_add_tail(&wrap->list, &info->rx_urb_q);
	if (info->flags & THROTTLED) {
		info->flags |= ACTUALLY_THROTTLED;
		spin_unlock(&info->lock);
		return;
	}
	spin_unlock(&info->lock);

	schedule_work(&info->rx_work);
}


static void whiteheat_write_callback(struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = get_usb_serial (port, __FUNCTION__);
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct whiteheat_urb_wrap *wrap;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock(&info->lock);
	wrap = urb_to_wrap(urb, &info->tx_urbs_submitted);
	if (!wrap) {
		spin_unlock(&info->lock);
		err("%s - Not my urb!", __FUNCTION__);
		return;
	}
	list_del(&wrap->list);
	list_add(&wrap->list, &info->tx_urbs_free);
	spin_unlock(&info->lock);

	if (!serial) {
		dbg("%s - bad serial pointer, exiting", __FUNCTION__);
		return;
	}

	if (urb->status) {
		dbg("%s - nonzero write bulk status received: %d", __FUNCTION__, urb->status);
		return;
	}

	usb_serial_port_softint((void *)port);

	schedule_work(&port->work);
}


/*****************************************************************************
 * Connect Tech's White Heat firmware interface
 *****************************************************************************/
static int firm_send_command (struct usb_serial_port *port, __u8 command, __u8 *data, __u8 datasize)
{
	struct usb_serial_port *command_port;
	struct whiteheat_command_private *command_info;
	struct whiteheat_private *info;
	int timeout;
	__u8 *transfer_buffer;
	int retval = 0;
	unsigned long flags;

	dbg("%s - command %d", __FUNCTION__, command);

	command_port = &port->serial->port[COMMAND_PORT];
	command_info = usb_get_serial_port_data(command_port);
	spin_lock_irqsave(&command_info->lock, flags);
	command_info->command_finished = FALSE;
	
	transfer_buffer = (__u8 *)command_port->write_urb->transfer_buffer;
	transfer_buffer[0] = command;
	memcpy (&transfer_buffer[1], data, datasize);
	command_port->write_urb->transfer_buffer_length = datasize + 1;
	command_port->write_urb->dev = port->serial->dev;
	retval = usb_submit_urb (command_port->write_urb, GFP_KERNEL);
	spin_unlock_irqrestore(&command_info->lock, flags);
	if (retval) {
		dbg("%s - submit urb failed", __FUNCTION__);
		goto exit;
	}

	/* wait for the command to complete */
	timeout = COMMAND_TIMEOUT;
	while (timeout && (command_info->command_finished == FALSE)) {
		timeout = interruptible_sleep_on_timeout (&command_info->wait_command, timeout);
	}

	spin_lock_irqsave(&command_info->lock, flags);

	if (command_info->command_finished == FALSE) {
		dbg("%s - command timed out.", __FUNCTION__);
		retval = -ETIMEDOUT;
		goto exit;
	}

	if (command_info->command_finished == WHITEHEAT_CMD_FAILURE) {
		dbg("%s - command failed.", __FUNCTION__);
		retval = -EIO;
		goto exit;
	}

	if (command_info->command_finished == WHITEHEAT_CMD_COMPLETE) {
		dbg("%s - command completed.", __FUNCTION__);
		switch (command) {
			case WHITEHEAT_GET_DTR_RTS:
				info = usb_get_serial_port_data(port);
				memcpy(&info->mcr, command_info->result_buffer, sizeof(struct whiteheat_dr_info));
				break;
		}
	}

exit:
	spin_unlock_irqrestore(&command_info->lock, flags);
	return retval;
}


static int firm_open(struct usb_serial_port *port) {
	struct whiteheat_simple open_command;

	open_command.port = port->number - port->serial->minor + 1;
	return firm_send_command(port, WHITEHEAT_OPEN, (__u8 *)&open_command, sizeof(open_command));
}


static int firm_close(struct usb_serial_port *port) {
	struct whiteheat_simple close_command;

	close_command.port = port->number - port->serial->minor + 1;
	return firm_send_command(port, WHITEHEAT_CLOSE, (__u8 *)&close_command, sizeof(close_command));
}


static int firm_setup_port(struct usb_serial_port *port) {
	struct whiteheat_port_settings port_settings;
	unsigned int cflag = port->tty->termios->c_cflag;

	port_settings.port = port->number + 1;

	/* get the byte size */
	switch (cflag & CSIZE) {
		case CS5:	port_settings.bits = 5;   break;
		case CS6:	port_settings.bits = 6;   break;
		case CS7:	port_settings.bits = 7;   break;
		default:
		case CS8:	port_settings.bits = 8;   break;
	}
	dbg("%s - data bits = %d", __FUNCTION__, port_settings.bits);
	
	/* determine the parity */
	if (cflag & PARENB)
		if (cflag & CMSPAR)
			if (cflag & PARODD)
				port_settings.parity = WHITEHEAT_PAR_MARK;
			else
				port_settings.parity = WHITEHEAT_PAR_SPACE;
		else
			if (cflag & PARODD)
				port_settings.parity = WHITEHEAT_PAR_ODD;
			else
				port_settings.parity = WHITEHEAT_PAR_EVEN;
	else
		port_settings.parity = WHITEHEAT_PAR_NONE;
	dbg("%s - parity = %c", __FUNCTION__, port_settings.parity);

	/* figure out the stop bits requested */
	if (cflag & CSTOPB)
		port_settings.stop = 2;
	else
		port_settings.stop = 1;
	dbg("%s - stop bits = %d", __FUNCTION__, port_settings.stop);

	/* figure out the flow control settings */
	if (cflag & CRTSCTS)
		port_settings.hflow = (WHITEHEAT_HFLOW_CTS | WHITEHEAT_HFLOW_RTS);
	else
		port_settings.hflow = WHITEHEAT_HFLOW_NONE;
	dbg("%s - hardware flow control = %s %s %s %s", __FUNCTION__,
	    (port_settings.hflow & WHITEHEAT_HFLOW_CTS) ? "CTS" : "",
	    (port_settings.hflow & WHITEHEAT_HFLOW_RTS) ? "RTS" : "",
	    (port_settings.hflow & WHITEHEAT_HFLOW_DSR) ? "DSR" : "",
	    (port_settings.hflow & WHITEHEAT_HFLOW_DTR) ? "DTR" : "");
	
	/* determine software flow control */
	if (I_IXOFF(port->tty))
		port_settings.sflow = WHITEHEAT_SFLOW_RXTX;
	else
		port_settings.sflow = WHITEHEAT_SFLOW_NONE;
	dbg("%s - software flow control = %c", __FUNCTION__, port_settings.sflow);
	
	port_settings.xon = START_CHAR(port->tty);
	port_settings.xoff = STOP_CHAR(port->tty);
	dbg("%s - XON = %2x, XOFF = %2x", __FUNCTION__, port_settings.xon, port_settings.xoff);

	/* get the baud rate wanted */
	port_settings.baud = tty_get_baud_rate(port->tty);
	dbg("%s - baud rate = %d", __FUNCTION__, port_settings.baud);

	/* handle any settings that aren't specified in the tty structure */
	port_settings.lloop = 0;
	
	/* now send the message to the device */
	return firm_send_command(port, WHITEHEAT_SETUP_PORT, (__u8 *)&port_settings, sizeof(port_settings));
}


static int firm_set_rts(struct usb_serial_port *port, __u8 onoff) {
	struct whiteheat_set_rdb rts_command;

	rts_command.port = port->number - port->serial->minor + 1;
	rts_command.state = onoff;
	return firm_send_command(port, WHITEHEAT_SET_RTS, (__u8 *)&rts_command, sizeof(rts_command));
}


static int firm_set_dtr(struct usb_serial_port *port, __u8 onoff) {
	struct whiteheat_set_rdb dtr_command;

	dtr_command.port = port->number - port->serial->minor + 1;
	dtr_command.state = onoff;
	return firm_send_command(port, WHITEHEAT_SET_RTS, (__u8 *)&dtr_command, sizeof(dtr_command));
}


static int firm_set_break(struct usb_serial_port *port, __u8 onoff) {
	struct whiteheat_set_rdb break_command;

	break_command.port = port->number - port->serial->minor + 1;
	break_command.state = onoff;
	return firm_send_command(port, WHITEHEAT_SET_RTS, (__u8 *)&break_command, sizeof(break_command));
}


static int firm_purge(struct usb_serial_port *port, __u8 rxtx) {
	struct whiteheat_purge purge_command;

	purge_command.port = port->number - port->serial->minor + 1;
	purge_command.what = rxtx;
	return firm_send_command(port, WHITEHEAT_PURGE, (__u8 *)&purge_command, sizeof(purge_command));
}


static int firm_get_dtr_rts(struct usb_serial_port *port) {
	struct whiteheat_simple get_dr_command;

	get_dr_command.port = port->number - port->serial->minor + 1;
	return firm_send_command(port, WHITEHEAT_GET_DTR_RTS, (__u8 *)&get_dr_command, sizeof(get_dr_command));
}


static int firm_report_tx_done(struct usb_serial_port *port) {
	struct whiteheat_simple close_command;

	close_command.port = port->number - port->serial->minor + 1;
	return firm_send_command(port, WHITEHEAT_REPORT_TX_DONE, (__u8 *)&close_command, sizeof(close_command));
}


/*****************************************************************************
 * Connect Tech's White Heat utility functions
 *****************************************************************************/
static int start_command_port(struct usb_serial *serial)
{
	struct usb_serial_port *command_port;
	struct whiteheat_command_private *command_info;
	unsigned long flags;
	int retval = 0;
	
	command_port = &serial->port[COMMAND_PORT];
	command_info = usb_get_serial_port_data(command_port);
	spin_lock_irqsave(&command_info->lock, flags);
	if (!command_info->port_running) {
		/* Work around HCD bugs */
		usb_clear_halt(serial->dev, command_port->read_urb->pipe);

		command_port->read_urb->dev = serial->dev;
		retval = usb_submit_urb(command_port->read_urb, GFP_KERNEL);
		if (retval) {
			err("%s - failed submitting read urb, error %d", __FUNCTION__, retval);
			goto exit;
		}
	}
	command_info->port_running++;

exit:
	spin_unlock_irqrestore(&command_info->lock, flags);
	return retval;
}


static void stop_command_port(struct usb_serial *serial)
{
	struct usb_serial_port *command_port;
	struct whiteheat_command_private *command_info;
	unsigned long flags;

	command_port = &serial->port[COMMAND_PORT];
	command_info = usb_get_serial_port_data(command_port);
	spin_lock_irqsave(&command_info->lock, flags);
	command_info->port_running--;
	if (!command_info->port_running)
		usb_unlink_urb(command_port->read_urb);
	spin_unlock_irqrestore(&command_info->lock, flags);
}


static int start_port_read(struct usb_serial_port *port)
{
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct whiteheat_urb_wrap *wrap;
	struct urb *urb;
	int retval = 0;
	unsigned long flags;
	struct list_head *tmp;
	struct list_head *tmp2;

	spin_lock_irqsave(&info->lock, flags);

	list_for_each_safe(tmp, tmp2, &info->rx_urbs_free) {
		list_del(tmp);
		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		urb = wrap->urb;
		urb->dev = port->serial->dev;
		retval = usb_submit_urb(urb, GFP_KERNEL);
		if (retval) {
			list_add(tmp, &info->rx_urbs_free);
			list_for_each_safe(tmp, tmp2, &info->rx_urbs_submitted) {
				wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
				urb = wrap->urb;
				usb_unlink_urb(urb);
				list_del(tmp);
				list_add(tmp, &info->rx_urbs_free);
			}
			break;
		}
		list_add(tmp, &info->rx_urbs_submitted);
	}

	spin_unlock_irqrestore(&info->lock, flags);

	return retval;
}


static struct whiteheat_urb_wrap *urb_to_wrap(struct urb* urb, struct list_head *head)
{
	struct whiteheat_urb_wrap *wrap;
	struct list_head *tmp;

	list_for_each(tmp, head) {
		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		if (wrap->urb == urb)
			return wrap;
	}

	return NULL;
}


static struct list_head *list_first(struct list_head *head)
{
	return head->next;
}


static void rx_data_softint(void *private)
{
	struct usb_serial_port *port = (struct usb_serial_port *)private;
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct tty_struct *tty = port->tty;
	struct whiteheat_urb_wrap *wrap;
	struct urb *urb;
	unsigned long flags;
	struct list_head *tmp;
	struct list_head *tmp2;
	int result;
	int sent = 0;

	spin_lock_irqsave(&info->lock, flags);
	if (info->flags & THROTTLED) {
		spin_unlock_irqrestore(&info->lock, flags);
		return;
	}

	list_for_each_safe(tmp, tmp2, &info->rx_urb_q) {
		list_del(tmp);
		spin_unlock_irqrestore(&info->lock, flags);

		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		urb = wrap->urb;

		if (tty && urb->actual_length) {
			if (urb->actual_length > TTY_FLIPBUF_SIZE - tty->flip.count) {
				spin_lock_irqsave(&info->lock, flags);
				list_add(tmp, &info->rx_urb_q);
				spin_unlock_irqrestore(&info->lock, flags);
				tty_flip_buffer_push(tty);
				schedule_work(&info->rx_work);
				return;
			}

			memcpy(tty->flip.char_buf_ptr, urb->transfer_buffer, urb->actual_length);
			tty->flip.char_buf_ptr += urb->actual_length;
			tty->flip.count += urb->actual_length;
			sent += urb->actual_length;
		}

		urb->dev = port->serial->dev;
		result = usb_submit_urb(urb, GFP_ATOMIC);
		if (result) {
			err("%s - failed resubmitting read urb, error %d", __FUNCTION__, result);
			spin_lock_irqsave(&info->lock, flags);
			list_add(tmp, &info->rx_urbs_free);
			continue;
		}

		spin_lock_irqsave(&info->lock, flags);
		list_add(tmp, &info->rx_urbs_submitted);
	}
	spin_unlock_irqrestore(&info->lock, flags);

	if (sent)
		tty_flip_buffer_push(tty);
}


/*****************************************************************************
 * Connect Tech's White Heat module functions
 *****************************************************************************/
static int __init whiteheat_init (void)
{
	usb_serial_register (&whiteheat_fake_device);
	usb_serial_register (&whiteheat_device);
	usb_register (&whiteheat_driver);
	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
}


static void __exit whiteheat_exit (void)
{
	usb_deregister (&whiteheat_driver);
	usb_serial_deregister (&whiteheat_fake_device);
	usb_serial_deregister (&whiteheat_device);
}


module_init(whiteheat_init);
module_exit(whiteheat_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

MODULE_PARM(urb_pool_size, "i");
MODULE_PARM_DESC(urb_pool_size, "Number of urbs to use for buffering");

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug enabled or not");
