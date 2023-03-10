/*
 *  linux/include/linux/sunrpc/timer.h
 *
 *  Declarations for the RPC transport timer.
 *
 *  Copyright (C) 2002 Trond Myklebust <trond.myklebust@fys.uio.no>
 */

#ifndef _LINUX_SUNRPC_TIMER_H
#define _LINUX_SUNRPC_TIMER_H

#include <asm/atomic.h>

struct rpc_rtt {
	unsigned long timeo;	/* default timeout value */
	unsigned long srtt[5];	/* smoothed round trip time << 3 */
	unsigned long sdrtt[5];	/* smoothed medium deviation of RTT */
	atomic_t  ntimeouts;	/* Global count of the number of timeouts */
};


extern void rpc_init_rtt(struct rpc_rtt *rt, unsigned long timeo);
extern void rpc_update_rtt(struct rpc_rtt *rt, unsigned timer, long m);
extern unsigned long rpc_calc_rto(struct rpc_rtt *rt, unsigned timer);

static inline void rpc_inc_timeo(struct rpc_rtt *rt)
{
	atomic_inc(&rt->ntimeouts);
}

static inline void rpc_clear_timeo(struct rpc_rtt *rt)
{
	atomic_set(&rt->ntimeouts, 0);
}

static inline int rpc_ntimeo(struct rpc_rtt *rt)
{
	return atomic_read(&rt->ntimeouts);
}

#endif /* _LINUX_SUNRPC_TIMER_H */
