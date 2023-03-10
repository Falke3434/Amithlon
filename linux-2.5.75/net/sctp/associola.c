/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * This module provides the abstraction for an SCTP association.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Hui Huang             <hui.huang@nokia.com>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 *    Ryan Layer	    <rmlayer@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/sched.h>

#include <linux/slab.h>
#include <linux/in.h>
#include <net/ipv6.h>
#include <net/sctp/sctp.h>

/* Forward declarations for internal functions. */
static void sctp_assoc_bh_rcv(struct sctp_association *asoc);


/* 1st Level Abstractions. */

/* Allocate and initialize a new association */
struct sctp_association *sctp_association_new(const struct sctp_endpoint *ep,
					 const struct sock *sk,
					 sctp_scope_t scope, int gfp)
{
	struct sctp_association *asoc;

	asoc = t_new(struct sctp_association, gfp);
	if (!asoc)
		goto fail;

	if (!sctp_association_init(asoc, ep, sk, scope, gfp))
		goto fail_init;

	asoc->base.malloced = 1;
	SCTP_DBG_OBJCNT_INC(assoc);

	return asoc;

fail_init:
	kfree(asoc);
fail:
	return NULL;
}

/* Initialize a new association from provided memory. */
struct sctp_association *sctp_association_init(struct sctp_association *asoc,
					  const struct sctp_endpoint *ep,
					  const struct sock *sk,
					  sctp_scope_t scope,
					  int gfp)
{
	struct sctp_opt *sp;
	int i;

	/* Retrieve the SCTP per socket area.  */
	sp = sctp_sk((struct sock *)sk);

	/* Init all variables to a known value.  */
	memset(asoc, 0, sizeof(struct sctp_association));

	/* Discarding const is appropriate here.  */
	asoc->ep = (struct sctp_endpoint *)ep;
	sctp_endpoint_hold(asoc->ep);

	/* Hold the sock.  */
	asoc->base.sk = (struct sock *)sk;
	sock_hold(asoc->base.sk);

	/* Initialize the common base substructure.  */
	asoc->base.type = SCTP_EP_TYPE_ASSOCIATION;

	/* Initialize the object handling fields.  */
	atomic_set(&asoc->base.refcnt, 1);
	asoc->base.dead = 0;
	asoc->base.malloced = 0;

	/* Initialize the bind addr area.  */
	sctp_bind_addr_init(&asoc->base.bind_addr, ep->base.bind_addr.port);
	asoc->base.addr_lock = RW_LOCK_UNLOCKED;

	asoc->state = SCTP_STATE_CLOSED;

	/* Set these values from the socket values, a conversion between
	 * millsecons to seconds/microseconds must also be done.
	 */
	asoc->cookie_life.tv_sec = sp->assocparams.sasoc_cookie_life / 1000;
	asoc->cookie_life.tv_usec = (sp->assocparams.sasoc_cookie_life % 1000)
					* 1000;
	asoc->pmtu = 0;
	asoc->frag_point = 0;

	/* Set the association max_retrans and RTO values from the
	 * socket values.
	 */
	asoc->max_retrans = sp->assocparams.sasoc_asocmaxrxt;
	asoc->rto_initial = sp->rtoinfo.srto_initial * HZ / 1000;
	asoc->rto_max = sp->rtoinfo.srto_max * HZ / 1000;
	asoc->rto_min = sp->rtoinfo.srto_min * HZ / 1000;

	asoc->overall_error_count = 0;

	/* Initialize the maximum mumber of new data packets that can be sent
	 * in a burst.
	 */
	asoc->max_burst = sctp_max_burst;

	/* Copy things from the endpoint.  */
	for (i = SCTP_EVENT_TIMEOUT_NONE; i < SCTP_NUM_TIMEOUT_TYPES; ++i) {
		asoc->timeouts[i] = ep->timeouts[i];
		init_timer(&asoc->timers[i]);
		asoc->timers[i].function = sctp_timer_events[i];
		asoc->timers[i].data = (unsigned long) asoc;
	}

	/* Pull default initialization values from the sock options.
	 * Note: This assumes that the values have already been
	 * validated in the sock.
	 */
	asoc->c.sinit_max_instreams = sp->initmsg.sinit_max_instreams;
	asoc->c.sinit_num_ostreams  = sp->initmsg.sinit_num_ostreams;
	asoc->max_init_attempts	= sp->initmsg.sinit_max_attempts;

	asoc->max_init_timeo    = sp->initmsg.sinit_max_init_timeo * HZ / 1000;

	/* Allocate storage for the ssnmap after the inbound and outbound
	 * streams have been negotiated during Init.
	 */
	asoc->ssnmap = NULL;

	/* Set the local window size for receive.
	 * This is also the rcvbuf space per association.
	 * RFC 6 - A SCTP receiver MUST be able to receive a minimum of
	 * 1500 bytes in one SCTP packet.
	 */
	if (sk->sk_rcvbuf < SCTP_DEFAULT_MINWINDOW)
		asoc->rwnd = SCTP_DEFAULT_MINWINDOW;
	else
		asoc->rwnd = sk->sk_rcvbuf;

	asoc->a_rwnd = asoc->rwnd;

	asoc->rwnd_over = 0;

	/* Use my own max window until I learn something better.  */
	asoc->peer.rwnd = SCTP_DEFAULT_MAXWINDOW;

	/* Set the sndbuf size for transmit.  */
	asoc->sndbuf_used = 0;

	init_waitqueue_head(&asoc->wait);

	asoc->c.my_vtag = sctp_generate_tag(ep);
	asoc->peer.i.init_tag = 0;     /* INIT needs a vtag of 0. */
	asoc->c.peer_vtag = 0;
	asoc->c.my_ttag   = 0;
	asoc->c.peer_ttag = 0;

	asoc->c.initial_tsn = sctp_generate_tsn(ep);

	asoc->next_tsn = asoc->c.initial_tsn;

	asoc->ctsn_ack_point = asoc->next_tsn - 1;
	asoc->highest_sacked = asoc->ctsn_ack_point;
	asoc->last_cwr_tsn = asoc->ctsn_ack_point;
	asoc->unack_data = 0;

	SCTP_DEBUG_PRINTK("myctsnap for %s INIT as 0x%x.\n",
			  asoc->ep->debug_name,
			  asoc->ctsn_ack_point);

	/* ADDIP Section 4.1 Asconf Chunk Procedures
	 *
	 * When an endpoint has an ASCONF signaled change to be sent to the
	 * remote endpoint it should do the following:
	 * ...
	 * A2) a serial number should be assigned to the chunk. The serial
	 * number should be a monotonically increasing number. All serial
	 * numbers are defined to be initialized at the start of the
	 * association to the same value as the initial TSN.
	 */
	asoc->addip_serial = asoc->c.initial_tsn;

	/* Make an empty list of remote transport addresses.  */
	INIT_LIST_HEAD(&asoc->peer.transport_addr_list);

	/* RFC 2960 5.1 Normal Establishment of an Association
	 *
	 * After the reception of the first data chunk in an
	 * association the endpoint must immediately respond with a
	 * sack to acknowledge the data chunk.  Subsequent
	 * acknowledgements should be done as described in Section
	 * 6.2.
	 *
	 * [We implement this by telling a new association that it
	 * already received one packet.]
	 */
	asoc->peer.sack_needed = 1;

	/* Create an input queue.  */
	sctp_inq_init(&asoc->base.inqueue);
	sctp_inq_set_th_handler(&asoc->base.inqueue,
				    (void (*)(void *))sctp_assoc_bh_rcv,
				    asoc);

	/* Create an output queue.  */
	sctp_outq_init(asoc, &asoc->outqueue);
	sctp_outq_set_output_handlers(&asoc->outqueue,
				      sctp_packet_init,
				      sctp_packet_config,
				      sctp_packet_append_chunk,
				      sctp_packet_transmit_chunk,
				      sctp_packet_transmit);

	if (!sctp_ulpq_init(&asoc->ulpq, asoc))
		goto fail_init;

	/* Set up the tsn tracking. */
	sctp_tsnmap_init(&asoc->peer.tsn_map, SCTP_TSN_MAP_SIZE, 0);

	skb_queue_head_init(&asoc->addip_chunks);

	asoc->need_ecne = 0;

	asoc->eyecatcher = SCTP_ASSOC_EYECATCHER;

	/* Assume that peer would support both address types unless we are
	 * told otherwise.
	 */
	asoc->peer.ipv4_address = 1;
	asoc->peer.ipv6_address = 1;
	INIT_LIST_HEAD(&asoc->asocs);

	asoc->autoclose = sp->autoclose;

	asoc->default_stream = sp->default_stream;
	asoc->default_ppid = sp->default_ppid;
	asoc->default_flags = sp->default_flags;
	asoc->default_context = sp->default_context;
	asoc->default_timetolive = sp->default_timetolive;

	return asoc;

fail_init:
	sctp_endpoint_put(asoc->ep);
	sock_put(asoc->base.sk);
	return NULL;
}

/* Free this association if possible.  There may still be users, so
 * the actual deallocation may be delayed.
 */
void sctp_association_free(struct sctp_association *asoc)
{
	struct sock *sk = asoc->base.sk;
	struct sctp_transport *transport;
	struct list_head *pos, *temp;
	int i;

	list_del(&asoc->asocs);

	/* Decrement the backlog value for a TCP-style listening socket. */
	if (sctp_style(sk, TCP) && sctp_sstate(sk, LISTENING))
		sk->sk_ack_backlog--;

	/* Mark as dead, so other users can know this structure is
	 * going away.
	 */
	asoc->base.dead = 1;

	/* Dispose of any data lying around in the outqueue. */
	sctp_outq_free(&asoc->outqueue);

	/* Dispose of any pending messages for the upper layer. */
	sctp_ulpq_free(&asoc->ulpq);

	/* Dispose of any pending chunks on the inqueue. */
	sctp_inq_free(&asoc->base.inqueue);

	/* Free ssnmap storage. */
	sctp_ssnmap_free(asoc->ssnmap);

	/* Clean up the bound address list. */
	sctp_bind_addr_free(&asoc->base.bind_addr);

	/* Do we need to go through all of our timers and
	 * delete them?   To be safe we will try to delete all, but we
	 * should be able to go through and make a guess based
	 * on our state.
	 */
	for (i = SCTP_EVENT_TIMEOUT_NONE; i < SCTP_NUM_TIMEOUT_TYPES; ++i) {
		if (timer_pending(&asoc->timers[i]) &&
		    del_timer(&asoc->timers[i]))
			sctp_association_put(asoc);
	}

	/* Free peer's cached cookie. */
	if (asoc->peer.cookie) {
		kfree(asoc->peer.cookie);
	}

	/* Release the transport structures. */
	list_for_each_safe(pos, temp, &asoc->peer.transport_addr_list) {
		transport = list_entry(pos, struct sctp_transport, transports);
		list_del(pos);
		sctp_transport_free(transport);
	}

	asoc->eyecatcher = 0;

	sctp_association_put(asoc);
}

/* Cleanup and free up an association. */
static void sctp_association_destroy(struct sctp_association *asoc)
{
	SCTP_ASSERT(asoc->base.dead, "Assoc is not dead", return);

	sctp_endpoint_put(asoc->ep);
	sock_put(asoc->base.sk);

	if (asoc->base.malloced) {
		kfree(asoc);
		SCTP_DBG_OBJCNT_DEC(assoc);
	}
}

/* Change the primary destination address for the peer. */
void sctp_assoc_set_primary(struct sctp_association *asoc,
			    struct sctp_transport *transport)
{
	asoc->peer.primary_path = transport;

	/* Set a default msg_name for events. */
	memcpy(&asoc->peer.primary_addr, &transport->ipaddr,
	       sizeof(union sctp_addr));

	/* If the primary path is changing, assume that the
	 * user wants to use this new path.
	 */
	if (transport->active)
		asoc->peer.active_path = transport;

	/*
	 * SFR-CACC algorithm:
	 * Upon the receipt of a request to change the primary
	 * destination address, on the data structure for the new
	 * primary destination, the sender MUST do the following:
	 *
	 * 1) If CHANGEOVER_ACTIVE is set, then there was a switch
	 * to this destination address earlier. The sender MUST set
	 * CYCLING_CHANGEOVER to indicate that this switch is a
	 * double switch to the same destination address.
	 */
	if (transport->cacc.changeover_active)
		transport->cacc.cycling_changeover = 1;

	/* 2) The sender MUST set CHANGEOVER_ACTIVE to indicate that
	 * a changeover has occurred.
	 */
	transport->cacc.changeover_active = 1;

	/* 3) The sender MUST store the next TSN to be sent in
	 * next_tsn_at_change.
	 */
	transport->cacc.next_tsn_at_change = asoc->next_tsn;
}

/* Add a transport address to an association.  */
struct sctp_transport *sctp_assoc_add_peer(struct sctp_association *asoc,
					   const union sctp_addr *addr,
					   int gfp)
{
	struct sctp_transport *peer;
	struct sctp_opt *sp;
	unsigned short port;

	sp = sctp_sk(asoc->base.sk);

	/* AF_INET and AF_INET6 share common port field. */
	port = addr->v4.sin_port;

	/* Set the port if it has not been set yet.  */
	if (0 == asoc->peer.port)
		asoc->peer.port = port;

	/* Check to see if this is a duplicate. */
	peer = sctp_assoc_lookup_paddr(asoc, addr);
	if (peer)
		return peer;

	peer = sctp_transport_new(addr, gfp);
	if (!peer)
		return NULL;

	sctp_transport_set_owner(peer, asoc);

	/* Initialize the pmtu of the transport. */
	sctp_transport_pmtu(peer);

	/* If this is the first transport addr on this association,
	 * initialize the association PMTU to the peer's PMTU.
	 * If not and the current association PMTU is higher than the new
	 * peer's PMTU, reset the association PMTU to the new peer's PMTU.
	 */
	if (asoc->pmtu)
		asoc->pmtu = min_t(int, peer->pmtu, asoc->pmtu);
	else
		asoc->pmtu = peer->pmtu;

	SCTP_DEBUG_PRINTK("sctp_assoc_add_peer:association %p PMTU set to "
			  "%d\n", asoc, asoc->pmtu);

	asoc->frag_point = sctp_frag_point(sp, asoc->pmtu);

	/* The asoc->peer.port might not be meaningful yet, but
	 * initialize the packet structure anyway.
	 */
	(asoc->outqueue.init_output)(&peer->packet,
				     peer,
				     asoc->base.bind_addr.port,
				     asoc->peer.port);

	/* 7.2.1 Slow-Start
	 *
	 * o The initial cwnd before data transmission or after a
	 *   sufficiently long idle period MUST be <= 2*MTU.
	 *
	 * o The initial value of ssthresh MAY be arbitrarily high
	 *   (for example, implementations MAY use the size of the
	 *   receiver advertised window).
	 */
	peer->cwnd = asoc->pmtu * 2;

	/* At this point, we may not have the receiver's advertised window,
	 * so initialize ssthresh to the default value and it will be set
	 * later when we process the INIT.
	 */
	peer->ssthresh = SCTP_DEFAULT_MAXWINDOW;

	peer->partial_bytes_acked = 0;
	peer->flight_size = 0;
	peer->error_threshold = peer->max_retrans;

	/* By default, enable heartbeat for peer address. */
	peer->hb_allowed = 1;

	/* Initialize the peer's heartbeat interval based on the
	 * sock configured value.
	 */
	peer->hb_interval = sp->paddrparam.spp_hbinterval * HZ;

	/* Set the path max_retrans.  */
	peer->max_retrans = asoc->max_retrans;

	/* Set the transport's RTO.initial value */
	peer->rto = asoc->rto_initial;

	/* Attach the remote transport to our asoc.  */
	list_add_tail(&peer->transports, &asoc->peer.transport_addr_list);

	/* If we do not yet have a primary path, set one.  */
	if (!asoc->peer.primary_path) {
		sctp_assoc_set_primary(asoc, peer);
		asoc->peer.retran_path = peer;
	}

	if (asoc->peer.active_path == asoc->peer.retran_path)
		asoc->peer.retran_path = peer;

	return peer;
}

/* Lookup a transport by address. */
struct sctp_transport *sctp_assoc_lookup_paddr(
					const struct sctp_association *asoc,
					const union sctp_addr *address)
{
	struct sctp_transport *t;
	struct list_head *pos;

	/* Cycle through all transports searching for a peer address. */

	list_for_each(pos, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, struct sctp_transport, transports);
		if (sctp_cmp_addr_exact(address, &t->ipaddr))
			return t;
	}

	return NULL;
}

/* Engage in transport control operations.
 * Mark the transport up or down and send a notification to the user.
 * Select and update the new active and retran paths.
 */
void sctp_assoc_control_transport(struct sctp_association *asoc,
				  struct sctp_transport *transport,
				  sctp_transport_cmd_t command,
				  sctp_sn_error_t error)
{
	struct sctp_transport *t = NULL;
	struct sctp_transport *first;
	struct sctp_transport *second;
	struct sctp_ulpevent *event;
	struct list_head *pos;
	int spc_state = 0;

	/* Record the transition on the transport.  */
	switch (command) {
	case SCTP_TRANSPORT_UP:
		transport->active = SCTP_ACTIVE;
		spc_state = ADDRESS_AVAILABLE;
		break;

	case SCTP_TRANSPORT_DOWN:
		transport->active = SCTP_INACTIVE;
		spc_state = ADDRESS_UNREACHABLE;
		break;

	default:
		return;
	};

	/* Generate and send a SCTP_PEER_ADDR_CHANGE notification to the
	 * user.
	 */
	event = sctp_ulpevent_make_peer_addr_change(asoc,
				(struct sockaddr_storage *) &transport->ipaddr,
				0, spc_state, error, GFP_ATOMIC);
	if (event)
		sctp_ulpq_tail_event(&asoc->ulpq, event);

	/* Select new active and retran paths. */

	/* Look for the two most recently used active transports.
	 *
	 * This code produces the wrong ordering whenever jiffies
	 * rolls over, but we still get usable transports, so we don't
	 * worry about it.
	 */
	first = NULL; second = NULL;

	list_for_each(pos, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, struct sctp_transport, transports);

		if (!t->active)
			continue;
		if (!first || t->last_time_heard > first->last_time_heard) {
			second = first;
			first = t;
		}
		if (!second || t->last_time_heard > second->last_time_heard)
			second = t;
	}

	/* RFC 2960 6.4 Multi-Homed SCTP Endpoints
	 *
	 * By default, an endpoint should always transmit to the
	 * primary path, unless the SCTP user explicitly specifies the
	 * destination transport address (and possibly source
	 * transport address) to use.
	 *
	 * [If the primary is active but not most recent, bump the most
	 * recently used transport.]
	 */
	if (asoc->peer.primary_path->active &&
	    first != asoc->peer.primary_path) {
		second = first;
		first = asoc->peer.primary_path;
	}

	/* If we failed to find a usable transport, just camp on the
	 * primary, even if it is inactive.
	 */
	if (!first) {
		first = asoc->peer.primary_path;
		second = asoc->peer.primary_path;
	}

	/* Set the active and retran transports.  */
	asoc->peer.active_path = first;
	asoc->peer.retran_path = second;
}

/* Hold a reference to an association. */
void sctp_association_hold(struct sctp_association *asoc)
{
	atomic_inc(&asoc->base.refcnt);
}

/* Release a reference to an association and cleanup
 * if there are no more references.
 */
void sctp_association_put(struct sctp_association *asoc)
{
	if (atomic_dec_and_test(&asoc->base.refcnt))
		sctp_association_destroy(asoc);
}

/* Allocate the next TSN, Transmission Sequence Number, for the given
 * association.
 */
__u32 sctp_association_get_next_tsn(struct sctp_association *asoc)
{
	/* From Section 1.6 Serial Number Arithmetic:
	 * Transmission Sequence Numbers wrap around when they reach
	 * 2**32 - 1.  That is, the next TSN a DATA chunk MUST use
	 * after transmitting TSN = 2*32 - 1 is TSN = 0.
	 */
	__u32 retval = asoc->next_tsn;
	asoc->next_tsn++;
	asoc->unack_data++;

	return retval;
}

/* Allocate 'num' TSNs by incrementing the association's TSN by num. */
__u32 sctp_association_get_tsn_block(struct sctp_association *asoc, int num)
{
	__u32 retval = asoc->next_tsn;

	asoc->next_tsn += num;
	asoc->unack_data += num;

	return retval;
}


/* Compare two addresses to see if they match.  Wildcard addresses
 * only match themselves.
 */
int sctp_cmp_addr_exact(const union sctp_addr *ss1,
			const union sctp_addr *ss2)
{
	struct sctp_af *af;

	af = sctp_get_af_specific(ss1->sa.sa_family);
	if (unlikely(!af))
		return 0;

	return af->cmp_addr(ss1, ss2);
}

/* Return an ecne chunk to get prepended to a packet.
 * Note:  We are sly and return a shared, prealloced chunk.  FIXME:
 * No we don't, but we could/should.
 */
struct sctp_chunk *sctp_get_ecne_prepend(struct sctp_association *asoc)
{
	struct sctp_chunk *chunk;

	/* Send ECNE if needed.
	 * Not being able to allocate a chunk here is not deadly.
	 */
	if (asoc->need_ecne)
		chunk = sctp_make_ecne(asoc, asoc->last_ecne_tsn);
	else
		chunk = NULL;

	return chunk;
}

/* Use this function for the packet prepend callback when no ECNE
 * packet is desired (e.g. some packets don't like to be bundled).
 */
struct sctp_chunk *sctp_get_no_prepend(struct sctp_association *asoc)
{
	return NULL;
}

/*
 * Find which transport this TSN was sent on.
 */
struct sctp_transport *sctp_assoc_lookup_tsn(struct sctp_association *asoc,
					     __u32 tsn)
{
	struct sctp_transport *active;
	struct sctp_transport *match;
	struct list_head *entry, *pos;
	struct sctp_transport *transport;
	struct sctp_chunk *chunk;
	__u32 key = htonl(tsn);

	match = NULL;

	/*
	 * FIXME: In general, find a more efficient data structure for
	 * searching.
	 */

	/*
	 * The general strategy is to search each transport's transmitted
	 * list.   Return which transport this TSN lives on.
	 *
	 * Let's be hopeful and check the active_path first.
	 * Another optimization would be to know if there is only one
	 * outbound path and not have to look for the TSN at all.
	 *
	 */

	active = asoc->peer.active_path;

	list_for_each(entry, &active->transmitted) {
		chunk = list_entry(entry, struct sctp_chunk, transmitted_list);

		if (key == chunk->subh.data_hdr->tsn) {
			match = active;
			goto out;
		}
	}

	/* If not found, go search all the other transports. */
	list_for_each(pos, &asoc->peer.transport_addr_list) {
		transport = list_entry(pos, struct sctp_transport, transports);

		if (transport == active)
			break;
		list_for_each(entry, &transport->transmitted) {
			chunk = list_entry(entry, struct sctp_chunk,
					   transmitted_list);
			if (key == chunk->subh.data_hdr->tsn) {
				match = transport;
				goto out;
			}
		}
	}
out:
	return match;
}

/* Is this the association we are looking for? */
struct sctp_transport *sctp_assoc_is_match(struct sctp_association *asoc,
					   const union sctp_addr *laddr,
					   const union sctp_addr *paddr)
{
	struct sctp_transport *transport;

	sctp_read_lock(&asoc->base.addr_lock);

	if ((asoc->base.bind_addr.port == laddr->v4.sin_port) &&
	    (asoc->peer.port == paddr->v4.sin_port)) {
		transport = sctp_assoc_lookup_paddr(asoc, paddr);
		if (!transport)
			goto out;

		if (sctp_bind_addr_match(&asoc->base.bind_addr, laddr,
					 sctp_sk(asoc->base.sk)))
			goto out;
	}
	transport = NULL;

out:
	sctp_read_unlock(&asoc->base.addr_lock);
	return transport;
}

/*  Is this a live association structure. */
int sctp_assoc_valid(struct sock *sk, struct sctp_association *asoc)
{

	/* First, verify that this is a kernel address. */
	if (!sctp_is_valid_kaddr((unsigned long) asoc))
		return 0;

	/* Verify that this _is_ an sctp_association
	 * data structure and if so, that the socket matches.
	 */
	if (SCTP_ASSOC_EYECATCHER != asoc->eyecatcher)
		return 0;
	if (asoc->base.sk != sk)
		return 0;

	/* The association is valid. */
	return 1;
}

/* Do delayed input processing.  This is scheduled by sctp_rcv(). */
static void sctp_assoc_bh_rcv(struct sctp_association *asoc)
{
	struct sctp_endpoint *ep;
	struct sctp_chunk *chunk;
	struct sock *sk;
	struct sctp_inq *inqueue;
	int state, subtype;
	int error = 0;

	/* The association should be held so we should be safe. */
	ep = asoc->ep;
	sk = asoc->base.sk;

	inqueue = &asoc->base.inqueue;
	while (NULL != (chunk = sctp_inq_pop(inqueue))) {
		state = asoc->state;
		subtype = chunk->chunk_hdr->type;

		/* Remember where the last DATA chunk came from so we
		 * know where to send the SACK.
		 */
		if (sctp_chunk_is_data(chunk))
			asoc->peer.last_data_from = chunk->transport;
		else
			SCTP_INC_STATS(SctpInCtrlChunks);

		if (chunk->transport)
			chunk->transport->last_time_heard = jiffies;

		/* Run through the state machine. */
		error = sctp_do_sm(SCTP_EVENT_T_CHUNK, SCTP_ST_CHUNK(subtype),
				   state, ep, asoc, chunk, GFP_ATOMIC);

		/* Check to see if the association is freed in response to
		 * the incoming chunk.  If so, get out of the while loop.
		 */
		if (!sctp_assoc_valid(sk, asoc))
			break;

		/* If there is an error on chunk, discard this packet. */
		if (error && chunk)
			chunk->pdiscard = 1;
	}

}

/* This routine moves an association from its old sk to a new sk.  */
void sctp_assoc_migrate(struct sctp_association *assoc, struct sock *newsk)
{
	struct sctp_opt *newsp = sctp_sk(newsk);
	struct sock *oldsk = assoc->base.sk;

	/* Delete the association from the old endpoint's list of
	 * associations.
	 */
	list_del(&assoc->asocs);

	/* Decrement the backlog value for a TCP-style socket. */
	if (sctp_style(oldsk, TCP))
		oldsk->sk_ack_backlog--;

	/* Release references to the old endpoint and the sock.  */
	sctp_endpoint_put(assoc->ep);
	sock_put(assoc->base.sk);

	/* Get a reference to the new endpoint.  */
	assoc->ep = newsp->ep;
	sctp_endpoint_hold(assoc->ep);

	/* Get a reference to the new sock.  */
	assoc->base.sk = newsk;
	sock_hold(assoc->base.sk);

	/* Add the association to the new endpoint's list of associations.  */
	sctp_endpoint_add_asoc(newsp->ep, assoc);
}

/* Update an association (possibly from unexpected COOKIE-ECHO processing).  */
void sctp_assoc_update(struct sctp_association *asoc,
		       struct sctp_association *new)
{
	/* Copy in new parameters of peer. */
	asoc->c = new->c;
	asoc->peer.rwnd = new->peer.rwnd;
	asoc->peer.sack_needed = new->peer.sack_needed;
	asoc->peer.i = new->peer.i;
	sctp_tsnmap_init(&asoc->peer.tsn_map, SCTP_TSN_MAP_SIZE,
			 asoc->peer.i.initial_tsn);

	/* FIXME:
	 *    Do we need to copy primary_path etc?
	 *
	 *    More explicitly, addresses may have been removed and
	 *    this needs accounting for.
	 */

	/* If the case is A (association restart), use
	 * initial_tsn as next_tsn. If the case is B, use
	 * current next_tsn in case data sent to peer
	 * has been discarded and needs retransmission.
	 */
	if (sctp_state(asoc, ESTABLISHED)) {

		asoc->next_tsn = new->next_tsn;
		asoc->ctsn_ack_point = new->ctsn_ack_point;

		/* Reinitialize SSN for both local streams
		 * and peer's streams.
		 */
		sctp_ssnmap_clear(asoc->ssnmap);

	} else {
		asoc->ctsn_ack_point = asoc->next_tsn - 1;
		if (!asoc->ssnmap) {
			/* Move the ssnmap. */
			asoc->ssnmap = new->ssnmap;
			new->ssnmap = NULL;
		}
	}

}

/* Update the retran path for sending a retransmitted packet.
 * Round-robin through the active transports, else round-robin
 * through the inactive transports as this is the next best thing
 * we can try.
 */
void sctp_assoc_update_retran_path(struct sctp_association *asoc)
{
	struct sctp_transport *t, *next;
	struct list_head *head = &asoc->peer.transport_addr_list;
	struct list_head *pos;

	/* Find the next transport in a round-robin fashion. */
	t = asoc->peer.retran_path;
	pos = &t->transports;
	next = NULL;

	while (1) {
		/* Skip the head. */
		if (pos->next == head)
			pos = head->next;
		else
			pos = pos->next;

		t = list_entry(pos, struct sctp_transport, transports);

		/* Try to find an active transport. */

		if (t->active) {
			break;
		} else {
			/* Keep track of the next transport in case
			 * we don't find any active transport.
			 */
			if (!next)
				next = t;
		}

		/* We have exhausted the list, but didn't find any
		 * other active transports.  If so, use the next
		 * transport.
		 */
		if (t == asoc->peer.retran_path) {
			t = next;
			break;
		}
	}

	asoc->peer.retran_path = t;
}

/* Choose the transport for sending a SHUTDOWN packet.  */
struct sctp_transport *sctp_assoc_choose_shutdown_transport(
	struct sctp_association *asoc)
{
	/* If this is the first time SHUTDOWN is sent, use the active path,
	 * else use the retran path. If the last SHUTDOWN was sent over the
	 * retran path, update the retran path and use it.
	 */
	if (!asoc->shutdown_last_sent_to)
		return asoc->peer.active_path;
	else {
		if (asoc->shutdown_last_sent_to == asoc->peer.retran_path)
			sctp_assoc_update_retran_path(asoc);
		return asoc->peer.retran_path;
	}

}

/* Update the association's pmtu and frag_point by going through all the
 * transports. This routine is called when a transport's PMTU has changed.
 */
void sctp_assoc_sync_pmtu(struct sctp_association *asoc)
{
	struct sctp_transport *t;
	struct list_head *pos;
	__u32 pmtu = 0;

	if (!asoc)
		return;

	/* Get the lowest pmtu of all the transports. */
	list_for_each(pos, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, struct sctp_transport, transports);
		if (!pmtu || (t->pmtu < pmtu))
			pmtu = t->pmtu;
	}

	if (pmtu) {
		struct sctp_opt *sp = sctp_sk(asoc->base.sk);
		asoc->pmtu = pmtu;
		asoc->frag_point = sctp_frag_point(sp, pmtu);
	}

	SCTP_DEBUG_PRINTK("%s: asoc:%p, pmtu:%d, frag_point:%d\n",
			  __FUNCTION__, asoc, asoc->pmtu, asoc->frag_point);
}

/* Should we send a SACK to update our peer? */
static inline int sctp_peer_needs_update(struct sctp_association *asoc)
{
	switch (asoc->state) {
	case SCTP_STATE_ESTABLISHED:
	case SCTP_STATE_SHUTDOWN_PENDING:
	case SCTP_STATE_SHUTDOWN_RECEIVED:
		if ((asoc->rwnd > asoc->a_rwnd) &&
		    ((asoc->rwnd - asoc->a_rwnd) >=
		     min_t(__u32, (asoc->base.sk->sk_rcvbuf >> 1), asoc->pmtu)))
			return 1;
		break;
	default:
		break;
	}
	return 0;
}

/* Increase asoc's rwnd by len and send any window update SACK if needed. */
void sctp_assoc_rwnd_increase(struct sctp_association *asoc, unsigned len)
{
	struct sctp_chunk *sack;
	struct timer_list *timer;

	if (asoc->rwnd_over) {
		if (asoc->rwnd_over >= len) {
			asoc->rwnd_over -= len;
		} else {
			asoc->rwnd += (len - asoc->rwnd_over);
			asoc->rwnd_over = 0;
		}
	} else {
		asoc->rwnd += len;
	}

	SCTP_DEBUG_PRINTK("%s: asoc %p rwnd increased by %d to (%u, %u) "
			  "- %u\n", __FUNCTION__, asoc, len, asoc->rwnd,
			  asoc->rwnd_over, asoc->a_rwnd);

	/* Send a window update SACK if the rwnd has increased by at least the
	 * minimum of the association's PMTU and half of the receive buffer.
	 * The algorithm used is similar to the one described in
	 * Section 4.2.3.3 of RFC 1122.
	 */
	if (sctp_peer_needs_update(asoc)) {
		asoc->a_rwnd = asoc->rwnd;
		SCTP_DEBUG_PRINTK("%s: Sending window update SACK- asoc: %p "
				  "rwnd: %u a_rwnd: %u\n", __FUNCTION__,
				  asoc, asoc->rwnd, asoc->a_rwnd);
		sack = sctp_make_sack(asoc);
		if (!sack)
			return;

		asoc->peer.sack_needed = 0;

		sctp_outq_tail(&asoc->outqueue, sack);

		/* Stop the SACK timer.  */
		timer = &asoc->timers[SCTP_EVENT_TIMEOUT_SACK];
		if (timer_pending(timer) && del_timer(timer))
			sctp_association_put(asoc);
	}
}

/* Decrease asoc's rwnd by len. */
void sctp_assoc_rwnd_decrease(struct sctp_association *asoc, unsigned len)
{
	SCTP_ASSERT(asoc->rwnd, "rwnd zero", return);
	SCTP_ASSERT(!asoc->rwnd_over, "rwnd_over not zero", return);
	if (asoc->rwnd >= len) {
		asoc->rwnd -= len;
	} else {
		asoc->rwnd_over = len - asoc->rwnd;
		asoc->rwnd = 0;
	}
	SCTP_DEBUG_PRINTK("%s: asoc %p rwnd decreased by %d to (%u, %u)\n",
			  __FUNCTION__, asoc, len, asoc->rwnd,
			  asoc->rwnd_over);
}

/* Build the bind address list for the association based on info from the
 * local endpoint and the remote peer.
 */
int sctp_assoc_set_bind_addr_from_ep(struct sctp_association *asoc, int gfp)
{
	sctp_scope_t scope;
	int flags;

	/* Use scoping rules to determine the subset of addresses from
	 * the endpoint.
	 */
	scope = sctp_scope(&asoc->peer.active_path->ipaddr);
	flags = (PF_INET6 == asoc->base.sk->sk_family) ? SCTP_ADDR6_ALLOWED : 0;
	if (asoc->peer.ipv4_address)
		flags |= SCTP_ADDR4_PEERSUPP;
	if (asoc->peer.ipv6_address)
		flags |= SCTP_ADDR6_PEERSUPP;

	return sctp_bind_addr_copy(&asoc->base.bind_addr,
				   &asoc->ep->base.bind_addr,
				   scope, gfp, flags);
}

/* Build the association's bind address list from the cookie.  */
int sctp_assoc_set_bind_addr_from_cookie(struct sctp_association *asoc,
					 struct sctp_cookie *cookie, int gfp)
{
	int var_size2 = ntohs(cookie->peer_init->chunk_hdr.length);
	int var_size3 = cookie->raw_addr_list_len;
	__u8 *raw = (__u8 *)cookie + sizeof(struct sctp_cookie) + var_size2;

	return sctp_raw_to_bind_addrs(&asoc->base.bind_addr, raw, var_size3,
				      asoc->ep->base.bind_addr.port, gfp);
}
