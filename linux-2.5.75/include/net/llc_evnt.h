#ifndef LLC_EVNT_H
#define LLC_EVNT_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
/* Station component state transition events */
/* Types of events (possible values in 'ev->type') */
#define LLC_STATION_EV_TYPE_SIMPLE	1
#define LLC_STATION_EV_TYPE_CONDITION	2
#define LLC_STATION_EV_TYPE_PRIM	3
#define LLC_STATION_EV_TYPE_PDU		4       /* command/response PDU */
#define LLC_STATION_EV_TYPE_ACK_TMR	5
#define LLC_STATION_EV_TYPE_RPT_STATUS	6

/* Events */
#define LLC_STATION_EV_ENABLE_WITH_DUP_ADDR_CHECK		1
#define LLC_STATION_EV_ENABLE_WITHOUT_DUP_ADDR_CHECK		2
#define LLC_STATION_EV_ACK_TMR_EXP_LT_RETRY_CNT_MAX_RETRY	3
#define LLC_STATION_EV_ACK_TMR_EXP_EQ_RETRY_CNT_MAX_RETRY	4
#define LLC_STATION_EV_RX_NULL_DSAP_XID_C			5
#define LLC_STATION_EV_RX_NULL_DSAP_0_XID_R_XID_R_CNT_EQ	6
#define LLC_STATION_EV_RX_NULL_DSAP_1_XID_R_XID_R_CNT_EQ	7
#define LLC_STATION_EV_RX_NULL_DSAP_TEST_C			8
#define LLC_STATION_EV_DISABLE_REQ				9

struct llc_station_state_ev {
	u8		 type;
	u8		 prim;
	u8		 prim_type;
	u8		 reason;
	struct list_head node; /* node in station->ev_q.list */
};

static __inline__ struct llc_station_state_ev *
					llc_station_ev(struct sk_buff *skb)
{
	return (struct llc_station_state_ev *)skb->cb;
}

typedef int (*llc_station_ev_t)(struct llc_station *station,
				struct sk_buff *skb);

extern int llc_stat_ev_enable_with_dup_addr_check(struct llc_station *station,
						  struct sk_buff *skb);
extern int llc_stat_ev_enable_without_dup_addr_check(struct llc_station *station,
						     struct sk_buff *skb);
extern int llc_stat_ev_ack_tmr_exp_lt_retry_cnt_max_retry(struct llc_station *
									station,
							  struct sk_buff *skb);
extern int llc_stat_ev_ack_tmr_exp_eq_retry_cnt_max_retry(struct llc_station *station,
							  struct sk_buff *skb);
extern int llc_stat_ev_rx_null_dsap_xid_c(struct llc_station *station,
					  struct sk_buff *skb);
extern int llc_stat_ev_rx_null_dsap_0_xid_r_xid_r_cnt_eq(struct llc_station *station,
							 struct sk_buff *skb);
extern int llc_stat_ev_rx_null_dsap_1_xid_r_xid_r_cnt_eq(struct llc_station *station,
							 struct sk_buff *skb);
extern int llc_stat_ev_rx_null_dsap_test_c(struct llc_station *station,
					   struct sk_buff *skb);
extern int llc_stat_ev_disable_req(struct llc_station *station,
				   struct sk_buff *skb);
#endif /* LLC_EVNT_H */
