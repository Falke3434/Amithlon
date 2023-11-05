/*
 *	Magic Keyboard interface for Linux	
 *
 *      Copyright (C) 1997 Bernd Meyer
 *      derived from rtc.c, the Real Time Clock interface, which is
 *	Copyright (C) 1996 Paul Gortmaker
 *
 *      This interface allows for low level redirection of keyboards.
 *      It wedges in between the low level (hardware) keyboard handler and
 *      the high level one (which hands the keyboard events on to the
 *      console, X or SVGAlib apps). When the device /dev/mkbd is opened
 *      for reading, scancodes coming from the low level driver are handed
 *      out to the reading process instead of being passed on to the 
 *      high level driver. The user level process can then do whatever
 *      it wants (including sending them over a network to another machine).
 *      If /dev/mkbd is written to, the bytes are fed to the high level
 *      driver as if they had been generated by the low level driver.
 *
 *      /dev/mkbd will block on reads until at least one byte is ready for
 *      reading. The data received from the low level driver is stored in a
 *      cyclic buffer of finite size, so a process that has the device open
 *      should make sure that it clears that buffer regularly.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

#define MKBD_VERSION		"0.01"

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/poll.h>
#include <linux/keyboard.h>
#include <linux/kbd_ll.h>
#include <linux/pc_keyb.h>

/*
 *	We sponge a minor off of the misc major. No need slurping
 *	up another valuable major dev number for this. 
 */

static DECLARE_WAIT_QUEUE_HEAD(mkbd_wait);

static long long mkbd_llseek(struct file *file, loff_t offset, int origin);
static ssize_t mkbd_read(struct file *file,
		      char *buf, size_t count, loff_t *ppos);
static int mkbd_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg);
static unsigned int mkbd_poll(struct file *file, poll_table *wait);

#define BUFFER_SIZE 128

static unsigned char buffer[BUFFER_SIZE];
static unsigned long start=0, end=0;
static unsigned long number_open=0;
static unsigned long bytes_total=0;
static unsigned long bytes_total_in=0;

static unsigned int bytes_in_buffer(void)
{
  return (BUFFER_SIZE+end-start)%BUFFER_SIZE;
}

static unsigned char get_from_buffer(void)
{
  if (bytes_in_buffer()>0) {
    unsigned char answer=buffer[start];
    start=(start+1)%BUFFER_SIZE;
    return answer;
  }
  return 0; /* This shouldn't happen! */
}

static void put_into_buffer(unsigned char x)
{
  int newend=(end+1)%BUFFER_SIZE;

  if (newend!=start) {
    bytes_total++;
    buffer[end]=x;
    end=newend;
  }
}

/*
 *	Now all the various file operations that we export.
 */

static long long mkbd_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static ssize_t mkbd_read(struct file *file,
		      char *buf, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	int retval = 0;
	
	if (count < sizeof(unsigned char))  /* At least one character */
		return -EINVAL;

	add_wait_queue(&mkbd_wait, &wait);
	current->state = TASK_INTERRUPTIBLE;
		
	while (bytes_in_buffer() == 0) {
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}

	if (retval == 0) { /* Supposedly, we have some data in the buffer */
	  unsigned long used=0;
	  while (bytes_in_buffer()>0 && count>0 && retval==0) {
	    retval=put_user(get_from_buffer(),(unsigned char*)buf);
	    count--;
	    used++;
	    buf++;
	  }
	  if (!retval)
	    retval = used; 
	}
	
	current->state = TASK_RUNNING;
	remove_wait_queue(&mkbd_wait, &wait);
	
	return retval;
}

static ssize_t mkbd_write(struct file * file,
		       const char * buf, size_t count, loff_t* ppos )
{
  const char *tmp = buf;
  char c;
  
  for( ; count-- > 0;++tmp) {
    get_user(c,tmp);
    bytes_total_in++;
    handle_scancode(c,!(c&0x80));
  }
  return( tmp - buf );
}

static int mkbd_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
  return -EINVAL;
}

void mkbd_handle_scancode(unsigned char x, int down)
{
  put_into_buffer(x);
  wake_up_interruptible(&mkbd_wait);	
}

static int mkbd_open(struct inode *inode, struct file *file)
{
  if (number_open==0) { /* First one to open this one */
    scancode_handler=mkbd_handle_scancode;
  }
  number_open++; /* Should we check this for overflow? */
  return 0;
}

static int mkbd_release(struct inode *inode, struct file *file)
{
  number_open--;
  if (number_open==0) {
    scancode_handler=handle_scancode;
  }
  return 0;
}

/* What does this one do? No idea! */

static unsigned int mkbd_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &mkbd_wait, wait);
	if (bytes_in_buffer() > 0)
		return POLLIN | POLLRDNORM;
	return 0;
}

/*
 *	The various file operations we support.
 */

static struct file_operations mkbd_fops = {
  owner:      NULL,  /* Now why aren't I allowed to say
			THIS_MODULE here? rtc.c can... */
  llseek:     mkbd_llseek,
  read:	      mkbd_read,
  write:      mkbd_write,
  poll:	      mkbd_poll,
  ioctl:      mkbd_ioctl,
  open:	      mkbd_open,
  release:    mkbd_release,
};

static struct miscdevice mkbd_dev=
{
	MKBD_MINOR,
	"mkbd",
	&mkbd_fops
};

int __init mkbd_init(void)
{
	printk(KERN_INFO "Magic keyboard Driver v%s\n", MKBD_VERSION);
	misc_register(&mkbd_dev);

	start=end=0;
	number_open=0;
	bytes_total=0;
	bytes_total_in=0;

	return 0;
}

/*
 *	Info exported via "/proc/mkbd".
 */

int get_mkbd_status(char *buf)
{
	char *p;

	p=buf;
	p += sprintf(p,
		"bytes in buffer\t: %05d\n"
		"start          \t: %05d\n"
		"end            \t: %05d\n"
		"bytes passed on\t: %05d\n"
		"bytes passed in\t: %05d\n"
		"number of processes\t: %05d\n",
		     (unsigned int)bytes_in_buffer(),
		     (unsigned int)start,
		     (unsigned int)end,
		     (unsigned int)bytes_total,
		     (unsigned int)bytes_total_in,
		     (unsigned int)number_open);
	return  p - buf;
}


