#ifndef LLC_CONN_H
#define LLC_CONN_H
/*
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001, 2002 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/timer.h>
#include <net/llc_if.h>
#include <linux/llc.h>

struct llc_timer {
	struct timer_list timer;
	u16		  expire;	/* timer expire time */
};

struct llc_opt {
	struct sock	    *sk;		/* sock that has this llc_opt */
	struct sockaddr_llc addr;		/* address sock is bound to */
	u8		    state;		/* state of connection */
	struct llc_sap	    *sap;		/* pointer to parent SAP */
	struct llc_addr	    laddr;		/* lsap/mac pair */
	struct llc_addr	    daddr;		/* dsap/mac pair */
	struct net_device   *dev;		/* device to send to remote */
	u8		    retry_count;	/* number of retries */
	u8		    ack_must_be_send;
	u8		    first_pdu_Ns;
	u8		    npta;
	struct llc_timer    ack_timer;
	struct llc_timer    pf_cycle_timer;
	struct llc_timer    rej_sent_timer;
	struct llc_timer    busy_state_timer;	/* ind busy clr at remote LLC */
	u8		    vS;			/* seq# next in-seq I-PDU tx'd*/
	u8		    vR;			/* seq# next in-seq I-PDU rx'd*/
	u32		    n2;			/* max nbr re-tx's for timeout*/
	u32		    n1;			/* max nbr octets in I PDU */
	u8		    k;			/* tx window size; max = 127 */
	u8		    rw;			/* rx window size; max = 127 */
	u8		    p_flag;		/* state flags */
	u8		    f_flag;
	u8		    s_flag;
	u8		    data_flag;
	u8		    remote_busy_flag;
	u8		    cause_flag;
	struct sk_buff_head pdu_unack_q;	/* PUDs sent/waiting ack */
	u16		    link;		/* network layer link number */
	u8		    X;			/* a temporary variable */
	u8		    ack_pf;		/* this flag indicates what is
						   the P-bit of acknowledge */
	u8		    failed_data_req; /* recognize that already exist a
						failed llc_data_req_handler
						(tx_buffer_full or unacceptable
						state */
	u8		    dec_step;
	u8		    inc_cntr;
	u8		    dec_cntr;
	u8		    connect_step;
	u8		    last_nr;	   /* NR of last pdu received */
	u32		    rx_pdu_hdr;	   /* used for saving header of last pdu
					      received and caused sending FRMR.
					      Used for resending FRMR */
};

#define llc_sk(__sk) ((struct llc_opt *)(__sk)->sk_protinfo)

extern struct sock *llc_sk_alloc(int family, int priority);
extern void llc_sk_free(struct sock *sk);

extern void llc_sk_reset(struct sock *sk);
extern int llc_sk_init(struct sock *sk);

/* Access to a connection */
extern int llc_conn_state_process(struct sock *sk, struct sk_buff *skb);
extern void llc_conn_send_pdu(struct sock *sk, struct sk_buff *skb);
extern void llc_conn_rtn_pdu(struct sock *sk, struct sk_buff *skb);
extern void llc_conn_resend_i_pdu_as_cmd(struct sock *sk, u8 nr,
					 u8 first_p_bit);
extern void llc_conn_resend_i_pdu_as_rsp(struct sock *sk, u8 nr,
					 u8 first_f_bit);
extern int llc_conn_remove_acked_pdus(struct sock *conn, u8 nr,
				      u16 *how_many_unacked);
extern struct sock *llc_lookup_established(struct llc_sap *sap,
					   struct llc_addr *daddr,
					   struct llc_addr *laddr);
extern struct sock *llc_lookup_listener(struct llc_sap *sap,
					struct llc_addr *laddr);
extern struct sock *llc_lookup_dgram(struct llc_sap *sap,
				     struct llc_addr *laddr);
extern void llc_save_primitive(struct sk_buff* skb, u8 prim);
extern u8 llc_data_accept_state(u8 state);
extern void llc_build_offset_table(void);
#endif /* LLC_CONN_H */
