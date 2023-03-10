/*
 *	NET3:	Support for 802.2 demultiplexing off Ethernet (Token ring
 *		is kept separate see p8022tr.c)
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *		Demultiplex 802.2 encoded protocols. We match the entry by the
 *		SSAP/DSAP pair and then deliver to the registered datalink that
 *		matches. The control byte is ignored and handling of such items
 *		is up to the routine passed the frame.
 *
 *		Unlike the 802.3 datalink we have a list of 802.2 entries as
 *		there are multiple protocols to demux. The list is currently
 *		short (3 or 4 entries at most). The current demux assumes this.
 */
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/datalink.h>
#include <linux/mm.h>
#include <linux/in.h>
#include <linux/init.h>
#include <net/p8022.h>
#include <net/llc_sap.h>

static int p8022_request(struct datalink_proto *dl, struct sk_buff *skb,
			 unsigned char *dest)
{
	llc_build_and_send_ui_pkt(dl->sap, skb, dest, dl->sap->laddr.lsap);
	return 0;
}

struct datalink_proto *register_8022_client(unsigned char type,
					    int (*func)(struct sk_buff *skb,
							struct net_device *dev,
							struct packet_type *pt))
{
	struct datalink_proto *proto;

	proto = kmalloc(sizeof(*proto), GFP_ATOMIC);
	if (proto) {
		proto->type[0]		= type;
		proto->header_length	= 3;
		proto->request		= p8022_request;
		proto->sap = llc_sap_open(type, func);
		if (!proto->sap) {
			kfree(proto);
			proto = NULL;
		}
	}
	return proto;
}

void unregister_8022_client(struct datalink_proto *proto)
{
	llc_sap_close(proto->sap);
	kfree(proto);
}

EXPORT_SYMBOL(register_8022_client);
EXPORT_SYMBOL(unregister_8022_client);

MODULE_LICENSE("GPL");
