/* 
 * Sony Programmable I/O Control Device driver for VAIO
 *
 * Copyright (C) 2001 Stelian Pop <stelian.pop@fr.alcove.com>, Alc?ve
 *
 * Copyright (C) 2001 Michael Ashley <m.ashley@unsw.edu.au>
 *
 * Copyright (C) 2001 Junichi Morita <jun1m@mars.dti.ne.jp>
 *
 * Copyright (C) 2000 Takaya Kinjo <t-kinjo@tc4.so-net.ne.jp>
 *
 * Copyright (C) 2000 Andrew Tridgell <tridge@valinux.com>
 *
 * Earlier work by Werner Almesberger, Paul `Rusty' Russell and Paul Mackerras.
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "sonypi.h"
#include <linux/sonypi.h>

static struct sonypi_device sonypi_device;
static int minor = -1;
static int verbose; /* = 0 */
static int fnkeyinit; /* = 0 */
static int camera; /* = 0 */
extern int is_sony_vaio_laptop; /* set in DMI table parse routines */

/* Inits the queue */
static inline void sonypi_initq(void) {
        sonypi_device.queue.head = sonypi_device.queue.tail = 0;
	sonypi_device.queue.len = 0;
	sonypi_device.queue.s_lock = (spinlock_t)SPIN_LOCK_UNLOCKED;
	init_waitqueue_head(&sonypi_device.queue.proc_list);
}

/* Pulls an event from the queue */
static inline unsigned char sonypi_pullq(void) {
        unsigned char result;
	unsigned long flags;

	spin_lock_irqsave(&sonypi_device.queue.s_lock, flags);
	if (!sonypi_device.queue.len) {
		spin_unlock_irqrestore(&sonypi_device.queue.s_lock, flags);
		return 0;
	}
	result = sonypi_device.queue.buf[sonypi_device.queue.head];
        sonypi_device.queue.head++;
	sonypi_device.queue.head &= (SONYPI_BUF_SIZE - 1);
	sonypi_device.queue.len--;
	spin_unlock_irqrestore(&sonypi_device.queue.s_lock, flags);
        return result;
}

/* Pushes an event into the queue */
static inline void sonypi_pushq(unsigned char event) {
	unsigned long flags;

	spin_lock_irqsave(&sonypi_device.queue.s_lock, flags);
	if (sonypi_device.queue.len == SONYPI_BUF_SIZE) {
		/* remove the first element */
        	sonypi_device.queue.head++;
		sonypi_device.queue.head &= (SONYPI_BUF_SIZE - 1);
		sonypi_device.queue.len--;
	}
	sonypi_device.queue.buf[sonypi_device.queue.tail] = event;
	sonypi_device.queue.tail++;
	sonypi_device.queue.tail &= (SONYPI_BUF_SIZE - 1);
	sonypi_device.queue.len++;

	kill_fasync(&sonypi_device.queue.fasync, SIGIO, POLL_IN);
	wake_up_interruptible(&sonypi_device.queue.proc_list);
	spin_unlock_irqrestore(&sonypi_device.queue.s_lock, flags);
}

/* Tests if the queue is empty */
static inline int sonypi_emptyq(void) {
        int result;
	unsigned long flags;

	spin_lock_irqsave(&sonypi_device.queue.s_lock, flags);
        result = (sonypi_device.queue.len == 0);
	spin_unlock_irqrestore(&sonypi_device.queue.s_lock, flags);
        return result;
}

static void sonypi_ecrset(u16 addr, u16 value) {

	wait_on_command(inw_p(SONYPI_CST_IOPORT) & 3);
	outw_p(0x81, SONYPI_CST_IOPORT);
	wait_on_command(inw_p(SONYPI_CST_IOPORT) & 2);
	outw_p(addr, SONYPI_DATA_IOPORT);
	wait_on_command(inw_p(SONYPI_CST_IOPORT) & 2);
	outw_p(value, SONYPI_DATA_IOPORT);
	wait_on_command(inw_p(SONYPI_CST_IOPORT) & 2);
}

static u16 sonypi_ecrget(u16 addr) {

	wait_on_command(inw_p(SONYPI_CST_IOPORT) & 3);
	outw_p(0x80, SONYPI_CST_IOPORT);
	wait_on_command(inw_p(SONYPI_CST_IOPORT) & 2);
	outw_p(addr, SONYPI_DATA_IOPORT);
	wait_on_command(inw_p(SONYPI_CST_IOPORT) & 2);
	return inw_p(SONYPI_DATA_IOPORT);
}

/* Initializes the device - this comes from the AML code in the ACPI bios */
static void __devinit sonypi_normal_srs(void) {
	u32 v;

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = (v & 0xFFFF0000) | ((u32)sonypi_device.ioport1);
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = (v & 0xFFF0FFFF) | 
	    (((u32)sonypi_device.ioport1 ^ sonypi_device.ioport2) << 16);
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);

	v = inl(SONYPI_IRQ_PORT);
	v &= ~(((u32)0x3) << SONYPI_IRQ_SHIFT);
	v |= (((u32)sonypi_device.bits) << SONYPI_IRQ_SHIFT);
	outl(v, SONYPI_IRQ_PORT);

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = (v & 0xFF1FFFFF) | 0x00C00000;
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);
}

static void __devinit sonypi_r505_srs(void) {
	sonypi_ecrset(SONYPI_SHIB, (sonypi_device.ioport1 & 0xFF00) >> 8);
	sonypi_ecrset(SONYPI_SLOB,  sonypi_device.ioport1 & 0x00FF);
	sonypi_ecrset(SONYPI_SIRQ,  sonypi_device.bits);
	udelay(10);
}

/* Disables the device - this comes from the AML code in the ACPI bios */
static void __devexit sonypi_normal_dis(void) {
	u32 v;

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = v & 0xFF3FFFFF;
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);

	v = inl(SONYPI_IRQ_PORT);
	v |= (0x3 << SONYPI_IRQ_SHIFT);
	outl(v, SONYPI_IRQ_PORT);
}

static void __devexit sonypi_r505_dis(void) {
	sonypi_ecrset(SONYPI_SHIB, 0);
	sonypi_ecrset(SONYPI_SLOB, 0);
	sonypi_ecrset(SONYPI_SIRQ, 0);
}

static u8 sonypi_call1(u8 dev) {
	u8 v1, v2;

	wait_on_command(inb_p(sonypi_device.ioport2) & 2);
	outb(dev, sonypi_device.ioport2);
	v1 = inb_p(sonypi_device.ioport2);
	v2 = inb_p(sonypi_device.ioport1);
	return v2;
}

static u8 sonypi_call2(u8 dev, u8 fn) {
	u8 v1;

	wait_on_command(inb_p(sonypi_device.ioport2) & 2);
	outb(dev, sonypi_device.ioport2);
	wait_on_command(inb_p(sonypi_device.ioport2) & 2);
	outb(fn, sonypi_device.ioport1);
	v1 = inb_p(sonypi_device.ioport1);
	return v1;
}

static u8 sonypi_call3(u8 dev, u8 fn, u8 v) {
	u8 v1;

	wait_on_command(inb_p(sonypi_device.ioport2) & 2);
	outb(dev, sonypi_device.ioport2);
	wait_on_command(inb_p(sonypi_device.ioport2) & 2);
	outb(fn, sonypi_device.ioport1);
	wait_on_command(inb_p(sonypi_device.ioport2) & 2);
	outb(v, sonypi_device.ioport1);
	v1 = inb_p(sonypi_device.ioport1);
	return v1;
}

static u8 sonypi_read(u8 fn) {
	u8 v1, v2;
	int n = 100;

	while (n--) {
		v1 = sonypi_call2(0x8f, fn);
		v2 = sonypi_call2(0x8f, fn);
		if (v1 == v2 && v1 != 0xff)
			return v1;
	}
	return 0xff;
}

/* Set brightness, hue etc */
static void sonypi_set(u8 fn, u8 v) {
	
	wait_on_command(sonypi_call3(0x90, fn, v));
}

/* Tests if the camera is ready */
static int sonypi_camera_ready(void) {
	u8 v;

	v = sonypi_call2(0x8f, SONYPI_CAMERA_STATUS);
	return (v != 0xff && (v & SONYPI_CAMERA_STATUS_READY));
}

/* Turns the camera off */
static void sonypi_camera_off(void) {

	sonypi_set(SONYPI_CAMERA_PICTURE, SONYPI_CAMERA_MUTE_MASK);

	if (!sonypi_device.camera_power)
		return;

	sonypi_call2(0x91, 0); 
	sonypi_device.camera_power = 0;
}

/* Turns the camera on */
static void sonypi_camera_on(void) {
	int i, j;

	if (sonypi_device.camera_power)
		return;

	for (j = 5; j > 0; j--) {

		while (sonypi_call2(0x91, 0x1) != 0) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		}
		sonypi_call1(0x93);

		for (i = 400; i > 0; i--) {
			if (sonypi_camera_ready())
				break;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		}
		if (i != 0)
			break;
	}
	
	if (j == 0) {
		printk(KERN_WARNING "sonypi: failed to power on camera\n");
		return;
	}

	sonypi_set(0x10, 0x5a);
	sonypi_device.camera_power = 1;
}

/* Interrupt handler: some event is available */
void sonypi_irq(int irq, void *dev_id, struct pt_regs *regs) {
	u8 v1, v2, event = 0;
	int i;
	u8 sonypi_jogger_ev, sonypi_fnkey_ev;

	if (sonypi_device.model == SONYPI_DEVICE_MODEL_R505) {
		sonypi_jogger_ev = SONYPI_R505_JOGGER_EV;
		sonypi_fnkey_ev = SONYPI_R505_FNKEY_EV;
	}
	else {
		sonypi_jogger_ev = SONYPI_NORMAL_JOGGER_EV;
		sonypi_fnkey_ev = SONYPI_NORMAL_FNKEY_EV;
	}

	v1 = inb_p(sonypi_device.ioport1);
	v2 = inb_p(sonypi_device.ioport2);

	if ((v2 & SONYPI_NORMAL_PKEY_EV) == SONYPI_NORMAL_PKEY_EV) {
		for (i = 0; sonypi_pkeyev[i].event; i++)
			if (sonypi_pkeyev[i].data == v1) {
				event = sonypi_pkeyev[i].event;
				goto found;
			}
	}
	if ((v2 & sonypi_jogger_ev) == sonypi_jogger_ev) {
		for (i = 0; sonypi_joggerev[i].event; i++)
			if (sonypi_joggerev[i].data == v1) {
				event = sonypi_joggerev[i].event;
				goto found;
			}
	}
	if ((v2 & SONYPI_CAPTURE_EV) == SONYPI_CAPTURE_EV) {
		for (i = 0; sonypi_captureev[i].event; i++)
			if (sonypi_captureev[i].data == v1) {
				event = sonypi_captureev[i].event;
				goto found;
			}
	}
	if ((v2 & sonypi_fnkey_ev) == sonypi_fnkey_ev) {
		for (i = 0; sonypi_fnkeyev[i].event; i++)
			if (sonypi_fnkeyev[i].data == v1) {
				event = sonypi_fnkeyev[i].event;
				goto found;
			}
	}
	if ((v2 & SONYPI_BLUETOOTH_EV) == SONYPI_BLUETOOTH_EV) {
		for (i = 0; sonypi_blueev[i].event; i++)
			if (sonypi_blueev[i].data == v1) {
				event = sonypi_blueev[i].event;
				goto found;
			}
	}
	if (verbose)
		printk(KERN_WARNING 
		       "sonypi: unknown event port1=0x%x,port2=0x%x\n",v1,v2);
	return;

found:
	sonypi_pushq(event);
}

/* External camera command (exported to the motion eye v4l driver) */
u8 sonypi_camera_command(int command, u8 value) {
	u8 ret = 0;

	if (!camera)
		return 0;

	down(&sonypi_device.lock);

	switch(command) {
		case SONYPI_COMMAND_GETCAMERA:
			ret = sonypi_camera_ready();
			break;
		case SONYPI_COMMAND_SETCAMERA:
			if (value)
				sonypi_camera_on();
			else
				sonypi_camera_off();
			break;
		case SONYPI_COMMAND_GETCAMERABRIGHTNESS:
			ret = sonypi_read(SONYPI_CAMERA_BRIGHTNESS);
			break;
		case SONYPI_COMMAND_SETCAMERABRIGHTNESS:
			sonypi_set(SONYPI_CAMERA_BRIGHTNESS, value);
			break;
		case SONYPI_COMMAND_GETCAMERACONTRAST:
			ret = sonypi_read(SONYPI_CAMERA_CONTRAST);
			break;
		case SONYPI_COMMAND_SETCAMERACONTRAST:
			sonypi_set(SONYPI_CAMERA_CONTRAST, value);
			break;
		case SONYPI_COMMAND_GETCAMERAHUE:
			ret = sonypi_read(SONYPI_CAMERA_HUE);
			break;
		case SONYPI_COMMAND_SETCAMERAHUE:
			sonypi_set(SONYPI_CAMERA_HUE, value);
			break;
		case SONYPI_COMMAND_GETCAMERACOLOR:
			ret = sonypi_read(SONYPI_CAMERA_COLOR);
			break;
		case SONYPI_COMMAND_SETCAMERACOLOR:
			sonypi_set(SONYPI_CAMERA_COLOR, value);
			break;
		case SONYPI_COMMAND_GETCAMERASHARPNESS:
			ret = sonypi_read(SONYPI_CAMERA_SHARPNESS);
			break;
		case SONYPI_COMMAND_SETCAMERASHARPNESS:
			sonypi_set(SONYPI_CAMERA_SHARPNESS, value);
			break;
		case SONYPI_COMMAND_GETCAMERAPICTURE:
			ret = sonypi_read(SONYPI_CAMERA_PICTURE);
			break;
		case SONYPI_COMMAND_SETCAMERAPICTURE:
			sonypi_set(SONYPI_CAMERA_PICTURE, value);
			break;
		case SONYPI_COMMAND_GETCAMERAAGC:
			ret = sonypi_read(SONYPI_CAMERA_AGC);
			break;
		case SONYPI_COMMAND_SETCAMERAAGC:
			sonypi_set(SONYPI_CAMERA_AGC, value);
			break;
		case SONYPI_COMMAND_GETCAMERADIRECTION:
			ret = sonypi_read(SONYPI_CAMERA_STATUS);
			ret &= SONYPI_DIRECTION_BACKWARDS;
			break;
		case SONYPI_COMMAND_GETCAMERAROMVERSION:
			ret = sonypi_read(SONYPI_CAMERA_ROMVERSION);
			break;
		case SONYPI_COMMAND_GETCAMERAREVISION:
			ret = sonypi_read(SONYPI_CAMERA_REVISION);
			break;
	}
	up(&sonypi_device.lock);
	return ret;
}

static int sonypi_misc_fasync(int fd, struct file *filp, int on) {
	int retval;

	retval = fasync_helper(fd, filp, on, &sonypi_device.queue.fasync);
	if (retval < 0)
		return retval;
	return 0;
}

static int sonypi_misc_release(struct inode * inode, struct file * file) {
	sonypi_misc_fasync(-1, file, 0);
	down(&sonypi_device.lock);
	sonypi_device.open_count--;
	up(&sonypi_device.lock);
	return 0;
}

static int sonypi_misc_open(struct inode * inode, struct file * file) {
	down(&sonypi_device.lock);
	if (sonypi_device.open_count)
		goto out;
	sonypi_device.open_count++;
	/* Flush input queue */
	sonypi_initq();
out:
	up(&sonypi_device.lock);
	return 0;
}

static ssize_t sonypi_misc_read(struct file * file, char * buf, 
		                size_t count, loff_t *pos) {
	DECLARE_WAITQUEUE(wait, current);
	ssize_t i = count;
	unsigned char c;

	if (sonypi_emptyq()) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		add_wait_queue(&sonypi_device.queue.proc_list, &wait);
repeat:
		set_current_state(TASK_INTERRUPTIBLE);
		if (sonypi_emptyq() && !signal_pending(current)) {
			schedule();
			goto repeat;
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(&sonypi_device.queue.proc_list, &wait);
	}
	while (i > 0 && !sonypi_emptyq()) {
		c = sonypi_pullq();
		put_user(c, buf++);
		i--;
        }
	if (count - i) {
		file->f_dentry->d_inode->i_atime = CURRENT_TIME;
		return count-i;
	}
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

static unsigned int sonypi_misc_poll(struct file *file, poll_table * wait) {
	poll_wait(file, &sonypi_device.queue.proc_list, wait);
	if (!sonypi_emptyq())
		return POLLIN | POLLRDNORM;
	return 0;
}

static int sonypi_misc_ioctl(struct inode *ip, struct file *fp, 
			     unsigned int cmd, unsigned long arg) {
	int ret = 0;
	u8 val;

	down(&sonypi_device.lock);
	switch (cmd) {
		case SONYPI_IOCGBRT:
			val = sonypi_ecrget(0x96) & 0xff;
			if (copy_to_user((u8 *)arg, &val, sizeof(val))) {
				ret = -EFAULT;
				goto out;
			}
			break;
		case SONYPI_IOCSBRT:
			if (copy_from_user(&val, (u8 *)arg, sizeof(val))) {
				ret = -EFAULT;
				goto out;
			}
			sonypi_ecrset(0x96, val);
			break;
	default:
		ret = -EINVAL;
	}
out:
	up(&sonypi_device.lock);
	return ret;
}

static struct file_operations sonypi_misc_fops = {
	owner:		THIS_MODULE,
	read:		sonypi_misc_read,
	poll:		sonypi_misc_poll,
	open:		sonypi_misc_open,
	release:	sonypi_misc_release,
	fasync: 	sonypi_misc_fasync,
	ioctl:		sonypi_misc_ioctl,
};

struct miscdevice sonypi_misc_device = {
	-1, "sonypi", &sonypi_misc_fops
};

static int __devinit sonypi_probe(struct pci_dev *pcidev, 
		                  const struct pci_device_id *ent) {
	int i, ret;
	struct sonypi_ioport_list *ioport_list;
	struct sonypi_irq_list *irq_list;

	if (sonypi_device.dev) {
		printk(KERN_ERR "sonypi: only one device allowed!\n"),
		ret = -EBUSY;
		goto out1;
	}
	sonypi_device.dev = pcidev;
	sonypi_device.model = (int)ent->driver_data;
	sonypi_initq();
	init_MUTEX(&sonypi_device.lock);
	
	if (pci_enable_device(pcidev)) {
		printk(KERN_ERR "sonypi: pci_enable_device failed\n");
		ret = -EIO;
		goto out1;
	}

	sonypi_misc_device.minor = (minor == -1) ? 
		MISC_DYNAMIC_MINOR : minor;
	if ((ret = misc_register(&sonypi_misc_device))) {
		printk(KERN_ERR "sonypi: misc_register failed\n");
		goto out1;
	}

	if (sonypi_device.model == SONYPI_DEVICE_MODEL_R505) {
		ioport_list = sonypi_r505_ioport_list;
		sonypi_device.region_size = SONYPI_R505_REGION_SIZE;
		irq_list = sonypi_r505_irq_list;
	}
	else {
		ioport_list = sonypi_normal_ioport_list;
		sonypi_device.region_size = SONYPI_NORMAL_REGION_SIZE;
		irq_list = sonypi_normal_irq_list;
	}

	for (i = 0; ioport_list[i].port1; i++) {
		if (request_region(ioport_list[i].port1, 
				   sonypi_device.region_size, 
				   "Sony Programable I/O Device")) {
			/* get the ioport */
			sonypi_device.ioport1 = ioport_list[i].port1;
			sonypi_device.ioport2 = ioport_list[i].port2;
			break;
		}
	}
	if (!sonypi_device.ioport1) {
		printk(KERN_ERR "sonypi: request_region failed\n");
		ret = -ENODEV;
		goto out2;
	}

	for (i = 0; irq_list[i].irq; i++) {
		if (!request_irq(irq_list[i].irq, sonypi_irq, 
				 SA_SHIRQ, "sonypi", sonypi_irq)) {
			sonypi_device.irq = irq_list[i].irq;
			sonypi_device.bits = irq_list[i].bits;
			break;
		}
	}
	if (!sonypi_device.irq ) {
		printk(KERN_ERR "sonypi: request_irq failed\n");
		ret = -ENODEV;
		goto out3;
	}

	if (fnkeyinit)
		outb(0xf0, 0xb2);

	if (sonypi_device.model == SONYPI_DEVICE_MODEL_R505)
		sonypi_r505_srs();
	else
		sonypi_normal_srs();

	sonypi_call1(0x82);
	sonypi_call2(0x81, 0xff);
	sonypi_call1(0x92); 

	printk(KERN_INFO "sonypi: Sony Programmable I/O Controller Driver v%d.%d.\n",
	       SONYPI_DRIVER_MAJORVERSION,
	       SONYPI_DRIVER_MINORVERSION);
	printk(KERN_INFO "sonypi: detected %s model, camera = %s\n",
	       (sonypi_device.model == SONYPI_DEVICE_MODEL_NORMAL) ?
	       "normal" : "R505",
	       camera ? "on" : "off");
	printk(KERN_INFO "sonypi: enabled at irq=%d, port1=0x%x, port2=0x%x\n",
	       sonypi_device.irq, 
	       sonypi_device.ioport1, sonypi_device.ioport2);
	if (minor == -1)
		printk(KERN_INFO "sonypi: device allocated minor is %d\n",
		       sonypi_misc_device.minor);

	return 0;

out3:
	release_region(sonypi_device.ioport1, sonypi_device.region_size);
out2:
	misc_deregister(&sonypi_misc_device);
out1:
	return ret;
}

static void __devexit sonypi_remove(struct pci_dev *pcidev) {
	sonypi_call2(0x81, 0); /* make sure we don't get any more events */
	if (camera)
		sonypi_camera_off();
	if (sonypi_device.model == SONYPI_DEVICE_MODEL_R505)
		sonypi_r505_dis();
	else
		sonypi_normal_dis();
	free_irq(sonypi_device.irq, sonypi_irq);
	release_region(sonypi_device.ioport1, sonypi_device.region_size);
	misc_deregister(&sonypi_misc_device);
	printk(KERN_INFO "sonypi: removed.\n");
}

static struct pci_device_id sonypi_id_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_3, 
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
	  (unsigned long) SONYPI_DEVICE_MODEL_NORMAL },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_10,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
	  (unsigned long) SONYPI_DEVICE_MODEL_R505 },
	{ }
};

MODULE_DEVICE_TABLE(pci, sonypi_id_tbl);

static struct pci_driver sonypi_driver = {
	name:		"sonypi",
	id_table:	sonypi_id_tbl,
	probe:		sonypi_probe,
	remove:		sonypi_remove,
};

static int __init sonypi_init_module(void) {
	if (is_sony_vaio_laptop)
		return pci_module_init(&sonypi_driver);
	else
		return -ENODEV;
}

static void __exit sonypi_cleanup_module(void) {
	pci_unregister_driver(&sonypi_driver);
}

/* Module entry points */
module_init(sonypi_init_module);
module_exit(sonypi_cleanup_module);

MODULE_AUTHOR("Stelian Pop <stelian.pop@fr.alcove.com>");
MODULE_DESCRIPTION("Sony Programmable I/O Control Device driver");
MODULE_LICENSE("GPL");


MODULE_PARM(minor,"i");
MODULE_PARM_DESC(minor, "minor number of the misc device, default is -1 (automatic)");
MODULE_PARM(verbose,"i");
MODULE_PARM_DESC(verbose, "be verbose, default is 0 (no)");
MODULE_PARM(fnkeyinit,"i");
MODULE_PARM_DESC(fnkeyinit, "set this if your Fn keys do not generate any event");
MODULE_PARM(camera,"i");
MODULE_PARM_DESC(camera, "set this if you have a MotionEye camera (PictureBook series)");

EXPORT_SYMBOL(sonypi_camera_command);
