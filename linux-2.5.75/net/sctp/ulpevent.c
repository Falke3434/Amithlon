/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * These functions manipulate an sctp event.   The struct ulpevent is used
 * to carry notifications and data to the ULP (sockets).
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
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/skbuff.h>
#include <net/sctp/structs.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

static void sctp_ulpevent_set_owner_r(struct sk_buff *skb,
				      struct sctp_association *asoc);
static void sctp_ulpevent_set_owner(struct sk_buff *skb,
				    const struct sctp_association *asoc);

/* Create a new sctp_ulpevent.  */
struct sctp_ulpevent *sctp_ulpevent_new(int size, int msg_flags, int gfp)
{
	struct sctp_ulpevent *event;
	struct sk_buff *skb;

	skb = alloc_skb(size, gfp);
	if (!skb)
		goto fail;

	event = sctp_skb2event(skb);
	event = sctp_ulpevent_init(event, msg_flags);
	if (!event)
		goto fail_init;
	return event;

fail_init:
	kfree_skb(skb);
fail:
	return NULL;
}

/* Initialize an ULP event from an given skb.  */
struct sctp_ulpevent *sctp_ulpevent_init(struct sctp_ulpevent *event,
					 int msg_flags)
{
	memset(event, sizeof(struct sctp_ulpevent), 0x00);
	event->msg_flags = msg_flags;
	return event;
}

/* Dispose of an event.  */
void sctp_ulpevent_free(struct sctp_ulpevent *event)
{
	kfree_skb(sctp_event2skb(event));
}

/* Is this a MSG_NOTIFICATION?  */
int sctp_ulpevent_is_notification(const struct sctp_ulpevent *event)
{
	return MSG_NOTIFICATION == (event->msg_flags & MSG_NOTIFICATION);
}

/* Create and initialize an SCTP_ASSOC_CHANGE event.
 *
 * 5.3.1.1 SCTP_ASSOC_CHANGE
 *
 * Communication notifications inform the ULP that an SCTP association
 * has either begun or ended. The identifier for a new association is
 * provided by this notification.
 *
 * Note: There is no field checking here.  If a field is unused it will be
 * zero'd out.
 */
struct sctp_ulpevent  *sctp_ulpevent_make_assoc_change(
	const struct sctp_association *asoc,
	__u16 flags, __u16 state, __u16 error, __u16 outbound,
	__u16 inbound, int gfp)
{
	struct sctp_ulpevent *event;
	struct sctp_assoc_change *sac;
	struct sk_buff *skb;

	event = sctp_ulpevent_new(sizeof(struct sctp_assoc_change),
				  MSG_NOTIFICATION, gfp);
	if (!event)
		goto fail;
	skb = sctp_event2skb(event);
	sac = (struct sctp_assoc_change *)
		skb_put(skb, sizeof(struct sctp_assoc_change));

	/* Socket Extensions for SCTP
	 * 5.3.1.1 SCTP_ASSOC_CHANGE
	 *
	 * sac_type:
	 * It should be SCTP_ASSOC_CHANGE.
	 */
	sac->sac_type = SCTP_ASSOC_CHANGE;

	/* Socket Extensions for SCTP
	 * 5.3.1.1 SCTP_ASSOC_CHANGE
	 *
	 * sac_state: 32 bits (signed integer)
	 * This field holds one of a number of values that communicate the
	 * event that happened to the association.
	 */
	sac->sac_state = state;

	/* Socket Extensions for SCTP
	 * 5.3.1.1 SCTP_ASSOC_CHANGE
	 *
	 * sac_flags: 16 bits (unsigned integer)
	 * Currently unused.
	 */
	sac->sac_flags = 0;

	/* Socket Extensions for SCTP
	 * 5.3.1.1 SCTP_ASSOC_CHANGE
	 *
	 * sac_length: sizeof (__u32)
	 * This field is the total length of the notification data, including
	 * the notification header.
	 */
	sac->sac_length = sizeof(struct sctp_assoc_change);

	/* Socket Extensions for SCTP
	 * 5.3.1.1 SCTP_ASSOC_CHANGE
	 *
	 * sac_error:  32 bits (signed integer)
	 *
	 * If the state was reached due to a error condition (e.g.
	 * COMMUNICATION_LOST) any relevant error information is available in
	 * this field. This corresponds to the protocol error codes defined in
	 * [SCTP].
	 */
	sac->sac_error = error;

	/* Socket Extensions for SCTP
	 * 5.3.1.1 SCTP_ASSOC_CHANGE
	 *
	 * sac_outbound_streams:  16 bits (unsigned integer)
	 * sac_inbound_streams:  16 bits (unsigned integer)
	 *
	 * The maximum number of streams allowed in each direction are
	 * available in sac_outbound_streams and sac_inbound streams.
	 */
	sac->sac_outbound_streams = outbound;
	sac->sac_inbound_streams = inbound;

	/* Socket Extensions for SCTP
	 * 5.3.1.1 SCTP_ASSOC_CHANGE
	 *
	 * sac_assoc_id: sizeof (sctp_assoc_t)
	 *
	 * The association id field, holds the identifier for the association.
	 * All notifications for a given association have the same association
	 * identifier.  For TCP style socket, this field is ignored.
	 */
	sctp_ulpevent_set_owner(skb, asoc);
	sac->sac_assoc_id = sctp_assoc2id(asoc);

	return event;

fail:
	return NULL;
}

/* Create and initialize an SCTP_PEER_ADDR_CHANGE event.
 *
 * Socket Extensions for SCTP - draft-01
 * 5.3.1.2 SCTP_PEER_ADDR_CHANGE
 *
 * When a destination address on a multi-homed peer encounters a change
 * an interface details event is sent.
 */
struct sctp_ulpevent *sctp_ulpevent_make_peer_addr_change(
	const struct sctp_association *asoc,
	const struct sockaddr_storage *aaddr,
	int flags, int state, int error, int gfp)
{
	struct sctp_ulpevent *event;
	struct sctp_paddr_change  *spc;
	struct sk_buff *skb;

	event = sctp_ulpevent_new(sizeof(struct sctp_paddr_change),
				  MSG_NOTIFICATION, gfp);
	if (!event)
		goto fail;

	skb = sctp_event2skb(event);
	spc = (struct sctp_paddr_change *)
		skb_put(skb, sizeof(struct sctp_paddr_change));

	/* Sockets API Extensions for SCTP
	 * Section 5.3.1.2 SCTP_PEER_ADDR_CHANGE
	 *
	 * spc_type:
	 *
	 *    It should be SCTP_PEER_ADDR_CHANGE.
	 */
	spc->spc_type = SCTP_PEER_ADDR_CHANGE;

	/* Sockets API Extensions for SCTP
	 * Section 5.3.1.2 SCTP_PEER_ADDR_CHANGE
	 *
	 * spc_length: sizeof (__u32)
	 *
	 * This field is the total length of the notification data, including
	 * the notification header.
	 */
	spc->spc_length = sizeof(struct sctp_paddr_change);

	/* Sockets API Extensions for SCTP
	 * Section 5.3.1.2 SCTP_PEER_ADDR_CHANGE
	 *
	 * spc_flags: 16 bits (unsigned integer)
	 * Currently unused.
	 */
	spc->spc_flags = 0;

	/* Sockets API Extensions for SCTP
	 * Section 5.3.1.2 SCTP_PEER_ADDR_CHANGE
	 *
	 * spc_state:  32 bits (signed integer)
	 *
	 * This field holds one of a number of values that communicate the
	 * event that happened to the address.
	 */
	spc->spc_state = state;

	/* Sockets API Extensions for SCTP
	 * Section 5.3.1.2 SCTP_PEER_ADDR_CHANGE
	 *
	 * spc_error:  32 bits (signed integer)
	 *
	 * If the state was reached due to any error condition (e.g.
	 * ADDRESS_UNREACHABLE) any relevant error information is available in
	 * this field.
	 */
	spc->spc_error = error;

	/* Socket Extensions for SCTP
	 * 5.3.1.1 SCTP_ASSOC_CHANGE
	 *
	 * spc_assoc_id: sizeof (sctp_assoc_t)
	 *
	 * The association id field, holds the identifier for the association.
	 * All notifications for a given association have the same association
	 * identifier.  For TCP style socket, this field is ignored.
	 */
	sctp_ulpevent_set_owner(skb, asoc);
	spc->spc_assoc_id = sctp_assoc2id(asoc);

	/* Sockets API Extensions for SCTP
	 * Section 5.3.1.2 SCTP_PEER_ADDR_CHANGE
	 *
	 * spc_aaddr: sizeof (struct sockaddr_storage)
	 *
	 * The affected address field, holds the remote peer's address that is
	 * encountering the change of state.
	 */
	memcpy(&spc->spc_aaddr, aaddr, sizeof(struct sockaddr_storage));

	return event;

fail:
	return NULL;
}

/* Create and initialize an SCTP_REMOTE_ERROR notification.
 *
 * Note: This assumes that the chunk->skb->data already points to the
 * operation error payload.
 *
 * Socket Extensions for SCTP - draft-01
 * 5.3.1.3 SCTP_REMOTE_ERROR
 *
 * A remote peer may send an Operational Error message to its peer.
 * This message indicates a variety of error conditions on an
 * association. The entire error TLV as it appears on the wire is
 * included in a SCTP_REMOTE_ERROR event.  Please refer to the SCTP
 * specification [SCTP] and any extensions for a list of possible
 * error formats.
 */
struct sctp_ulpevent *sctp_ulpevent_make_remote_error(
	const struct sctp_association *asoc, struct sctp_chunk *chunk,
	__u16 flags, int gfp)
{
	struct sctp_ulpevent *event;
	struct sctp_remote_error *sre;
	struct sk_buff *skb;
	sctp_errhdr_t *ch;
	__u16 cause;
	int elen;

	ch = (sctp_errhdr_t *)(chunk->skb->data);
	cause = ch->cause;
	elen = WORD_ROUND(ntohs(ch->length)) - sizeof(sctp_errhdr_t);

	/* Pull off the ERROR header.  */
	skb_pull(chunk->skb, sizeof(sctp_errhdr_t));

	/* Copy the skb to a new skb with room for us to prepend
	 * notification with.
	 */
	skb = skb_copy_expand(chunk->skb, sizeof(struct sctp_remote_error), 
			      0, gfp);

	/* Pull off the rest of the cause TLV from the chunk.  */
	skb_pull(chunk->skb, elen);
	if (!skb)
		goto fail;

	/* Embed the event fields inside the cloned skb.  */
	event = sctp_skb2event(skb);
	event = sctp_ulpevent_init(event, MSG_NOTIFICATION);

	if (!event)
		goto fail;

	sre = (struct sctp_remote_error *)
		skb_push(skb, sizeof(struct sctp_remote_error));

	/* Trim the buffer to the right length.  */
	skb_trim(skb, sizeof(struct sctp_remote_error) + elen);

	/* Socket Extensions for SCTP
	 * 5.3.1.3 SCTP_REMOTE_ERROR
	 *
	 * sre_type:
	 *   It should be SCTP_REMOTE_ERROR.
	 */
	sre->sre_type = SCTP_REMOTE_ERROR;

	/*
	 * Socket Extensions for SCTP
	 * 5.3.1.3 SCTP_REMOTE_ERROR
	 *
	 * sre_flags: 16 bits (unsigned integer)
	 *   Currently unused.
	 */
	sre->sre_flags = 0;

	/* Socket Extensions for SCTP
	 * 5.3.1.3 SCTP_REMOTE_ERROR
	 *
	 * sre_length: sizeof (__u32)
	 *
	 * This field is the total length of the notification data,
	 * including the notification header.
	 */
	sre->sre_length = skb->len;

	/* Socket Extensions for SCTP
	 * 5.3.1.3 SCTP_REMOTE_ERROR
	 *
	 * sre_error: 16 bits (unsigned integer)
	 * This value represents one of the Operational Error causes defined in
	 * the SCTP specification, in network byte order.
	 */
	sre->sre_error = cause;

	/* Socket Extensions for SCTP
	 * 5.3.1.3 SCTP_REMOTE_ERROR
	 *
	 * sre_assoc_id: sizeof (sctp_assoc_t)
	 *
	 * The association id field, holds the identifier for the association.
	 * All notifications for a given association have the same association
	 * identifier.  For TCP style socket, this field is ignored.
	 */
	skb = sctp_event2skb(event);
	sctp_ulpevent_set_owner(skb, asoc);
	sre->sre_assoc_id = sctp_assoc2id(asoc);

	return event;

fail:
	return NULL;
}

/* Create and initialize a SCTP_SEND_FAILED notification.
 *
 * Socket Extensions for SCTP - draft-01
 * 5.3.1.4 SCTP_SEND_FAILED
 */
struct sctp_ulpevent *sctp_ulpevent_make_send_failed(
	const struct sctp_association *asoc, struct sctp_chunk *chunk,
	__u16 flags, __u32 error, int gfp)
{
	struct sctp_ulpevent *event;
	struct sctp_send_failed *ssf;
	struct sk_buff *skb;

	/* Pull off any padding. */
	int len = ntohs(chunk->chunk_hdr->length);

	/* Make skb with more room so we can prepend notification.  */
	skb = skb_copy_expand(chunk->skb,
			      sizeof(struct sctp_send_failed), /* headroom */
			      0,                               /* tailroom */
			      gfp);
	if (!skb)
		goto fail;

	/* Pull off the common chunk header and DATA header.  */
	skb_pull(skb, sizeof(struct sctp_data_chunk));
	len -= sizeof(struct sctp_data_chunk);

	/* Embed the event fields inside the cloned skb.  */
	event = sctp_skb2event(skb);
	event = sctp_ulpevent_init(event, MSG_NOTIFICATION);
	if (!event)
		goto fail;

	ssf = (struct sctp_send_failed *)
		skb_push(skb, sizeof(struct sctp_send_failed));

	/* Socket Extensions for SCTP
	 * 5.3.1.4 SCTP_SEND_FAILED
	 *
	 * ssf_type:
	 * It should be SCTP_SEND_FAILED.
	 */
	ssf->ssf_type = SCTP_SEND_FAILED;

	/* Socket Extensions for SCTP
	 * 5.3.1.4 SCTP_SEND_FAILED
	 *
	 * ssf_flags: 16 bits (unsigned integer)
	 * The flag value will take one of the following values
	 *
	 * SCTP_DATA_UNSENT - Indicates that the data was never put on
	 *                    the wire.
	 *
	 * SCTP_DATA_SENT   - Indicates that the data was put on the wire.
	 *                    Note that this does not necessarily mean that the
	 *                    data was (or was not) successfully delivered.
	 */
	ssf->ssf_flags = flags;

	/* Socket Extensions for SCTP
	 * 5.3.1.4 SCTP_SEND_FAILED
	 *
	 * ssf_length: sizeof (__u32)
	 * This field is the total length of the notification data, including
	 * the notification header.
	 */
	ssf->ssf_length = sizeof(struct sctp_send_failed) + len;
	skb_trim(skb, ssf->ssf_length);

	/* Socket Extensions for SCTP
	 * 5.3.1.4 SCTP_SEND_FAILED
	 *
	 * ssf_error: 16 bits (unsigned integer)
	 * This value represents the reason why the send failed, and if set,
	 * will be a SCTP protocol error code as defined in [SCTP] section
	 * 3.3.10.
	 */
	ssf->ssf_error = error;

	/* Socket Extensions for SCTP
	 * 5.3.1.4 SCTP_SEND_FAILED
	 *
	 * ssf_info: sizeof (struct sctp_sndrcvinfo)
	 * The original send information associated with the undelivered
	 * message.
	 */
	memcpy(&ssf->ssf_info, &chunk->sinfo, sizeof(struct sctp_sndrcvinfo));

	/* Per TSVWG discussion with Randy. Allow the application to
	 * ressemble a fragmented message. 
	 */
	ssf->ssf_info.sinfo_flags = chunk->chunk_hdr->flags;

	/* Socket Extensions for SCTP
	 * 5.3.1.4 SCTP_SEND_FAILED
	 *
	 * ssf_assoc_id: sizeof (sctp_assoc_t)
	 * The association id field, sf_assoc_id, holds the identifier for the
	 * association.  All notifications for a given association have the
	 * same association identifier.  For TCP style socket, this field is
	 * ignored.
	 */
	skb = sctp_event2skb(event);
	sctp_ulpevent_set_owner(skb, asoc);
	ssf->ssf_assoc_id = sctp_assoc2id(asoc);
	return event;

fail:
	return NULL;
}

/* Create and initialize a SCTP_SHUTDOWN_EVENT notification.
 *
 * Socket Extensions for SCTP - draft-01
 * 5.3.1.5 SCTP_SHUTDOWN_EVENT
 */
struct sctp_ulpevent *sctp_ulpevent_make_shutdown_event(
	const struct sctp_association *asoc,
	__u16 flags, int gfp)
{
	struct sctp_ulpevent *event;
	struct sctp_shutdown_event *sse;
	struct sk_buff *skb;

	event = sctp_ulpevent_new(sizeof(struct sctp_assoc_change),
				  MSG_NOTIFICATION, gfp);
	if (!event)
		goto fail;

	skb = sctp_event2skb(event);
	sse = (struct sctp_shutdown_event *)
		skb_put(skb, sizeof(struct sctp_shutdown_event));

	/* Socket Extensions for SCTP
	 * 5.3.1.5 SCTP_SHUTDOWN_EVENT
	 *
	 * sse_type
	 * It should be SCTP_SHUTDOWN_EVENT
	 */
	sse->sse_type = SCTP_SHUTDOWN_EVENT;

	/* Socket Extensions for SCTP
	 * 5.3.1.5 SCTP_SHUTDOWN_EVENT
	 *
	 * sse_flags: 16 bits (unsigned integer)
	 * Currently unused.
	 */
	sse->sse_flags = 0;

	/* Socket Extensions for SCTP
	 * 5.3.1.5 SCTP_SHUTDOWN_EVENT
	 *
	 * sse_length: sizeof (__u32)
	 * This field is the total length of the notification data, including
	 * the notification header.
	 */
	sse->sse_length = sizeof(struct sctp_shutdown_event);

	/* Socket Extensions for SCTP
	 * 5.3.1.5 SCTP_SHUTDOWN_EVENT
	 *
	 * sse_assoc_id: sizeof (sctp_assoc_t)
	 * The association id field, holds the identifier for the association.
	 * All notifications for a given association have the same association
	 * identifier.  For TCP style socket, this field is ignored.
	 */
	sctp_ulpevent_set_owner(skb, asoc);
	sse->sse_assoc_id = sctp_assoc2id(asoc);

	return event;

fail:
	return NULL;
}

/* A message has been received.  Package this message as a notification
 * to pass it to the upper layers.  Go ahead and calculate the sndrcvinfo
 * even if filtered out later.
 *
 * Socket Extensions for SCTP
 * 5.2.2 SCTP Header Information Structure (SCTP_SNDRCV)
 */
struct sctp_ulpevent *sctp_ulpevent_make_rcvmsg(struct sctp_association *asoc,
						struct sctp_chunk *chunk, 
						int gfp)
{
	struct sctp_ulpevent *event;
	struct sctp_sndrcvinfo *info;
	struct sk_buff *skb, *list;
	size_t padding, len;

	/* Clone the original skb, sharing the data.  */
	skb = skb_clone(chunk->skb, gfp);
	if (!skb)
		goto fail;

	/* First calculate the padding, so we don't inadvertently
	 * pass up the wrong length to the user.
	 *
	 * RFC 2960 - Section 3.2  Chunk Field Descriptions
	 *
	 * The total length of a chunk(including Type, Length and Value fields)
	 * MUST be a multiple of 4 bytes.  If the length of the chunk is not a
	 * multiple of 4 bytes, the sender MUST pad the chunk with all zero
	 * bytes and this padding is not included in the chunk length field.
	 * The sender should never pad with more than 3 bytes.  The receiver
	 * MUST ignore the padding bytes.
	 */
	len = ntohs(chunk->chunk_hdr->length);
	padding = WORD_ROUND(len) - len;

	/* Fixup cloned skb with just this chunks data.  */
	skb_trim(skb, chunk->chunk_end - padding - skb->data);

	/* Set up a destructor to do rwnd accounting.  */
	sctp_ulpevent_set_owner_r(skb, asoc);

	/* Embed the event fields inside the cloned skb.  */
	event = sctp_skb2event(skb);

	/* Initialize event with flags 0.  */
	event = sctp_ulpevent_init(event, 0);
	if (!event)
		goto fail_init;

	event->iif = sctp_chunk_iif(chunk);
	/* Note:  Not clearing the entire event struct as
	 * this is just a fragment of the real event.  However,
	 * we still need to do rwnd accounting.
	 */
	for (list = skb_shinfo(skb)->frag_list; list; list = list->next)
		sctp_ulpevent_set_owner_r(list, asoc);

	info = (struct sctp_sndrcvinfo *) &event->sndrcvinfo;

	/* Sockets API Extensions for SCTP
	 * Section 5.2.2 SCTP Header Information Structure (SCTP_SNDRCV)
	 *
	 * sinfo_stream: 16 bits (unsigned integer)
	 *
	 * For recvmsg() the SCTP stack places the message's stream number in
	 * this value.
	 */
	info->sinfo_stream = ntohs(chunk->subh.data_hdr->stream);

	/* Sockets API Extensions for SCTP
	 * Section 5.2.2 SCTP Header Information Structure (SCTP_SNDRCV)
	 *
	 * sinfo_ssn: 16 bits (unsigned integer)
	 *
	 * For recvmsg() this value contains the stream sequence number that
	 * the remote endpoint placed in the DATA chunk.  For fragmented
	 * messages this is the same number for all deliveries of the message
	 * (if more than one recvmsg() is needed to read the message).
	 */
	info->sinfo_ssn = ntohs(chunk->subh.data_hdr->ssn);

        /* Sockets API Extensions for SCTP
	 * Section 5.2.2 SCTP Header Information Structure (SCTP_SNDRCV)
	 *
	 * sinfo_ppid: 32 bits (unsigned integer)
	 *
	 * In recvmsg() this value is
	 * the same information that was passed by the upper layer in the peer
	 * application.  Please note that byte order issues are NOT accounted
	 * for and this information is passed opaquely by the SCTP stack from
	 * one end to the other.
	 */
	info->sinfo_ppid = ntohl(chunk->subh.data_hdr->ppid);

	/* Sockets API Extensions for SCTP
	 * Section 5.2.2 SCTP Header Information Structure (SCTP_SNDRCV)
	 *
	 * sinfo_flags: 16 bits (unsigned integer)
	 *
	 * This field may contain any of the following flags and is composed of
	 * a bitwise OR of these values.
	 *
	 * recvmsg() flags:
	 *
	 * MSG_UNORDERED - This flag is present when the message was sent
	 *                 non-ordered.
	 */
	if (chunk->chunk_hdr->flags & SCTP_DATA_UNORDERED) {
		info->sinfo_flags |= MSG_UNORDERED;

		/* sinfo_cumtsn: 32 bit (unsigned integer)
		 *
		 * This field will hold the current cumulative TSN as
		 * known by the underlying SCTP layer.  Note this field is
		 * ignored when sending and only valid for a receive
		 * operation when sinfo_flags are set to MSG_UNORDERED.
		 */
		info->sinfo_cumtsn = sctp_tsnmap_get_ctsn(&asoc->peer.tsn_map);
	}

	/* Note:  For reassembly, we need to have the fragmentation bits.
	 * For now, merge these into the msg_flags, since those bit
	 * possitions are not used.
	 */
	event->msg_flags |= chunk->chunk_hdr->flags;

	/* With 04 draft, tsn moves into sndrcvinfo. */
	info->sinfo_tsn = ntohl(chunk->subh.data_hdr->tsn);

	/* Context is not used on receive. */
	info->sinfo_context = 0;

	/* Sockets API Extensions for SCTP
	 * Section 5.2.2 SCTP Header Information Structure (SCTP_SNDRCV)
	 *
	 * sinfo_assoc_id: sizeof (sctp_assoc_t)
	 *
	 * The association handle field, sinfo_assoc_id, holds the identifier
	 * for the association announced in the COMMUNICATION_UP notification.
	 * All notifications for a given association have the same identifier.
	 * Ignored for TCP-style sockets.
	 */
	info->sinfo_assoc_id = sctp_assoc2id(asoc);

	return event;

fail_init:
	kfree_skb(skb);

fail:
	return NULL;
}

/* Create a partial delivery related event.
 *
 * 5.3.1.7 SCTP_PARTIAL_DELIVERY_EVENT
 *
 *   When a reciever is engaged in a partial delivery of a
 *   message this notification will be used to inidicate
 *   various events.
 */
struct sctp_ulpevent *sctp_ulpevent_make_pdapi(
	const struct sctp_association *asoc, __u32 indication, int gfp)
{
	struct sctp_ulpevent *event;
	struct sctp_rcv_pdapi_event *pd;
	struct sk_buff *skb;

	event = sctp_ulpevent_new(sizeof(struct sctp_assoc_change),
				  MSG_NOTIFICATION, gfp);
	if (!event)
		goto fail;

	skb = sctp_event2skb(event);
	pd = (struct sctp_rcv_pdapi_event *)
		skb_put(skb, sizeof(struct sctp_rcv_pdapi_event));

	/* pdapi_type
	 *   It should be SCTP_PARTIAL_DELIVERY_EVENT
	 *
	 * pdapi_flags: 16 bits (unsigned integer)
	 *   Currently unused.
	 */
	pd->pdapi_type = SCTP_PARTIAL_DELIVERY_EVENT;
	pd->pdapi_flags = 0;

	/* pdapi_length: 32 bits (unsigned integer)
	 *
	 * This field is the total length of the notification data, including
	 * the notification header.  It will generally be sizeof (struct
	 * sctp_rcv_pdapi_event).
	 */
	pd->pdapi_length = sizeof(struct sctp_rcv_pdapi_event);

        /*  pdapi_indication: 32 bits (unsigned integer)
	 *
	 * This field holds the indication being sent to the application.
	 */
	pd->pdapi_indication = indication;

	/*  pdapi_assoc_id: sizeof (sctp_assoc_t)
	 *
	 * The association id field, holds the identifier for the association.
	 */
	pd->pdapi_assoc_id = sctp_assoc2id(asoc);

	return event;
fail:
	return NULL;
}

/* Return the notification type, assuming this is a notification
 * event.
 */
__u16 sctp_ulpevent_get_notification_type(const struct sctp_ulpevent *event)
{
	union sctp_notification *notification;
	struct sk_buff *skb;

	skb = sctp_event2skb((struct sctp_ulpevent *)event);
	notification = (union sctp_notification *) skb->data;
	return notification->h.sn_type;
}

/* Copy out the sndrcvinfo into a msghdr.  */
void sctp_ulpevent_read_sndrcvinfo(const struct sctp_ulpevent *event,
				   struct msghdr *msghdr)
{
	if (!sctp_ulpevent_is_notification(event)) {
		put_cmsg(msghdr, IPPROTO_SCTP, SCTP_SNDRCV,
			 sizeof(struct sctp_sndrcvinfo),
			 (void *) &event->sndrcvinfo);
	}
}

/* Do accounting for bytes just read by user.  */
static void sctp_rcvmsg_rfree(struct sk_buff *skb)
{
	struct sctp_association *asoc;
	struct sctp_ulpevent *event;

	/* Current stack structures assume that the rcv buffer is
	 * per socket.   For UDP style sockets this is not true as
	 * multiple associations may be on a single UDP-style socket.
	 * Use the local private area of the skb to track the owning
	 * association.
	 */
	event = sctp_skb2event(skb);
	asoc = event->asoc;
	sctp_assoc_rwnd_increase(asoc, skb_headlen(skb));
	sctp_association_put(asoc);
}

/* Charge receive window for bytes received.  */
static void sctp_ulpevent_set_owner_r(struct sk_buff *skb,
				      struct sctp_association *asoc)
{
	struct sctp_ulpevent *event;

	/* The current stack structures assume that the rcv buffer is
	 * per socket.  For UDP-style sockets this is not true as
	 * multiple associations may be on a single UDP-style socket.
	 * We use the local private area of the skb to track the owning
	 * association.
	 */
	sctp_association_hold(asoc);
	skb->sk = asoc->base.sk;
	event = sctp_skb2event(skb);
	event->asoc = asoc;

	skb->destructor = sctp_rcvmsg_rfree;

	sctp_assoc_rwnd_decrease(asoc, skb_headlen(skb));
}

/* A simple destructor to give up the reference to the association. */
static void sctp_ulpevent_rfree(struct sk_buff *skb)
{
	struct sctp_ulpevent *event;

	event = sctp_skb2event(skb);
	sctp_association_put(event->asoc);
}

/* Hold the association in case the msg_name needs read out of
 * the association.
 */
static void sctp_ulpevent_set_owner(struct sk_buff *skb,
				    const struct sctp_association *asoc)
{
	struct sctp_ulpevent *event;

	/* Cast away the const, as we are just wanting to
	 * bump the reference count.
	 */
	sctp_association_hold((struct sctp_association *)asoc);
	skb->sk = asoc->base.sk;
	event = sctp_skb2event(skb);
	event->asoc = (struct sctp_association *)asoc;
	skb->destructor = sctp_ulpevent_rfree;
}
