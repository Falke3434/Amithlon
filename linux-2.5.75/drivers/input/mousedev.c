/*
 * Input driver to ExplorerPS/2 device driver module.
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#define MOUSEDEV_MINOR_BASE 	32
#define MOUSEDEV_MINORS		32
#define MOUSEDEV_MIX		31

#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/config.h>
#include <linux/smp_lock.h>
#include <linux/random.h>
#include <linux/major.h>
#include <linux/device.h>
#include <linux/devfs_fs_kernel.h>
#ifdef CONFIG_INPUT_MOUSEDEV_PSAUX
#include <linux/miscdevice.h>
#endif

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Mouse (ExplorerPS/2) device interfaces");
MODULE_LICENSE("GPL");

#ifndef CONFIG_INPUT_MOUSEDEV_SCREEN_X
#define CONFIG_INPUT_MOUSEDEV_SCREEN_X	1024
#endif
#ifndef CONFIG_INPUT_MOUSEDEV_SCREEN_Y
#define CONFIG_INPUT_MOUSEDEV_SCREEN_Y	768
#endif

struct mousedev {
	int exist;
	int open;
	int minor;
	int misc;
	char name[16];
	wait_queue_head_t wait;
	struct list_head list;
	struct input_handle handle;
};

struct mousedev_list {
	struct fasync_struct *fasync;
	struct mousedev *mousedev;
	struct list_head node;
	int dx, dy, dz, oldx, oldy;
	signed char ps2[6];
	unsigned long buttons;
	unsigned char ready, buffer, bufsiz;
	unsigned char mode, imexseq, impsseq;
};

#define MOUSEDEV_SEQ_LEN	6

static unsigned char mousedev_imps_seq[] = { 0xf3, 200, 0xf3, 100, 0xf3, 80 };
static unsigned char mousedev_imex_seq[] = { 0xf3, 200, 0xf3, 200, 0xf3, 80 };

static struct input_handler mousedev_handler;

static struct mousedev *mousedev_table[MOUSEDEV_MINORS];
static struct mousedev mousedev_mix;

static int xres = CONFIG_INPUT_MOUSEDEV_SCREEN_X;
static int yres = CONFIG_INPUT_MOUSEDEV_SCREEN_Y;

static void mousedev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	struct mousedev *mousedevs[3] = { handle->private, &mousedev_mix, NULL };
	struct mousedev **mousedev = mousedevs;
	struct mousedev_list *list;
	int index, size, wake;

	while (*mousedev) {

		wake = 0;

		list_for_each_entry(list, &(*mousedev)->list, node)
			switch (type) {
				case EV_ABS:
					if (test_bit(BTN_TRIGGER, handle->dev->keybit))
						break;
					switch (code) {
						case ABS_X:	
							size = handle->dev->absmax[ABS_X] - handle->dev->absmin[ABS_X];
							if (size != 0) {
								list->dx += (value * xres - list->oldx) / size;
								list->oldx += list->dx * size;
							} else {
								list->dx += value - list->oldx;
								list->oldx += list->dx;
							}
							break;

						case ABS_Y:
							size = handle->dev->absmax[ABS_Y] - handle->dev->absmin[ABS_Y];
							if (size != 0) {
								list->dy -= (value * yres - list->oldy) / size;
								list->oldy -= list->dy * size;
							} else {
								list->dy -= value - list->oldy;
								list->oldy -= list->dy;
							}
							break;
					}
					break;

				case EV_REL:
					switch (code) {
						case REL_X:	list->dx += value; break;
						case REL_Y:	list->dy -= value; break;
						case REL_WHEEL:	if (list->mode) list->dz -= value; break;
					}
					break;

				case EV_KEY:
					switch (code) {
						case BTN_0:
						case BTN_TOUCH:
						case BTN_LEFT:   index = 0; break;
						case BTN_4:
						case BTN_EXTRA:  if (list->mode == 2) { index = 4; break; }
						case BTN_STYLUS:
						case BTN_1:
						case BTN_RIGHT:  index = 1; break;
						case BTN_3:
						case BTN_SIDE:   if (list->mode == 2) { index = 3; break; }
						case BTN_2:
						case BTN_STYLUS2:
						case BTN_MIDDLE: index = 2; break;	
						default: return;
					}
					switch (value) {
						case 0: clear_bit(index, &list->buttons); break;
						case 1: set_bit(index, &list->buttons); break;
						case 2: return;
					}
					break;

				case EV_SYN:
					switch (code) {
						case SYN_REPORT:
							list->ready = 1;
							kill_fasync(&list->fasync, SIGIO, POLL_IN);
							wake = 1;
							break;
					}
			}

		if (wake)
			wake_up_interruptible(&((*mousedev)->wait));

		mousedev++;
	}
}

static int mousedev_fasync(int fd, struct file *file, int on)
{
	int retval;
	struct mousedev_list *list = file->private_data;
	retval = fasync_helper(fd, file, on, &list->fasync);
	return retval < 0 ? retval : 0;
}

static void mousedev_free(struct mousedev *mousedev)
{
	devfs_remove("input/mouse%d", mousedev->minor);
	mousedev_table[mousedev->minor] = NULL;
	kfree(mousedev);
}

static int mixdev_release(void)
{
	struct input_handle *handle;

	list_for_each_entry(handle, &mousedev_handler.h_list, h_node) {
		struct mousedev *mousedev = handle->private;

		if (!mousedev->open) {
			if (mousedev->exist)
				input_close_device(&mousedev->handle);
			else
				mousedev_free(mousedev);
		}
	}

	return 0;
}

static int mousedev_release(struct inode * inode, struct file * file)
{
	struct mousedev_list *list = file->private_data;

	mousedev_fasync(-1, file, 0);

	list_del(&list->node);

	if (!--list->mousedev->open) {
		if (list->mousedev->minor == MOUSEDEV_MIX)
			return mixdev_release();

		if (!mousedev_mix.open) {
			if (list->mousedev->exist)
				input_close_device(&list->mousedev->handle);
			else
				mousedev_free(list->mousedev);
		}
	}
	
	kfree(list);
	return 0;
}

static int mousedev_open(struct inode * inode, struct file * file)
{
	struct mousedev_list *list;
	struct input_handle *handle;
	struct mousedev *mousedev;
	int i;

#ifdef CONFIG_INPUT_MOUSEDEV_PSAUX
	if (major(inode->i_rdev) == MISC_MAJOR)
		i = MOUSEDEV_MIX;
	else
#endif
		i = minor(inode->i_rdev) - MOUSEDEV_MINOR_BASE;

	if (i >= MOUSEDEV_MINORS || !mousedev_table[i])
		return -ENODEV;

	if (!(list = kmalloc(sizeof(struct mousedev_list), GFP_KERNEL)))
		return -ENOMEM;
	memset(list, 0, sizeof(struct mousedev_list));

	list->mousedev = mousedev_table[i];
	list_add_tail(&list->node, &mousedev_table[i]->list);
	file->private_data = list;

	if (!list->mousedev->open++) {
		if (list->mousedev->minor == MOUSEDEV_MIX) {
			list_for_each_entry(handle, &mousedev_handler.h_list, h_node) {
				mousedev = handle->private;
				if (!mousedev->open && mousedev->exist)	
					input_open_device(handle);
			}
		} else 
			if (!mousedev_mix.open && list->mousedev->exist)	
				input_open_device(&list->mousedev->handle);
	}

	return 0;
}

static void mousedev_packet(struct mousedev_list *list, unsigned char off)
{
	list->ps2[off] = 0x08 | ((list->dx < 0) << 4) | ((list->dy < 0) << 5) | (list->buttons & 0x07);
	list->ps2[off + 1] = (list->dx > 127 ? 127 : (list->dx < -127 ? -127 : list->dx));
	list->ps2[off + 2] = (list->dy > 127 ? 127 : (list->dy < -127 ? -127 : list->dy));
	list->dx -= list->ps2[off + 1];
	list->dy -= list->ps2[off + 2];
	list->bufsiz = off + 3;

	if (list->mode == 2) {
		list->ps2[off + 3] = (list->dz > 7 ? 7 : (list->dz < -7 ? -7 : list->dz));
		list->dz -= list->ps2[off + 3];
		list->ps2[off + 3] = (list->ps2[off + 3] & 0x0f) | ((list->buttons & 0x18) << 1);
		list->bufsiz++;
	}
	
	if (list->mode == 1) {
		list->ps2[off + 3] = (list->dz > 127 ? 127 : (list->dz < -127 ? -127 : list->dz));
		list->dz -= list->ps2[off + 3];
		list->bufsiz++;
	}

	if (!list->dx && !list->dy && (!list->mode || !list->dz)) list->ready = 0;
	list->buffer = list->bufsiz;
}


static ssize_t mousedev_write(struct file * file, const char * buffer, size_t count, loff_t *ppos)
{
	struct mousedev_list *list = file->private_data;
	unsigned char c;
	int i;

	for (i = 0; i < count; i++) {

		if (get_user(c, buffer + i))
			return -EFAULT;

		if (c == mousedev_imex_seq[list->imexseq]) {
			if (++list->imexseq == MOUSEDEV_SEQ_LEN) {
				list->imexseq = 0;
				list->mode = 2;
			}
		} else list->imexseq = 0;

		if (c == mousedev_imps_seq[list->impsseq]) {
			if (++list->impsseq == MOUSEDEV_SEQ_LEN) {
				list->impsseq = 0;
				list->mode = 1;
			}
		} else list->impsseq = 0;

		list->ps2[0] = 0xfa;
		list->bufsiz = 1;

		switch (c) {

			case 0xeb: /* Poll */
				mousedev_packet(list, 1);
				break;

			case 0xf2: /* Get ID */
				switch (list->mode) {
					case 0: list->ps2[1] = 0; break;
					case 1: list->ps2[1] = 3; break;
					case 2: list->ps2[1] = 4; break;
				}
				list->bufsiz = 2;
				break;

			case 0xe9: /* Get info */
				list->ps2[1] = 0x60; list->ps2[2] = 3; list->ps2[3] = 200;
				list->bufsiz = 4;
				break;

			case 0xff: /* Reset */
				list->impsseq = 0;
				list->imexseq = 0;
				list->mode = 0;
				list->ps2[0] = 0xaa;
				list->ps2[1] = 0x00;
				list->bufsiz = 2;
				break;
		}

		list->buffer = list->bufsiz;
	}

	kill_fasync(&list->fasync, SIGIO, POLL_IN);

	wake_up_interruptible(&list->mousedev->wait);
		
	return count;
}

static ssize_t mousedev_read(struct file * file, char * buffer, size_t count, loff_t *ppos)
{
	struct mousedev_list *list = file->private_data;
	int retval = 0;

	if (!list->ready && !list->buffer && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(list->mousedev->wait, list->ready || list->buffer);

	if (retval)
		return retval;

	if (!list->buffer && list->ready)
		mousedev_packet(list, 0);

	if (count > list->buffer)
		count = list->buffer;

	list->buffer -= count;

	if (copy_to_user(buffer, list->ps2 + list->bufsiz - list->buffer - count, count))
		return -EFAULT;

	return count;	
}

/* No kernel lock - fine */
static unsigned int mousedev_poll(struct file *file, poll_table *wait)
{
	struct mousedev_list *list = file->private_data;
	poll_wait(file, &list->mousedev->wait, wait);
	if (list->ready || list->buffer)
		return POLLIN | POLLRDNORM;
	return 0;
}

struct file_operations mousedev_fops = {
	.owner =	THIS_MODULE,
	.read =		mousedev_read,
	.write =	mousedev_write,
	.poll =		mousedev_poll,
	.open =		mousedev_open,
	.release =	mousedev_release,
	.fasync =	mousedev_fasync,
};

static struct input_handle *mousedev_connect(struct input_handler *handler, struct input_dev *dev, struct input_device_id *id)
{
	struct mousedev *mousedev;
	int minor = 0;

	for (minor = 0; minor < MOUSEDEV_MINORS && mousedev_table[minor]; minor++);
	if (minor == MOUSEDEV_MINORS) {
		printk(KERN_ERR "mousedev: no more free mousedev devices\n");
		return NULL;
	}

	if (!(mousedev = kmalloc(sizeof(struct mousedev), GFP_KERNEL)))
		return NULL;
	memset(mousedev, 0, sizeof(struct mousedev));

	INIT_LIST_HEAD(&mousedev->list);
	init_waitqueue_head(&mousedev->wait);

	mousedev->minor = minor;
	mousedev->exist = 1;
	mousedev->handle.dev = dev;
	mousedev->handle.name = mousedev->name;
	mousedev->handle.handler = handler;
	mousedev->handle.private = mousedev;
	sprintf(mousedev->name, "mouse%d", minor);

	if (mousedev_mix.open)
		input_open_device(&mousedev->handle);

	mousedev_table[minor] = mousedev;

	devfs_mk_cdev(MKDEV(INPUT_MAJOR, MOUSEDEV_MINOR_BASE + minor),
			S_IFCHR|S_IRUGO|S_IWUSR, "input/mouse%d", minor);

	return &mousedev->handle;
}

static void mousedev_disconnect(struct input_handle *handle)
{
	struct mousedev *mousedev = handle->private;

	mousedev->exist = 0;

	if (mousedev->open) {
		input_close_device(handle);
	} else {
		if (mousedev_mix.open)
			input_close_device(handle);
		mousedev_free(mousedev);
	}
}

static struct input_device_id mousedev_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT | INPUT_DEVICE_ID_MATCH_RELBIT,
		.evbit = { BIT(EV_KEY) | BIT(EV_REL) },
		.keybit = { [LONG(BTN_LEFT)] = BIT(BTN_LEFT) },
		.relbit = { BIT(REL_X) | BIT(REL_Y) },
	},	/* A mouse like device, at least one button, two relative axes */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_RELBIT,
		.evbit = { BIT(EV_KEY) | BIT(EV_REL) },
		.relbit = { BIT(REL_WHEEL) },
	},	/* A separate scrollwheel */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT(EV_KEY) | BIT(EV_ABS) },
		.keybit = { [LONG(BTN_TOUCH)] = BIT(BTN_TOUCH) },
		.absbit = { BIT(ABS_X) | BIT(ABS_Y) },
	},	/* A tablet like device, at least touch detection, two absolute axes */

	{ }, 	/* Terminating entry */
};

MODULE_DEVICE_TABLE(input, mousedev_ids);
	
static struct input_handler mousedev_handler = {
	.event =	mousedev_event,
	.connect =	mousedev_connect,
	.disconnect =	mousedev_disconnect,
	.fops =		&mousedev_fops,
	.minor =	MOUSEDEV_MINOR_BASE,
	.name =		"mousedev",
	.id_table =	mousedev_ids,
};

#ifdef CONFIG_INPUT_MOUSEDEV_PSAUX
static struct miscdevice psaux_mouse = {
	PSMOUSE_MINOR, "psaux", &mousedev_fops
};
#endif

static int __init mousedev_init(void)
{
	input_register_handler(&mousedev_handler);

	memset(&mousedev_mix, 0, sizeof(struct mousedev));
	INIT_LIST_HEAD(&mousedev_mix.list);
	init_waitqueue_head(&mousedev_mix.wait);
	mousedev_table[MOUSEDEV_MIX] = &mousedev_mix;
	mousedev_mix.exist = 1;
	mousedev_mix.minor = MOUSEDEV_MIX;

	devfs_mk_cdev(MKDEV(INPUT_MAJOR, MOUSEDEV_MINOR_BASE + MOUSEDEV_MIX),
			S_IFCHR|S_IRUGO|S_IWUSR, "input/mice");


#ifdef CONFIG_INPUT_MOUSEDEV_PSAUX
	if (!(mousedev_mix.misc = !misc_register(&psaux_mouse)))
		printk(KERN_WARNING "mice: could not misc_register the device\n");
#endif

	printk(KERN_INFO "mice: PS/2 mouse device common for all mice\n");

	return 0;
}

static void __exit mousedev_exit(void)
{
#ifdef CONFIG_INPUT_MOUSEDEV_PSAUX
	if (mousedev_mix.misc)
		misc_deregister(&psaux_mouse);
#endif
	devfs_remove("input/mice");
	input_unregister_handler(&mousedev_handler);
}

module_init(mousedev_init);
module_exit(mousedev_exit);

MODULE_PARM(xres, "i");
MODULE_PARM_DESC(xres, "Horizontal screen resolution");
MODULE_PARM(yres, "i");
MODULE_PARM_DESC(yres, "Vertical screen resolution");
