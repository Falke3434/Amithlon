/*
 *	X.25 Packet Layer release 002
 *
 *	This is ALPHA test software. This code may break your machine,
 *	randomly fail to work with new releases, misbehave and/or generally
 *	screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	X.25 001	Jonathan Naylor	Started coding.
 *	X.25 002	Jonathan Naylor	New timer architecture.
 *					Centralised disconnection processing.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/x25.h>

static void x25_heartbeat_expiry(unsigned long);
static void x25_timer_expiry(unsigned long);

void x25_start_heartbeat(struct sock *sk)
{
	del_timer(&sk->sk_timer);

	sk->sk_timer.data     = (unsigned long)sk;
	sk->sk_timer.function = &x25_heartbeat_expiry;
	sk->sk_timer.expires  = jiffies + 5 * HZ;

	add_timer(&sk->sk_timer);
}

void x25_stop_heartbeat(struct sock *sk)
{
	del_timer(&sk->sk_timer);
}

void x25_start_t2timer(struct sock *sk)
{
	struct x25_opt *x25 = x25_sk(sk);

	del_timer(&x25->timer);

	x25->timer.data     = (unsigned long)sk;
	x25->timer.function = &x25_timer_expiry;
	x25->timer.expires  = jiffies + x25->t2;

	add_timer(&x25->timer);
}

void x25_start_t21timer(struct sock *sk)
{
	struct x25_opt *x25 = x25_sk(sk);

	del_timer(&x25->timer);

	x25->timer.data     = (unsigned long)sk;
	x25->timer.function = &x25_timer_expiry;
	x25->timer.expires  = jiffies + x25->t21;

	add_timer(&x25->timer);
}

void x25_start_t22timer(struct sock *sk)
{
	struct x25_opt *x25 = x25_sk(sk);

	del_timer(&x25->timer);

	x25->timer.data     = (unsigned long)sk;
	x25->timer.function = &x25_timer_expiry;
	x25->timer.expires  = jiffies + x25->t22;

	add_timer(&x25->timer);
}

void x25_start_t23timer(struct sock *sk)
{
	struct x25_opt *x25 = x25_sk(sk);

	del_timer(&x25->timer);

	x25->timer.data     = (unsigned long)sk;
	x25->timer.function = &x25_timer_expiry;
	x25->timer.expires  = jiffies + x25->t23;

	add_timer(&x25->timer);
}

void x25_stop_timer(struct sock *sk)
{
	del_timer(&x25_sk(sk)->timer);
}

unsigned long x25_display_timer(struct sock *sk)
{
	struct x25_opt *x25 = x25_sk(sk);

	if (!timer_pending(&x25->timer))
		return 0;

	return x25->timer.expires - jiffies;
}

static void x25_heartbeat_expiry(unsigned long param)
{
	struct sock *sk = (struct sock *)param;

        bh_lock_sock(sk);
        if (sock_owned_by_user(sk)) /* can currently only occur in state 3 */ 
		goto restart_heartbeat;

	switch (x25_sk(sk)->state) {

		case X25_STATE_0:
			/*
			 * Magic here: If we listen() and a new link dies
			 * before it is accepted() it isn't 'dead' so doesn't
			 * get removed.
			 */
			if (sock_flag(sk, SOCK_DESTROY) ||
			    (sk->sk_state == TCP_LISTEN &&
			     sock_flag(sk, SOCK_DEAD))) {
				x25_destroy_socket(sk);
				goto unlock;
			}
			break;

		case X25_STATE_3:
			/*
			 * Check for the state of the receive buffer.
			 */
			x25_check_rbuf(sk);
			break;
	}
restart_heartbeat:
	x25_start_heartbeat(sk);
unlock:
	bh_unlock_sock(sk);
}

/*
 *	Timer has expired, it may have been T2, T21, T22, or T23. We can tell
 *	by the state machine state.
 */
static inline void x25_do_timer_expiry(struct sock * sk)
{
	struct x25_opt *x25 = x25_sk(sk);

	switch (x25->state) {

		case X25_STATE_3:	/* T2 */
			if (x25->condition & X25_COND_ACK_PENDING) {
				x25->condition &= ~X25_COND_ACK_PENDING;
				x25_enquiry_response(sk);
			}
			break;

		case X25_STATE_1:	/* T21 */
		case X25_STATE_4:	/* T22 */
			x25_write_internal(sk, X25_CLEAR_REQUEST);
			x25->state = X25_STATE_2;
			x25_start_t23timer(sk);
			break;

		case X25_STATE_2:	/* T23 */
			x25_disconnect(sk, ETIMEDOUT, 0, 0);
			break;
	}
}

static void x25_timer_expiry(unsigned long param)
{
	struct sock *sk = (struct sock *)param;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) { /* can currently only occur in state 3 */
		if (x25_sk(sk)->state == X25_STATE_3)
			x25_start_t2timer(sk);
	} else
		x25_do_timer_expiry(sk);
	bh_unlock_sock(sk);
}
