/*********************************************************************
 *                
 * Filename:      irtty-sir.c
 * Version:       2.0
 * Description:   IrDA line discipline implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Dec  9 21:18:38 1997
 * Modified at:   Sun Oct 27 22:13:30 2002
 * Modified by:   Martin Diehl <mad@mdiehl.de>
 * Sources:       slip.c by Laurence Culhane,   <loz@holmes.demon.co.uk>
 *                          Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 * 
 *     Copyright (c) 1998-2000 Dag Brattli,
 *     Copyright (c) 2002 Martin Diehl,
 *     All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Troms? admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/    

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/smp_lock.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

#include "sir-dev.h"
#include "irtty-sir.h"

MODULE_PARM(qos_mtt_bits, "i");
MODULE_PARM_DESC(qos_mtt_bits, "Minimum Turn Time");

static int qos_mtt_bits = 0x03;      /* 5 ms or more */

/* ------------------------------------------------------- */

/* device configuration callbacks always invoked with irda-thread context */

/* find out, how many chars we have in buffers below us
 * this is allowed to lie, i.e. return less chars than we
 * actually have. The returned value is used to determine
 * how long the irdathread should wait before doing the
 * real blocking wait_until_sent()
 */

static int irtty_chars_in_buffer(struct sir_dev *dev)
{
	struct sirtty_cb *priv = dev->priv;

	ASSERT(priv != NULL, return -1;);
	ASSERT(priv->magic == IRTTY_MAGIC, return -1;);

	return priv->tty->driver->chars_in_buffer(priv->tty);
}

/* Wait (sleep) until underlaying hardware finished transmission
 * i.e. hardware buffers are drained
 * this must block and not return before all characters are really sent
 *
 * If the tty sits on top of a 16550A-like uart, there are typically
 * up to 16 bytes in the fifo - f.e. 9600 bps 8N1 needs 16.7 msec
 *
 * With usbserial the uart-fifo is basically replaced by the converter's
 * outgoing endpoint buffer, which can usually hold 64 bytes (at least).
 * With pl2303 it appears we are safe with 60msec here.
 *
 * I really wish all serial drivers would provide
 * correct implementation of wait_until_sent()
 */

#define USBSERIAL_TX_DONE_DELAY	60

static void irtty_wait_until_sent(struct sir_dev *dev)
{
	struct sirtty_cb *priv = dev->priv;
	struct tty_struct *tty;

	ASSERT(priv != NULL, return;);
	ASSERT(priv->magic == IRTTY_MAGIC, return;);

	tty = priv->tty;
	if (tty->driver->wait_until_sent) {
		lock_kernel();
		tty->driver->wait_until_sent(tty, MSECS_TO_JIFFIES(100));
		unlock_kernel();
	}
	else {
		set_task_state(current, TASK_UNINTERRUPTIBLE);
		schedule_timeout(MSECS_TO_JIFFIES(USBSERIAL_TX_DONE_DELAY));
	}
}

/* 
 *  Function irtty_change_speed (dev, speed)
 *
 *    Change the speed of the serial port.
 *
 * This may sleep in set_termios (usbserial driver f.e.) and must
 * not be called from interrupt/timer/tasklet therefore.
 * All such invocations are deferred to kIrDAd now so we can sleep there.
 */

static int irtty_change_speed(struct sir_dev *dev, unsigned speed)
{
	struct sirtty_cb *priv = dev->priv;
	struct tty_struct *tty;
        struct termios old_termios;
	int cflag;

	ASSERT(priv != NULL, return -1;);
	ASSERT(priv->magic == IRTTY_MAGIC, return -1;);

	tty = priv->tty;

	lock_kernel();
	old_termios = *(tty->termios);
	cflag = tty->termios->c_cflag;

	cflag &= ~CBAUD;

	IRDA_DEBUG(2, "%s(), Setting speed to %d\n", __FUNCTION__, speed);

	switch (speed) {
	case 1200:
		cflag |= B1200;
		break;
	case 2400:
		cflag |= B2400;
		break;
	case 4800:
		cflag |= B4800;
		break;
	case 19200:
		cflag |= B19200;
		break;
	case 38400:
		cflag |= B38400;
		break;
	case 57600:
		cflag |= B57600;
		break;
	case 115200:
		cflag |= B115200;
		break;
	case 9600:
	default:
		cflag |= B9600;
		break;
	}	

	tty->termios->c_cflag = cflag;
	if (tty->driver->set_termios)
		tty->driver->set_termios(tty, &old_termios);
	unlock_kernel();

	priv->io.speed = speed;

	return 0;
}

/*
 * Function irtty_set_dtr_rts (dev, dtr, rts)
 *
 *    This function can be used by dongles etc. to set or reset the status
 *    of the dtr and rts lines
 */

static int irtty_set_dtr_rts(struct sir_dev *dev, int dtr, int rts)
{
	struct sirtty_cb *priv = dev->priv;
	int set = 0;
	int clear = 0;

	ASSERT(priv != NULL, return -1;);
	ASSERT(priv->magic == IRTTY_MAGIC, return -1;);

	if (rts)
		set |= TIOCM_RTS;
	else
		clear |= TIOCM_RTS;
	if (dtr)
		set |= TIOCM_DTR;
	else
		clear |= TIOCM_DTR;

	/*
	 * We can't use ioctl() because it expects a non-null file structure,
	 * and we don't have that here.
	 * This function is not yet defined for all tty driver, so
	 * let's be careful... Jean II
	 */
	ASSERT(priv->tty->driver->tiocmset != NULL, return -1;);
	priv->tty->driver->tiocmset(priv->tty, NULL, set, clear);

	return 0;
}

/* ------------------------------------------------------- */

/* called from sir_dev when there is more data to send
 * context is either netdev->hard_xmit or some transmit-completion bh
 * i.e. we are under spinlock here and must not sleep.
 */

static int irtty_do_write(struct sir_dev *dev, const unsigned char *ptr, size_t len)
{
	struct sirtty_cb *priv = dev->priv;
	struct tty_struct *tty;
	int writelen;

	ASSERT(priv != NULL, return -1;);
	ASSERT(priv->magic == IRTTY_MAGIC, return -1;);

	tty = priv->tty;
	if (!tty->driver->write)
		return 0;
	tty->flags |= (1 << TTY_DO_WRITE_WAKEUP);
	if (tty->driver->write_room) {
		writelen = tty->driver->write_room(tty);
		if (writelen > len)
			writelen = len;
	}
	else
		writelen = len;
	return tty->driver->write(tty, 0, ptr, writelen);
}

/* ------------------------------------------------------- */

/* irda line discipline callbacks */

/* 
 *  Function irtty_receive_buf( tty, cp, count)
 *
 *    Handle the 'receiver data ready' interrupt.  This function is called
 *    by the 'tty_io' module in the kernel when a block of IrDA data has
 *    been received, which can now be decapsulated and delivered for
 *    further processing 
 *
 * calling context depends on underlying driver and tty->low_latency!
 * for example (low_latency: 1 / 0):
 * serial.c:	uart-interrupt / softint
 * usbserial:	urb-complete-interrupt / softint
 */

static void irtty_receive_buf(struct tty_struct *tty, const unsigned char *cp,
			      char *fp, int count) 
{
	struct sir_dev *dev;
	struct sirtty_cb *priv = tty->disc_data;
	int	i;

	ASSERT(priv != NULL, return;);
	ASSERT(priv->magic == IRTTY_MAGIC, return;);

	if (unlikely(count==0))		/* yes, this happens */
		return;

	dev = priv->dev;
	if (!dev) {
		WARNING("%s(), not ready yet!\n", __FUNCTION__);
		return;
	}

	for (i = 0; i < count; i++) {
		/* 
		 *  Characters received with a parity error, etc?
		 */
 		if (fp && *fp++) { 
			IRDA_DEBUG(0, "Framing or parity error!\n");
			sirdev_receive(dev, NULL, 0);	/* notify sir_dev (updating stats) */
			return;
 		}
	}

	sirdev_receive(dev, cp, count);
}

/*
 * Function irtty_receive_room (tty)
 *
 *    Used by the TTY to find out how much data we can receive at a time
 * 
*/
static int irtty_receive_room(struct tty_struct *tty) 
{
	struct sirtty_cb *priv = tty->disc_data;

	ASSERT(priv != NULL, return 0;);
	ASSERT(priv->magic == IRTTY_MAGIC, return 0;);

	return 65536;  /* We can handle an infinite amount of data. :-) */
}

/*
 * Function irtty_write_wakeup (tty)
 *
 *    Called by the driver when there's room for more data.  If we have
 *    more packets to send, we send them here.
 *
 */
static void irtty_write_wakeup(struct tty_struct *tty) 
{
	struct sirtty_cb *priv = tty->disc_data;

	ASSERT(priv != NULL, return;);
	ASSERT(priv->magic == IRTTY_MAGIC, return;);

	tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);

	if (priv->dev)
		sirdev_write_complete(priv->dev);
}

/* ------------------------------------------------------- */

/*
 * Function irtty_stop_receiver (tty, stop)
 *
 */

static inline void irtty_stop_receiver(struct tty_struct *tty, int stop)
{
	struct termios old_termios;
	int cflag;

	lock_kernel();
	old_termios = *(tty->termios);
	cflag = tty->termios->c_cflag;
	
	if (stop)
		cflag &= ~CREAD;
	else
		cflag |= CREAD;

	tty->termios->c_cflag = cflag;
	if (tty->driver->set_termios)
		tty->driver->set_termios(tty, &old_termios);
	unlock_kernel();
}

/*****************************************************************/

DECLARE_MUTEX(irtty_sem);		/* serialize ldisc open/close with sir_dev */

/* notifier from sir_dev when irda% device gets opened (ifup) */

static int irtty_start_dev(struct sir_dev *dev)
{
	struct sirtty_cb *priv;
	struct tty_struct *tty;

	/* serialize with ldisc open/close */
	down(&irtty_sem);

	priv = dev->priv;
	if (unlikely(!priv || priv->magic!=IRTTY_MAGIC)) {
		up(&irtty_sem);
		return -ESTALE;
	}

	tty = priv->tty;

	if (tty->driver->start)
		tty->driver->start(tty);
	/* Make sure we can receive more data */
	irtty_stop_receiver(tty, FALSE);

	up(&irtty_sem);
	return 0;
}

/* notifier from sir_dev when irda% device gets closed (ifdown) */

static int irtty_stop_dev(struct sir_dev *dev)
{
	struct sirtty_cb *priv;
	struct tty_struct *tty;

	/* serialize with ldisc open/close */
	down(&irtty_sem);

	priv = dev->priv;
	if (unlikely(!priv || priv->magic!=IRTTY_MAGIC)) {
		up(&irtty_sem);
		return -ESTALE;
	}

	tty = priv->tty;

	/* Make sure we don't receive more data */
	irtty_stop_receiver(tty, TRUE);
	if (tty->driver->stop)
		tty->driver->stop(tty);

	up(&irtty_sem);

	return 0;
}

/* ------------------------------------------------------- */

struct sir_driver sir_tty_drv = {
	.owner			= THIS_MODULE,
	.driver_name		= "sir_tty",
	.start_dev		= irtty_start_dev,
	.stop_dev		= irtty_stop_dev,
	.do_write		= irtty_do_write,
	.chars_in_buffer	= irtty_chars_in_buffer,
	.wait_until_sent	= irtty_wait_until_sent,
	.set_speed		= irtty_change_speed,
	.set_dtr_rts		= irtty_set_dtr_rts,
};

/* ------------------------------------------------------- */

/*
 * Function irtty_ioctl (tty, file, cmd, arg)
 *
 *     The Swiss army knife of system calls :-)
 *
 */
static int irtty_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct irtty_info { char name[6]; } info;
	struct sir_dev *dev;
	struct sirtty_cb *priv = tty->disc_data;
	int size = _IOC_SIZE(cmd);
	int err = 0;

	ASSERT(priv != NULL, return -ENODEV;);
	ASSERT(priv->magic == IRTTY_MAGIC, return -EBADR;);

	IRDA_DEBUG(3, "%s(cmd=0x%X)\n", __FUNCTION__, cmd);

	dev = priv->dev;
	ASSERT(dev != NULL, return -1;);

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = verify_area(VERIFY_WRITE, (void *) arg, size);
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = verify_area(VERIFY_READ, (void *) arg, size);
	if (err)
		return err;
	
	switch (cmd) {
	case TCGETS:
	case TCGETA:
		err = n_tty_ioctl(tty, file, cmd, arg);
		break;

	case IRTTY_IOCTDONGLE:
		/* this call blocks for completion */
		err = sirdev_set_dongle(dev, (IRDA_DONGLE) arg);
		break;

	case IRTTY_IOCGET:
		ASSERT(dev->netdev != NULL, return -1;);

		memset(&info, 0, sizeof(info)); 
		strncpy(info.name, dev->netdev->name, sizeof(info.name)-1);

		if (copy_to_user((void *)arg, &info, sizeof(info)))
			err = -EFAULT;
		break;
	default:
		err = -ENOIOCTLCMD;
		break;
	}
	return err;
}


/* 
 *  Function irtty_open(tty)
 *
 *    This function is called by the TTY module when the IrDA line
 *    discipline is called for.  Because we are sure the tty line exists,
 *    we only have to link it to a free IrDA channel.  
 */
static int irtty_open(struct tty_struct *tty) 
{
	struct sir_dev *dev;
	struct sirtty_cb *priv;
	int ret = 0;

	/* Module stuff handled via irda_ldisc.owner - Jean II */

	/* First make sure we're not already connected. */
	if (tty->disc_data != NULL) {
		priv = tty->disc_data;
		if (priv && priv->magic == IRTTY_MAGIC) {
			ret = -EEXIST;
			goto out;
		}
		tty->disc_data = NULL;		/* ### */
	}

	/* stop the underlying  driver */
	irtty_stop_receiver(tty, TRUE);
	if (tty->driver->stop)
		tty->driver->stop(tty);

	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer(tty);
	
/* from old irtty - but what is it good for?
 * we _are_ the ldisc and we _don't_ implement flush_buffer!
 *
 *	if (tty->ldisc.flush_buffer)
 *		tty->ldisc.flush_buffer(tty);
 */

	/* apply mtt override */
	sir_tty_drv.qos_mtt_bits = qos_mtt_bits;

	/* get a sir device instance for this driver */
	dev = sirdev_get_instance(&sir_tty_drv, tty->name);
	if (!dev) {
		ret = -ENODEV;
		goto out;
	}

	/* allocate private device info block */
	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		goto out_put;
	memset(priv, 0, sizeof(*priv));

	priv->magic = IRTTY_MAGIC;
	priv->tty = tty;
	priv->dev = dev;

	/* serialize with start_dev - in case we were racing with ifup */
	down(&irtty_sem);

	dev->priv = priv;
	tty->disc_data = priv;

	up(&irtty_sem);

	IRDA_DEBUG(0, "%s - %s: irda line discipline opened\n", __FUNCTION__, tty->name);

	return 0;

out_put:
	sirdev_put_instance(dev);
out:
	return ret;
}

/* 
 *  Function irtty_close (tty)
 *
 *    Close down a IrDA channel. This means flushing out any pending queues,
 *    and then restoring the TTY line discipline to what it was before it got
 *    hooked to IrDA (which usually is TTY again).  
 */
static void irtty_close(struct tty_struct *tty) 
{
	struct sirtty_cb *priv = tty->disc_data;

	ASSERT(priv != NULL, return;);
	ASSERT(priv->magic == IRTTY_MAGIC, return;);

	/* Hm, with a dongle attached the dongle driver wants
	 * to close the dongle - which requires the use of
	 * some tty write and/or termios or ioctl operations.
	 * Are we allowed to call those when already requested
	 * to shutdown the ldisc?
	 * If not, we should somehow mark the dev being staled.
	 * Question remains, how to close the dongle in this case...
	 * For now let's assume we are granted to issue tty driver calls
	 * until we return here from the ldisc close. I'm just wondering
	 * how this behaves with hotpluggable serial hardware like
	 * rs232-pcmcia card or usb-serial...
	 *
	 * priv->tty = NULL?;
	 */

	/* we are dead now */
	tty->disc_data = 0;

	sirdev_put_instance(priv->dev);

	/* Stop tty */
	irtty_stop_receiver(tty, TRUE);
	tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
	if (tty->driver->stop)
		tty->driver->stop(tty);

	kfree(priv);

	IRDA_DEBUG(0, "%s - %s: irda line discipline closed\n", __FUNCTION__, tty->name);
}

/* ------------------------------------------------------- */

static struct tty_ldisc irda_ldisc = {
	.magic		= TTY_LDISC_MAGIC,
 	.name		= "irda",
	.flags		= 0,
	.open		= irtty_open,
	.close		= irtty_close,
	.read		= NULL,
	.write		= NULL,
	.ioctl		= irtty_ioctl,
 	.poll		= NULL,
	.receive_buf	= irtty_receive_buf,
	.receive_room	= irtty_receive_room,
	.write_wakeup	= irtty_write_wakeup,
	.owner		= THIS_MODULE,
};

/* ------------------------------------------------------- */

static int __init irtty_sir_init(void)
{
	int err;

	if ((err = tty_register_ldisc(N_IRDA, &irda_ldisc)) != 0)
		ERROR("IrDA: can't register line discipline (err = %d)\n",
			err);
	return err;
}

static void __exit irtty_sir_cleanup(void) 
{
	int err;

	if ((err = tty_register_ldisc(N_IRDA, NULL))) {
		ERROR("%s(), can't unregister line discipline (err = %d)\n",
		      __FUNCTION__, err);
	}
}

module_init(irtty_sir_init);
module_exit(irtty_sir_cleanup);

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("IrDA TTY device driver");
MODULE_LICENSE("GPL");

