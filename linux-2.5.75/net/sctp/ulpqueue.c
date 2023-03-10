/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This abstraction carries sctp events to the ULP (sockets).
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
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/sctp/structs.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* Forward declarations for internal helpers.  */
static inline struct sctp_ulpevent * sctp_ulpq_reasm(struct sctp_ulpq *ulpq,
						     struct sctp_ulpevent *);
static inline struct sctp_ulpevent *sctp_ulpq_order(struct sctp_ulpq *,
						    struct sctp_ulpevent *);

/* 1st Level Abstractions */

/* Create a new ULP queue.  */
struct sctp_ulpq *sctp_ulpq_new(struct sctp_association *asoc, int gfp)
{
	struct sctp_ulpq *ulpq;

	ulpq = kmalloc(sizeof(struct sctp_ulpq), gfp);
	if (!ulpq)
		goto fail;
	if (!sctp_ulpq_init(ulpq, asoc))
		goto fail_init;
	ulpq->malloced = 1;
	return ulpq;

fail_init:
	kfree(ulpq);
fail:
	return NULL;
}

/* Initialize a ULP queue from a block of memory.  */
struct sctp_ulpq *sctp_ulpq_init(struct sctp_ulpq *ulpq,
				 struct sctp_association *asoc)
{
	memset(ulpq, sizeof(struct sctp_ulpq), 0x00);

	ulpq->asoc = asoc;
	skb_queue_head_init(&ulpq->reasm);
	skb_queue_head_init(&ulpq->lobby);
	ulpq->pd_mode  = 0;
	ulpq->malloced = 0;

	return ulpq;
}


/* Flush the reassembly and ordering queues.  */
void sctp_ulpq_flush(struct sctp_ulpq *ulpq)
{
	struct sk_buff *skb;
	struct sctp_ulpevent *event;

	while ((skb = __skb_dequeue(&ulpq->lobby))) {
		event = sctp_skb2event(skb);
		sctp_ulpevent_free(event);
	}

	while ((skb = __skb_dequeue(&ulpq->reasm))) {
		event = sctp_skb2event(skb);
		sctp_ulpevent_free(event);
	}

}

/* Dispose of a ulpqueue.  */
void sctp_ulpq_free(struct sctp_ulpq *ulpq)
{
	sctp_ulpq_flush(ulpq);
	if (ulpq->malloced)
		kfree(ulpq);
}

/* Process an incoming DATA chunk.  */
int sctp_ulpq_tail_data(struct sctp_ulpq *ulpq, struct sctp_chunk *chunk,
			int gfp)
{
	struct sk_buff_head temp;
	sctp_data_chunk_t *hdr;
	struct sctp_ulpevent *event;

	hdr = (sctp_data_chunk_t *) chunk->chunk_hdr;

	/* Create an event from the incoming chunk. */
	event = sctp_ulpevent_make_rcvmsg(chunk->asoc, chunk, gfp);
	if (!event)
		return -ENOMEM;

	/* Do reassembly if needed.  */
	event = sctp_ulpq_reasm(ulpq, event);

	/* Do ordering if needed.  */
	if ((event) && (event->msg_flags & MSG_EOR)){
		/* Create a temporary list to collect chunks on.  */
		skb_queue_head_init(&temp);
		__skb_queue_tail(&temp, sctp_event2skb(event));

		event = sctp_ulpq_order(ulpq, event);
	}

	/* Send event to the ULP.  */
	if (event)
		sctp_ulpq_tail_event(ulpq, event);

	return 0;
}

/* Add a new event for propagation to the ULP.  */
/* Clear the partial delivery mode for this socket.   Note: This
 * assumes that no association is currently in partial delivery mode.
 */
int sctp_clear_pd(struct sock *sk)
{
	struct sctp_opt *sp;
	sp = sctp_sk(sk);

	sp->pd_mode = 0;
	if (!skb_queue_empty(&sp->pd_lobby)) {
		struct list_head *list;
		sctp_skb_list_tail(&sp->pd_lobby, &sk->sk_receive_queue);
		list = (struct list_head *)&sctp_sk(sk)->pd_lobby;
		INIT_LIST_HEAD(list);
		return 1;
	}
	return 0;
}

/* Clear the pd_mode and restart any pending messages waiting for delivery. */
static int sctp_ulpq_clear_pd(struct sctp_ulpq *ulpq)
{
	ulpq->pd_mode = 0;
	return sctp_clear_pd(ulpq->asoc->base.sk);
}



int sctp_ulpq_tail_event(struct sctp_ulpq *ulpq, struct sctp_ulpevent *event)
{
	struct sock *sk = ulpq->asoc->base.sk;
	struct sk_buff_head *queue;
	int clear_pd = 0;

	/* If the socket is just going to throw this away, do not
	 * even try to deliver it.
	 */
	if (sock_flag(sk, SOCK_DEAD) || (sk->sk_shutdown & RCV_SHUTDOWN))
		goto out_free;

	/* Check if the user wishes to receive this event.  */
	if (!sctp_ulpevent_is_enabled(event, &sctp_sk(sk)->subscribe))
		goto out_free;

	/* If we are in partial delivery mode, post to the lobby until
	 * partial delivery is cleared, unless, of course _this_ is
	 * the association the cause of the partial delivery.
	 */

	if (!sctp_sk(sk)->pd_mode) {
		queue = &sk->sk_receive_queue;
	} else if (ulpq->pd_mode) {
		if (event->msg_flags & MSG_NOTIFICATION)
		       	queue = &sctp_sk(sk)->pd_lobby;
		else {
			clear_pd = event->msg_flags & MSG_EOR;
			queue = &sk->sk_receive_queue;
		}
	} else
		queue = &sctp_sk(sk)->pd_lobby;


	/* If we are harvesting multiple skbs they will be
	 * collected on a list.
	 */
	if (sctp_event2skb(event)->list)
		sctp_skb_list_tail(sctp_event2skb(event)->list, queue);
	else
		__skb_queue_tail(queue, sctp_event2skb(event));

	/* Did we just complete partial delivery and need to get
	 * rolling again?  Move pending data to the receive
	 * queue.
	 */
	if (clear_pd)
		sctp_ulpq_clear_pd(ulpq);

	if (queue == &sk->sk_receive_queue)
		sk->sk_data_ready(sk, 0);
	return 1;

out_free:
	if (sctp_event2skb(event)->list)
		skb_queue_purge(sctp_event2skb(event)->list);
	else
		kfree_skb(sctp_event2skb(event));
	return 0;
}

/* 2nd Level Abstractions */

/* Helper function to store chunks that need to be reassembled.  */
static inline void sctp_ulpq_store_reasm(struct sctp_ulpq *ulpq,
					 struct sctp_ulpevent *event)
{
	struct sk_buff *pos;
	struct sctp_ulpevent *cevent;
	__u32 tsn, ctsn;

	tsn = event->sndrcvinfo.sinfo_tsn;

	/* See if it belongs at the end. */
	pos = skb_peek_tail(&ulpq->reasm);
	if (!pos) {
		__skb_queue_tail(&ulpq->reasm, sctp_event2skb(event));
		return;
	}

	/* Short circuit just dropping it at the end. */
	cevent = sctp_skb2event(pos);
	ctsn = cevent->sndrcvinfo.sinfo_tsn;
	if (TSN_lt(ctsn, tsn)) {
		__skb_queue_tail(&ulpq->reasm, sctp_event2skb(event));
		return;
	}

	/* Find the right place in this list. We store them by TSN.  */
	skb_queue_walk(&ulpq->reasm, pos) {
		cevent = sctp_skb2event(pos);
		ctsn = cevent->sndrcvinfo.sinfo_tsn;

		if (TSN_lt(tsn, ctsn))
			break;
	}

	/* Insert before pos. */
	__skb_insert(sctp_event2skb(event), pos->prev, pos, &ulpq->reasm);

}

/* Helper function to return an event corresponding to the reassembled
 * datagram.
 * This routine creates a re-assembled skb given the first and last skb's
 * as stored in the reassembly queue. The skb's may be non-linear if the sctp
 * payload was fragmented on the way and ip had to reassemble them.
 * We add the rest of skb's to the first skb's fraglist.
 */
static inline struct sctp_ulpevent *sctp_make_reassembled_event(struct sk_buff *f_frag, struct sk_buff *l_frag)
{
	struct sk_buff *pos;
	struct sctp_ulpevent *event;
	struct sk_buff *pnext, *last;
	struct sk_buff *list = skb_shinfo(f_frag)->frag_list;

	/* Store the pointer to the 2nd skb */
	if (f_frag == l_frag)
		pos = NULL;
	else
		pos = f_frag->next;

	/* Get the last skb in the f_frag's frag_list if present. */
	for (last = list; list; last = list, list = list->next);

	/* Add the list of remaining fragments to the first fragments
	 * frag_list.
	 */
	if (last)
		last->next = pos;
	else
		skb_shinfo(f_frag)->frag_list = pos;

	/* Remove the first fragment from the reassembly queue.  */
	__skb_unlink(f_frag, f_frag->list);
	while (pos) {

		pnext = pos->next;

		/* Update the len and data_len fields of the first fragment. */
		f_frag->len += pos->len;
		f_frag->data_len += pos->len;

		/* Remove the fragment from the reassembly queue.  */
		__skb_unlink(pos, pos->list);

		/* Break if we have reached the last fragment.  */
		if (pos == l_frag)
			break;

		pos->next = pnext;
		pos = pnext;
	};

	event = sctp_skb2event(f_frag);
	SCTP_INC_STATS(SctpReasmUsrMsgs);

	return event;
}


/* Helper function to check if an incoming chunk has filled up the last
 * missing fragment in a SCTP datagram and return the corresponding event.
 */
static inline struct sctp_ulpevent *sctp_ulpq_retrieve_reassembled(struct sctp_ulpq *ulpq)
{
	struct sk_buff *pos;
	struct sctp_ulpevent *cevent;
	struct sk_buff *first_frag = NULL;
	__u32 ctsn, next_tsn;
	struct sctp_ulpevent *retval = NULL;

	/* Initialized to 0 just to avoid compiler warning message.  Will
	 * never be used with this value. It is referenced only after it
	 * is set when we find the first fragment of a message.
	 */
	next_tsn = 0;

	/* The chunks are held in the reasm queue sorted by TSN.
	 * Walk through the queue sequentially and look for a sequence of
	 * fragmented chunks that complete a datagram.
	 * 'first_frag' and next_tsn are reset when we find a chunk which
	 * is the first fragment of a datagram. Once these 2 fields are set
	 * we expect to find the remaining middle fragments and the last
	 * fragment in order. If not, first_frag is reset to NULL and we
	 * start the next pass when we find another first fragment.
	 */
	skb_queue_walk(&ulpq->reasm, pos) {
		cevent = sctp_skb2event(pos);
		ctsn = cevent->sndrcvinfo.sinfo_tsn;

		switch (cevent->msg_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			first_frag = pos;
			next_tsn = ctsn + 1;
			break;

		case SCTP_DATA_MIDDLE_FRAG:
			if ((first_frag) && (ctsn == next_tsn))
				next_tsn++;
			else
				first_frag = NULL;
			break;

		case SCTP_DATA_LAST_FRAG:
			if (first_frag && (ctsn == next_tsn))
				goto found;
			else
				first_frag = NULL;
			break;
		};

	}
done:
	return retval;
found:
	retval = sctp_make_reassembled_event(first_frag, pos);
	if (retval)
		retval->msg_flags |= MSG_EOR;
	goto done;
}

/* Retrieve the next set of fragments of a partial message. */
static inline struct sctp_ulpevent *sctp_ulpq_retrieve_partial(struct sctp_ulpq *ulpq)
{
	struct sk_buff *pos, *last_frag, *first_frag;
	struct sctp_ulpevent *cevent;
	__u32 ctsn, next_tsn;
	int is_last;
	struct sctp_ulpevent *retval;

	/* The chunks are held in the reasm queue sorted by TSN.
	 * Walk through the queue sequentially and look for the first
	 * sequence of fragmented chunks.
	 */

	if (skb_queue_empty(&ulpq->reasm))
		return NULL;

	last_frag = first_frag = NULL;
	retval = NULL;
	next_tsn = 0;
	is_last = 0;

	skb_queue_walk(&ulpq->reasm, pos) {
		cevent = sctp_skb2event(pos);
		ctsn = cevent->sndrcvinfo.sinfo_tsn;

		switch (cevent->msg_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_MIDDLE_FRAG:
			if (!first_frag) {
				first_frag = pos;
				next_tsn = ctsn + 1;
				last_frag = pos;
			} else if (next_tsn == ctsn)
				next_tsn++;
			else
				goto done;
			break;
		case SCTP_DATA_LAST_FRAG:
			if (!first_frag)
				first_frag = pos;
			else if (ctsn != next_tsn)
				goto done;
			last_frag = pos;
			is_last = 1;
			goto done;
		default:
			return NULL;
		};
	}

	/* We have the reassembled event. There is no need to look
	 * further.
	 */
done:
	retval = sctp_make_reassembled_event(first_frag, last_frag);
	if (retval && is_last)
		retval->msg_flags |= MSG_EOR;

	return retval;
}


/* Helper function to reassemble chunks.  Hold chunks on the reasm queue that
 * need reassembling.
 */
static inline struct sctp_ulpevent *sctp_ulpq_reasm(struct sctp_ulpq *ulpq,
						   struct sctp_ulpevent *event)
{
	struct sctp_ulpevent *retval = NULL;

	/* Check if this is part of a fragmented message.  */
	if (SCTP_DATA_NOT_FRAG == (event->msg_flags & SCTP_DATA_FRAG_MASK)) {
		event->msg_flags |= MSG_EOR;
		return event;
	}

	sctp_ulpq_store_reasm(ulpq, event);
	if (!ulpq->pd_mode)
		retval = sctp_ulpq_retrieve_reassembled(ulpq);
	else {
		__u32 ctsn, ctsnap;

		/* Do not even bother unless this is the next tsn to
		 * be delivered.
		 */
		ctsn = event->sndrcvinfo.sinfo_tsn;
		ctsnap = sctp_tsnmap_get_ctsn(&ulpq->asoc->peer.tsn_map);
		if (TSN_lte(ctsn, ctsnap))
			retval = sctp_ulpq_retrieve_partial(ulpq);
	}

	return retval;
}

/* Retrieve the first part (sequential fragments) for partial delivery.  */
static inline struct sctp_ulpevent *sctp_ulpq_retrieve_first(struct sctp_ulpq *ulpq)
{
	struct sk_buff *pos, *last_frag, *first_frag;
	struct sctp_ulpevent *cevent;
	__u32 ctsn, next_tsn;
	struct sctp_ulpevent *retval;

	/* The chunks are held in the reasm queue sorted by TSN.
	 * Walk through the queue sequentially and look for a sequence of
	 * fragmented chunks that start a datagram.
	 */

	if (skb_queue_empty(&ulpq->reasm))
		return NULL;

	last_frag = first_frag = NULL;
	retval = NULL;
	next_tsn = 0;

	skb_queue_walk(&ulpq->reasm, pos) {
		cevent = sctp_skb2event(pos);
		ctsn = cevent->sndrcvinfo.sinfo_tsn;

		switch (cevent->msg_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			if (!first_frag) {
				first_frag = pos;
				next_tsn = ctsn + 1;
				last_frag = pos;
			} else
				goto done;
			break;

		case SCTP_DATA_MIDDLE_FRAG:
			if (!first_frag)
				return NULL;
			if (ctsn == next_tsn) {
				next_tsn++;
				last_frag = pos;
			} else
				goto done;
			break;
		default:
			return NULL;
		};
	}

	/* We have the reassembled event. There is no need to look
	 * further.
	 */
done:
	retval = sctp_make_reassembled_event(first_frag, last_frag);
	return retval;
}

/* Helper function to gather skbs that have possibly become
 * ordered by an an incoming chunk.
 */
static inline void sctp_ulpq_retrieve_ordered(struct sctp_ulpq *ulpq,
					      struct sctp_ulpevent *event)
{
	struct sk_buff *pos, *tmp;
	struct sctp_ulpevent *cevent;
	struct sctp_stream *in;
	__u16 sid, csid;
	__u16 ssn, cssn;

	sid = event->sndrcvinfo.sinfo_stream;
	ssn = event->sndrcvinfo.sinfo_ssn;
	in  = &ulpq->asoc->ssnmap->in;

	/* We are holding the chunks by stream, by SSN.  */
	sctp_skb_for_each(pos, &ulpq->lobby, tmp) {
		cevent = (struct sctp_ulpevent *) pos->cb;
		csid = cevent->sndrcvinfo.sinfo_stream;
		cssn = cevent->sndrcvinfo.sinfo_ssn;

		/* Have we gone too far?  */
		if (csid > sid)
			break;

		/* Have we not gone far enough?  */
		if (csid < sid)
			continue;

		if (cssn != sctp_ssn_peek(in, sid))
			break;

		/* Found it, so mark in the ssnmap. */
		sctp_ssn_next(in, sid);

		__skb_unlink(pos, pos->list);

		/* Attach all gathered skbs to the event.  */
		__skb_queue_tail(sctp_event2skb(event)->list, pos);
	}
}

/* Helper function to store chunks needing ordering.  */
static inline void sctp_ulpq_store_ordered(struct sctp_ulpq *ulpq,
					   struct sctp_ulpevent *event)
{
	struct sk_buff *pos;
	struct sctp_ulpevent *cevent;
	__u16 sid, csid;
	__u16 ssn, cssn;

	pos = skb_peek_tail(&ulpq->lobby);
	if (!pos) {
		__skb_queue_tail(&ulpq->lobby, sctp_event2skb(event));
		return;
	}

	sid = event->sndrcvinfo.sinfo_stream;
	ssn = event->sndrcvinfo.sinfo_ssn;
	
	cevent = (struct sctp_ulpevent *) pos->cb;
	csid = cevent->sndrcvinfo.sinfo_stream;
	cssn = cevent->sndrcvinfo.sinfo_ssn;
	if (sid > csid) {
		__skb_queue_tail(&ulpq->lobby, sctp_event2skb(event));
		return;
	}

	if ((sid == csid) && SSN_lt(cssn, ssn)) {
		__skb_queue_tail(&ulpq->lobby, sctp_event2skb(event));
		return;
	}

	/* Find the right place in this list.  We store them by
	 * stream ID and then by SSN.
	 */
	skb_queue_walk(&ulpq->lobby, pos) {
		cevent = (struct sctp_ulpevent *) pos->cb;
		csid = cevent->sndrcvinfo.sinfo_stream;
		cssn = cevent->sndrcvinfo.sinfo_ssn;

		if (csid > sid)
			break;
		if (csid == sid && SSN_lt(ssn, cssn))
			break;
	}


	/* Insert before pos. */
	__skb_insert(sctp_event2skb(event), pos->prev, pos, &ulpq->lobby);

}

static inline struct sctp_ulpevent *sctp_ulpq_order(struct sctp_ulpq *ulpq,
					struct sctp_ulpevent *event)
{
	__u16 sid, ssn;
	struct sctp_stream *in;

	/* Check if this message needs ordering.  */
	if (SCTP_DATA_UNORDERED & event->msg_flags)
		return event;

	/* Note: The stream ID must be verified before this routine.  */
	sid = event->sndrcvinfo.sinfo_stream;
	ssn = event->sndrcvinfo.sinfo_ssn;
	in  = &ulpq->asoc->ssnmap->in;

	/* Is this the expected SSN for this stream ID?  */
	if (ssn != sctp_ssn_peek(in, sid)) {
		/* We've received something out of order, so find where it
		 * needs to be placed.  We order by stream and then by SSN.
		 */
		sctp_ulpq_store_ordered(ulpq, event);
		return NULL;
	}

	/* Mark that the next chunk has been found.  */
	sctp_ssn_next(in, sid);

	/* Go find any other chunks that were waiting for
	 * ordering.
	 */
	sctp_ulpq_retrieve_ordered(ulpq, event);

	return event;
}

/* Renege 'needed' bytes from the ordering queue. */
static __u16 sctp_ulpq_renege_order(struct sctp_ulpq *ulpq, __u16 needed)
{
	__u16 freed = 0;
	__u32 tsn;
	struct sk_buff *skb;
	struct sctp_ulpevent *event;
	struct sctp_tsnmap *tsnmap;

	tsnmap = &ulpq->asoc->peer.tsn_map;

	while ((skb = __skb_dequeue_tail(&ulpq->lobby))) {
		freed += skb_headlen(skb);
		event = sctp_skb2event(skb);
		tsn = event->sndrcvinfo.sinfo_tsn;

		sctp_ulpevent_free(event);
		sctp_tsnmap_renege(tsnmap, tsn);
		if (freed >= needed)
			return freed;
	}

	return freed;
}

/* Renege 'needed' bytes from the reassembly queue. */
static __u16 sctp_ulpq_renege_frags(struct sctp_ulpq *ulpq, __u16 needed)
{
	__u16 freed = 0;
	__u32 tsn;
	struct sk_buff *skb;
	struct sctp_ulpevent *event;
	struct sctp_tsnmap *tsnmap;

	tsnmap = &ulpq->asoc->peer.tsn_map;

	/* Walk backwards through the list, reneges the newest tsns. */
	while ((skb = __skb_dequeue_tail(&ulpq->reasm))) {
		freed += skb_headlen(skb);
		event = sctp_skb2event(skb);
		tsn = event->sndrcvinfo.sinfo_tsn;

		sctp_ulpevent_free(event);
		sctp_tsnmap_renege(tsnmap, tsn);
		if (freed >= needed)
			return freed;
	}

	return freed;
}

/* Partial deliver the first message as there is pressure on rwnd. */
void sctp_ulpq_partial_delivery(struct sctp_ulpq *ulpq,
				struct sctp_chunk *chunk, int gfp)
{
	struct sctp_ulpevent *event;
	struct sctp_association *asoc;

	asoc = ulpq->asoc;

	/* Are we already in partial delivery mode?  */
	if (!sctp_sk(asoc->base.sk)->pd_mode) {

		/* Is partial delivery possible?  */
		event = sctp_ulpq_retrieve_first(ulpq);
		/* Send event to the ULP.   */
		if (event) {
			sctp_ulpq_tail_event(ulpq, event);
			sctp_sk(asoc->base.sk)->pd_mode = 1;
			ulpq->pd_mode = 1;
			return;
		}
	}
}

/* Renege some packets to make room for an incoming chunk.  */
void sctp_ulpq_renege(struct sctp_ulpq *ulpq, struct sctp_chunk *chunk,
		      int gfp)
{
	struct sctp_association *asoc;
	__u16 needed, freed;

	asoc = ulpq->asoc;

	if (chunk) {
		needed = ntohs(chunk->chunk_hdr->length);
		needed -= sizeof(sctp_data_chunk_t);
	} else 
		needed = SCTP_DEFAULT_MAXWINDOW;

	freed = 0;

	if (skb_queue_empty(&asoc->base.sk->sk_receive_queue)) {
		freed = sctp_ulpq_renege_order(ulpq, needed);
		if (freed < needed) {
			freed += sctp_ulpq_renege_frags(ulpq, needed - freed);
		}
	}
	/* If able to free enough room, accept this chunk. */
	if (chunk && (freed >= needed)) {
		__u32 tsn;
		tsn = ntohl(chunk->subh.data_hdr->tsn);
		sctp_tsnmap_mark(&asoc->peer.tsn_map, tsn);
		sctp_ulpq_tail_data(ulpq, chunk, gfp);
		
		sctp_ulpq_partial_delivery(ulpq, chunk, gfp);
	}

	return;
}



/* Notify the application if an association is aborted and in
 * partial delivery mode.  Send up any pending received messages.
 */
void sctp_ulpq_abort_pd(struct sctp_ulpq *ulpq, int gfp)
{
	struct sctp_ulpevent *ev = NULL;
	struct sock *sk;

	if (!ulpq->pd_mode)
		return;

	sk = ulpq->asoc->base.sk;
	if (sctp_ulpevent_type_enabled(SCTP_PARTIAL_DELIVERY_EVENT,
				       &sctp_sk(sk)->subscribe))
		ev = sctp_ulpevent_make_pdapi(ulpq->asoc,
					      SCTP_PARTIAL_DELIVERY_ABORTED,
					      gfp);
	if (ev)
		__skb_queue_tail(&sk->sk_receive_queue, sctp_event2skb(ev));

	/* If there is data waiting, send it up the socket now. */
	if (sctp_ulpq_clear_pd(ulpq) || ev)
		sk->sk_data_ready(sk, 0);
}
