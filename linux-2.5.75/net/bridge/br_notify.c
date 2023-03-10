/*
 *	Device event handling
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_notify.c,v 1.2 2000/02/21 15:51:34 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/if_bridge.h>
#include "br_private.h"

static int br_device_event(struct notifier_block *unused, unsigned long event, void *ptr);

struct notifier_block br_device_notifier =
{
	.notifier_call = br_device_event
};

static int br_device_event(struct notifier_block *unused, unsigned long event, void *ptr)
{
	struct net_device *dev;
	struct net_bridge_port *p;
	struct net_bridge *br;

	dev = ptr;
	p = dev->br_port;

	if (p == NULL)
		return NOTIFY_DONE;

	br = p->br;

	switch (event) 
	{
	case NETDEV_CHANGEADDR:
		spin_lock_bh(&br->lock);
		br_fdb_changeaddr(p, dev->dev_addr);
		br_stp_recalculate_bridge_id(br);
		spin_unlock_bh(&br->lock);
		break;

	case NETDEV_GOING_DOWN:
		/* extend the protocol to send some kind of notification? */
		break;

	case NETDEV_DOWN:
		if (br->dev->flags & IFF_UP) {
			spin_lock_bh(&br->lock);
			br_stp_disable_port(p);
			spin_unlock_bh(&br->lock);
		}
		break;

	case NETDEV_UP:
		if (!(br->dev->flags & IFF_UP)) {
			spin_lock_bh(&br->lock);
			br_stp_enable_port(p);
			spin_unlock_bh(&br->lock);
		}
		break;

	case NETDEV_UNREGISTER:
		br_del_if(br, dev);
		break;
	}

	return NOTIFY_DONE;
}
