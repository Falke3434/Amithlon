/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/spinlock.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/netfilter.h>
#include <linux/sysctl.h>
#include <net/ip.h>
#include <net/arp.h>

/*
 *	Callsign/UID mapper. This is in kernel space for security on multi-amateur machines.
 */

static ax25_uid_assoc *ax25_uid_list;
static rwlock_t ax25_uid_lock = RW_LOCK_UNLOCKED;

int ax25_uid_policy = 0;

ax25_address *ax25_findbyuid(uid_t uid)
{
	ax25_uid_assoc *ax25_uid;
	ax25_address *res = NULL;

	read_lock(&ax25_uid_lock);
	for (ax25_uid = ax25_uid_list; ax25_uid != NULL; ax25_uid = ax25_uid->next) {
		if (ax25_uid->uid == uid) {
			res = &ax25_uid->call;
			break;
		}
	}
	read_unlock(&ax25_uid_lock);

	return NULL;
}

int ax25_uid_ioctl(int cmd, struct sockaddr_ax25 *sax)
{
	ax25_uid_assoc *s, *ax25_uid;
	unsigned long res;

	switch (cmd) {
	case SIOCAX25GETUID:
		res = -ENOENT;
		read_lock(&ax25_uid_lock);
		for (ax25_uid = ax25_uid_list; ax25_uid != NULL; ax25_uid = ax25_uid->next) {
			if (ax25cmp(&sax->sax25_call, &ax25_uid->call) == 0) {
				res = ax25_uid->uid;
				break;
			}
		}
		read_unlock(&ax25_uid_lock);

		return res;

	case SIOCAX25ADDUID:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (ax25_findbyuid(sax->sax25_uid))
			return -EEXIST;
		if (sax->sax25_uid == 0)
			return -EINVAL;
		if ((ax25_uid = kmalloc(sizeof(*ax25_uid), GFP_KERNEL)) == NULL)
			return -ENOMEM;

		ax25_uid->uid  = sax->sax25_uid;
		ax25_uid->call = sax->sax25_call;

		write_lock(&ax25_uid_lock);
		ax25_uid->next = ax25_uid_list;
		ax25_uid_list  = ax25_uid;
		write_unlock(&ax25_uid_lock);

		return 0;

	case SIOCAX25DELUID:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		write_lock(&ax25_uid_lock);
		for (ax25_uid = ax25_uid_list; ax25_uid != NULL; ax25_uid = ax25_uid->next) {
			if (ax25cmp(&sax->sax25_call, &ax25_uid->call) == 0) {
				break;
			}
		}
		if (ax25_uid == NULL) {
			write_unlock(&ax25_uid_lock);
			return -ENOENT;
		}
		if ((s = ax25_uid_list) == ax25_uid) {
			ax25_uid_list = s->next;
			write_unlock(&ax25_uid_lock);
			kfree(ax25_uid);
			return 0;
		}
		while (s != NULL && s->next != NULL) {
			if (s->next == ax25_uid) {
				s->next = ax25_uid->next;
				write_unlock(&ax25_uid_lock);
				kfree(ax25_uid);
				return 0;
			}
			s = s->next;
		}
		write_unlock(&ax25_uid_lock);

		return -ENOENT;

	default:
		return -EINVAL;
	}

	return -EINVAL;	/*NOTREACHED */
}

int ax25_uid_get_info(char *buffer, char **start, off_t offset, int length)
{
	ax25_uid_assoc *pt;
	int len     = 0;
	off_t pos   = 0;
	off_t begin = 0;

	read_lock(&ax25_uid_lock);
	len += sprintf(buffer, "Policy: %d\n", ax25_uid_policy);

	for (pt = ax25_uid_list; pt != NULL; pt = pt->next) {
		len += sprintf(buffer + len, "%6d %s\n", pt->uid, ax2asc(&pt->call));

		pos = begin + len;

		if (pos < offset) {
			len = 0;
			begin = pos;
		}

		if (pos > offset + length)
			break;
	}
	read_unlock(&ax25_uid_lock);

	*start = buffer + (offset - begin);
	len   -= offset - begin;

	if (len > length)
		len = length;

	return len;
}

/*
 *	Free all memory associated with UID/Callsign structures.
 */
void __exit ax25_uid_free(void)
{
	ax25_uid_assoc *s, *ax25_uid;

	write_lock(&ax25_uid_lock);
	ax25_uid = ax25_uid_list;
	while (ax25_uid != NULL) {
		s        = ax25_uid;
		ax25_uid = ax25_uid->next;

		kfree(s);
	}
	ax25_uid_list = NULL;
	write_unlock(&ax25_uid_lock);
}
