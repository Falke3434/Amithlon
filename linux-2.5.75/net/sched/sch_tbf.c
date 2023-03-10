/*
 * net/sched/sch_tbf.c	Token Bucket Filter queue.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *		Dmitry Torokhov <dtor@mail.ru> - allow attaching inner qdiscs -
 *						 original idea by Martin Devera
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/pkt_sched.h>


/*	Simple Token Bucket Filter.
	=======================================

	SOURCE.
	-------

	None.

	Description.
	------------

	A data flow obeys TBF with rate R and depth B, if for any
	time interval t_i...t_f the number of transmitted bits
	does not exceed B + R*(t_f-t_i).

	Packetized version of this definition:
	The sequence of packets of sizes s_i served at moments t_i
	obeys TBF, if for any i<=k:

	s_i+....+s_k <= B + R*(t_k - t_i)

	Algorithm.
	----------
	
	Let N(t_i) be B/R initially and N(t) grow continuously with time as:

	N(t+delta) = min{B/R, N(t) + delta}

	If the first packet in queue has length S, it may be
	transmitted only at the time t_* when S/R <= N(t_*),
	and in this case N(t) jumps:

	N(t_* + 0) = N(t_* - 0) - S/R.



	Actually, QoS requires two TBF to be applied to a data stream.
	One of them controls steady state burst size, another
	one with rate P (peak rate) and depth M (equal to link MTU)
	limits bursts at a smaller time scale.

	It is easy to see that P>R, and B>M. If P is infinity, this double
	TBF is equivalent to a single one.

	When TBF works in reshaping mode, latency is estimated as:

	lat = max ((L-B)/R, (L-M)/P)


	NOTES.
	------

	If TBF throttles, it starts a watchdog timer, which will wake it up
	when it is ready to transmit.
	Note that the minimal timer resolution is 1/HZ.
	If no new packets arrive during this period,
	or if the device is not awaken by EOI for some previous packet,
	TBF can stop its activity for 1/HZ.


	This means, that with depth B, the maximal rate is

	R_crit = B*HZ

	F.e. for 10Mbit ethernet and HZ=100 the minimal allowed B is ~10Kbytes.

	Note that the peak rate TBF is much more tough: with MTU 1500
	P_crit = 150Kbytes/sec. So, if you need greater peak
	rates, use alpha with HZ=1000 :-)
*/

struct tbf_sched_data
{
/* Parameters */
	u32		limit;		/* Maximal length of backlog: bytes */
	u32		buffer;		/* Token bucket depth/rate: MUST BE >= MTU/B */
	u32		mtu;
	u32		max_size;
	struct qdisc_rate_table	*R_tab;
	struct qdisc_rate_table	*P_tab;

/* Variables */
	long	tokens;			/* Current number of B tokens */
	long	ptokens;		/* Current number of P tokens */
	psched_time_t	t_c;		/* Time check-point */
	struct timer_list wd_timer;	/* Watchdog timer */
	struct Qdisc	*qdisc;		/* Inner qdisc, default - bfifo queue */
};

#define L2T(q,L)   ((q)->R_tab->data[(L)>>(q)->R_tab->rate.cell_log])
#define L2T_P(q,L) ((q)->P_tab->data[(L)>>(q)->P_tab->rate.cell_log])

static int tbf_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	int ret;

	if (skb->len > q->max_size || sch->stats.backlog + skb->len > q->limit) {
		sch->stats.drops++;
#ifdef CONFIG_NET_CLS_POLICE
		if (sch->reshape_fail == NULL || sch->reshape_fail(skb, sch))
#endif
			kfree_skb(skb);
	
		return NET_XMIT_DROP;
	}
	
	if ((ret = q->qdisc->enqueue(skb, q->qdisc)) != 0) {
		sch->stats.drops++;
		return ret;
	}	
	
	sch->q.qlen++;
	sch->stats.backlog += skb->len;
	sch->stats.bytes += skb->len;
	sch->stats.packets++;
	return 0;
}

static int tbf_requeue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	int ret;
	
	if ((ret = q->qdisc->ops->requeue(skb, q->qdisc)) == 0) {
		sch->q.qlen++; 
		sch->stats.backlog += skb->len;
	}
	
	return ret;
}

static unsigned int tbf_drop(struct Qdisc* sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	unsigned int len;
	
	if ((len = q->qdisc->ops->drop(q->qdisc)) != 0) {
		sch->q.qlen--;
		sch->stats.backlog -= len;
		sch->stats.drops++;
	}
	return len;
}

static void tbf_watchdog(unsigned long arg)
{
	struct Qdisc *sch = (struct Qdisc*)arg;

	sch->flags &= ~TCQ_F_THROTTLED;
	netif_schedule(sch->dev);
}

static struct sk_buff *tbf_dequeue(struct Qdisc* sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	struct sk_buff *skb;
	
	skb = q->qdisc->dequeue(q->qdisc);

	if (skb) {
		psched_time_t now;
		long toks;
		long ptoks = 0;
		unsigned int len = skb->len;
		
		PSCHED_GET_TIME(now);

		toks = PSCHED_TDIFF_SAFE(now, q->t_c, q->buffer, 0);

		if (q->P_tab) {
			ptoks = toks + q->ptokens;
			if (ptoks > (long)q->mtu)
				ptoks = q->mtu;
			ptoks -= L2T_P(q, len);
		}
		toks += q->tokens;
		if (toks > (long)q->buffer)
			toks = q->buffer;
		toks -= L2T(q, len);

		if ((toks|ptoks) >= 0) {
			q->t_c = now;
			q->tokens = toks;
			q->ptokens = ptoks;
			sch->stats.backlog -= len;
			sch->q.qlen--;
			sch->flags &= ~TCQ_F_THROTTLED;
			return skb;
		}

		if (!netif_queue_stopped(sch->dev)) {
			long delay = PSCHED_US2JIFFIE(max_t(long, -toks, -ptoks));

			if (delay == 0)
				delay = 1;

			mod_timer(&q->wd_timer, jiffies+delay);
		}

		/* Maybe we have a shorter packet in the queue,
		   which can be sent now. It sounds cool,
		   but, however, this is wrong in principle.
		   We MUST NOT reorder packets under these circumstances.

		   Really, if we split the flow into independent
		   subflows, it would be a very good solution.
		   This is the main idea of all FQ algorithms
		   (cf. CSZ, HPFQ, HFSC)
		 */
		
		if (q->qdisc->ops->requeue(skb, q->qdisc) != NET_XMIT_SUCCESS) {
			/* When requeue fails skb is dropped */ 
			sch->q.qlen--;
			sch->stats.backlog -= len;
			sch->stats.drops++;
		}	
		
		sch->flags |= TCQ_F_THROTTLED;
		sch->stats.overlimits++;
	}
	return NULL;
}

static void tbf_reset(struct Qdisc* sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;

	qdisc_reset(q->qdisc);
	skb_queue_purge(&sch->q);
	sch->stats.backlog = 0;
	PSCHED_GET_TIME(q->t_c);
	q->tokens = q->buffer;
	q->ptokens = q->mtu;
	sch->flags &= ~TCQ_F_THROTTLED;
	del_timer(&q->wd_timer);
}

static struct Qdisc *tbf_create_dflt_qdisc(struct net_device *dev, u32 limit)
{
	struct Qdisc *q = qdisc_create_dflt(dev, &bfifo_qdisc_ops);
        struct rtattr *rta;
	int ret;
	
	if (q) {
		rta = kmalloc(RTA_LENGTH(sizeof(struct tc_fifo_qopt)), GFP_KERNEL);
		if (rta) {
			rta->rta_type = RTM_NEWQDISC;
			rta->rta_len = RTA_LENGTH(sizeof(struct tc_fifo_qopt)); 
			((struct tc_fifo_qopt *)RTA_DATA(rta))->limit = limit;
			
			ret = q->ops->change(q, rta);
			kfree(rta);
			
			if (ret == 0)
				return q;
		}
		qdisc_destroy(q);
	}

	return NULL;	
}

static int tbf_change(struct Qdisc* sch, struct rtattr *opt)
{
	int err = -EINVAL;
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	struct rtattr *tb[TCA_TBF_PTAB];
	struct tc_tbf_qopt *qopt;
	struct qdisc_rate_table *rtab = NULL;
	struct qdisc_rate_table *ptab = NULL;
	struct Qdisc *child = NULL;
	int max_size,n;

	if (rtattr_parse(tb, TCA_TBF_PTAB, RTA_DATA(opt), RTA_PAYLOAD(opt)) ||
	    tb[TCA_TBF_PARMS-1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_TBF_PARMS-1]) < sizeof(*qopt))
		goto done;

	qopt = RTA_DATA(tb[TCA_TBF_PARMS-1]);
	rtab = qdisc_get_rtab(&qopt->rate, tb[TCA_TBF_RTAB-1]);
	if (rtab == NULL)
		goto done;

	if (qopt->peakrate.rate) {
		if (qopt->peakrate.rate > qopt->rate.rate)
			ptab = qdisc_get_rtab(&qopt->peakrate, tb[TCA_TBF_PTAB-1]);
		if (ptab == NULL)
			goto done;
	}

	for (n = 0; n < 256; n++)
		if (rtab->data[n] > qopt->buffer) break;
	max_size = (n << qopt->rate.cell_log)-1;
	if (ptab) {
		int size;

		for (n = 0; n < 256; n++)
			if (ptab->data[n] > qopt->mtu) break;
		size = (n << qopt->peakrate.cell_log)-1;
		if (size < max_size) max_size = size;
	}
	if (max_size < 0)
		goto done;
	
	if (q->qdisc == &noop_qdisc) {
		if ((child = tbf_create_dflt_qdisc(sch->dev, qopt->limit)) == NULL)
			goto done;
	}

	sch_tree_lock(sch);
	if (child) q->qdisc = child;
	q->limit = qopt->limit;
	q->mtu = qopt->mtu;
	q->max_size = max_size;
	q->buffer = qopt->buffer;
	q->tokens = q->buffer;
	q->ptokens = q->mtu;
	rtab = xchg(&q->R_tab, rtab);
	ptab = xchg(&q->P_tab, ptab);
	sch_tree_unlock(sch);
	err = 0;
done:
	if (rtab)
		qdisc_put_rtab(rtab);
	if (ptab)
		qdisc_put_rtab(ptab);
	return err;
}

static int tbf_init(struct Qdisc* sch, struct rtattr *opt)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	
	if (opt == NULL)
		return -EINVAL;
	
	PSCHED_GET_TIME(q->t_c);
	init_timer(&q->wd_timer);
	q->wd_timer.function = tbf_watchdog;
	q->wd_timer.data = (unsigned long)sch;

	q->qdisc = &noop_qdisc;
	
	return tbf_change(sch, opt);
}

static void tbf_destroy(struct Qdisc *sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;

	del_timer(&q->wd_timer);

	if (q->P_tab)
		qdisc_put_rtab(q->P_tab);
	if (q->R_tab)
		qdisc_put_rtab(q->R_tab);
	
	qdisc_destroy(q->qdisc);
	q->qdisc = &noop_qdisc;
}

static int tbf_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	unsigned char	 *b = skb->tail;
	struct rtattr *rta;
	struct tc_tbf_qopt opt;
	
	rta = (struct rtattr*)b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);
	
	opt.limit = q->limit;
	opt.rate = q->R_tab->rate;
	if (q->P_tab)
		opt.peakrate = q->P_tab->rate;
	else
		memset(&opt.peakrate, 0, sizeof(opt.peakrate));
	opt.mtu = q->mtu;
	opt.buffer = q->buffer;
	RTA_PUT(skb, TCA_TBF_PARMS, sizeof(opt), &opt);
	rta->rta_len = skb->tail - b;

	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int tbf_dump_class(struct Qdisc *sch, unsigned long cl,
	       		  struct sk_buff *skb, struct tcmsg *tcm)
{
	struct tbf_sched_data *q = (struct tbf_sched_data*)sch->data;

	if (cl != 1) 	/* only one class */ 
		return -ENOENT;
    
	tcm->tcm_parent = TC_H_ROOT;
	tcm->tcm_handle = 1;
	tcm->tcm_info = q->qdisc->handle;

	return 0;
}

static int tbf_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		     struct Qdisc **old)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;

	if (new == NULL)
		new = &noop_qdisc;

	sch_tree_lock(sch);	
	*old = xchg(&q->qdisc, new);
	qdisc_reset(*old);
	sch_tree_unlock(sch);
	
	return 0;
}

static struct Qdisc *tbf_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	return q->qdisc;
}

static unsigned long tbf_get(struct Qdisc *sch, u32 classid)
{
	return 1;
}

static void tbf_put(struct Qdisc *sch, unsigned long arg)
{
}

static int tbf_change_class(struct Qdisc *sch, u32 classid, u32 parentid, 
			struct rtattr **tca, unsigned long *arg)
{
	return -ENOSYS;
}

static int tbf_delete(struct Qdisc *sch, unsigned long arg)
{
	return -ENOSYS;
}

static void tbf_walk(struct Qdisc *sch, struct qdisc_walker *walker)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;

	if (!walker->stop) {
		if (walker->count >= walker->skip) 
			if (walker->fn(sch, (unsigned long)q, walker) < 0) { 
				walker->stop = 1;
				return;
			}
		walker->count++;
	}
}

static struct Qdisc_class_ops tbf_class_ops =
{
	.graft		= 	tbf_graft,
	.leaf		=	tbf_leaf,
	.get		=	tbf_get,
	.put		=	tbf_put,
	.change		=	tbf_change_class,
	.delete		=	tbf_delete,
	.walk		=	tbf_walk,
	.dump		=	tbf_dump_class,
};

struct Qdisc_ops tbf_qdisc_ops = {
	.next		=	NULL,
	.cl_ops		=	&tbf_class_ops,
	.id		=	"tbf",
	.priv_size	=	sizeof(struct tbf_sched_data),
	.enqueue	=	tbf_enqueue,
	.dequeue	=	tbf_dequeue,
	.requeue	=	tbf_requeue,
	.drop		=	tbf_drop,
	.init		=	tbf_init,
	.reset		=	tbf_reset,
	.destroy	=	tbf_destroy,
	.change		=	tbf_change,
	.dump		=	tbf_dump,
	.owner		=	THIS_MODULE,
};


#ifdef MODULE
int init_module(void)
{
	return register_qdisc(&tbf_qdisc_ops);
}

void cleanup_module(void) 
{
	unregister_qdisc(&tbf_qdisc_ops);
}
#endif
MODULE_LICENSE("GPL");
