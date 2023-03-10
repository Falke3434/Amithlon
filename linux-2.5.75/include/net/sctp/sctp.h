/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines, Corp.
 * Copyright (c) 2001-2003 Intel Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * The base lksctp header.
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
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Daisy Chang           <daisyc@us.ibm.com>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 *    Ardelle Fan           <ardelle.fan@intel.com>
 *    Ryan Layer            <rmlayer@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#ifndef __net_sctp_h__
#define __net_sctp_h__

/* Header Strategy.
 *    Start getting some control over the header file depencies:
 *       includes
 *       constants
 *       structs
 *       prototypes
 *       macros, externs, and inlines
 *
 *   Move test_frame specific items out of the kernel headers
 *   and into the test frame headers.   This is not perfect in any sense
 *   and will continue to evolve.
 */


#include <linux/config.h>

#ifdef TEST_FRAME
#undef CONFIG_PROC_FS
#undef CONFIG_SCTP_DBG_OBJCNT
#undef CONFIG_SYSCTL
#endif /* TEST_FRAME */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#include <net/ipv6.h>
#include <net/ip6_route.h>
#endif

#include <asm/uaccess.h>
#include <asm/page.h>
#include <net/sock.h>
#include <net/snmp.h>
#include <net/sctp/structs.h>
#include <net/sctp/constants.h>
#include <net/sctp/sm.h>


/* Set SCTP_DEBUG flag via config if not already set. */
#ifndef SCTP_DEBUG
#ifdef CONFIG_SCTP_DBG_MSG
#define SCTP_DEBUG	1
#else
#define SCTP_DEBUG      0
#endif /* CONFIG_SCTP_DBG */
#endif /* SCTP_DEBUG */

#ifdef CONFIG_IP_SCTP_MODULE
#define SCTP_PROTOSW_FLAG 0
#else /* static! */
#define SCTP_PROTOSW_FLAG INET_PROTOSW_PERMANENT
#endif


/* Certain internal static functions need to be exported when
 * compiled into the test frame.
 */
#ifndef SCTP_STATIC
#define SCTP_STATIC static
#endif

/*
 * Function declarations.
 */

/*
 * sctp/protocol.c
 */
extern struct sock *sctp_get_ctl_sock(void);
extern int sctp_copy_local_addr_list(struct sctp_bind_addr *,
				     sctp_scope_t, int gfp, int flags);
extern struct sctp_pf *sctp_get_pf_specific(sa_family_t family);
extern int sctp_register_pf(struct sctp_pf *, sa_family_t);

/*
 * sctp/socket.c
 */
int sctp_backlog_rcv(struct sock *sk, struct sk_buff *skb);
int sctp_inet_listen(struct socket *sock, int backlog);
void sctp_write_space(struct sock *sk);
unsigned int sctp_poll(struct file *file, struct socket *sock,
		poll_table *wait);

/*
 * sctp/primitive.c
 */
int sctp_primitive_ASSOCIATE(struct sctp_association *, void *arg);
int sctp_primitive_SHUTDOWN(struct sctp_association *, void *arg);
int sctp_primitive_ABORT(struct sctp_association *, void *arg);
int sctp_primitive_SEND(struct sctp_association *, void *arg);
int sctp_primitive_REQUESTHEARTBEAT(struct sctp_association *, void *arg);

/*
 * sctp/crc32c.c
 */
__u32 sctp_start_cksum(__u8 *ptr, __u16 count);
__u32 sctp_update_cksum(__u8 *ptr, __u16 count, __u32 cksum);
__u32 sctp_end_cksum(__u32 cksum);
__u32 sctp_update_copy_cksum(__u8 *, __u8 *, __u16 count, __u32 cksum);

/*
 * sctp/input.c
 */
int sctp_rcv(struct sk_buff *skb);
void sctp_v4_err(struct sk_buff *skb, u32 info);
void sctp_hash_established(struct sctp_association *);
void __sctp_hash_established(struct sctp_association *);
void sctp_unhash_established(struct sctp_association *);
void __sctp_unhash_established(struct sctp_association *);
void sctp_hash_endpoint(struct sctp_endpoint *);
void __sctp_hash_endpoint(struct sctp_endpoint *);
void sctp_unhash_endpoint(struct sctp_endpoint *);
void __sctp_unhash_endpoint(struct sctp_endpoint *);
struct sctp_association *__sctp_lookup_association(
	const union sctp_addr *,
	const union sctp_addr *,
	struct sctp_transport **);
struct sock *sctp_err_lookup(int family, struct sk_buff *,
			     struct sctphdr *, struct sctp_endpoint **,
			     struct sctp_association **,
			     struct sctp_transport **);
void sctp_err_finish(struct sock *, struct sctp_endpoint *,
			    struct sctp_association *);
void sctp_icmp_frag_needed(struct sock *, struct sctp_association *,
			   struct sctp_transport *t, __u32 pmtu);

/*
 *  Section:  Macros, externs, and inlines
 */


#ifdef TEST_FRAME
#include <test_frame.h>
#else

/* spin lock wrappers. */
#define sctp_spin_lock_irqsave(lock, flags) spin_lock_irqsave(lock, flags)
#define sctp_spin_unlock_irqrestore(lock, flags)  \
       spin_unlock_irqrestore(lock, flags)
#define sctp_local_bh_disable() local_bh_disable()
#define sctp_local_bh_enable()  local_bh_enable()
#define sctp_spin_lock(lock)    spin_lock(lock)
#define sctp_spin_unlock(lock)  spin_unlock(lock)
#define sctp_write_lock(lock)   write_lock(lock)
#define sctp_write_unlock(lock) write_unlock(lock)
#define sctp_read_lock(lock)    read_lock(lock)
#define sctp_read_unlock(lock)  read_unlock(lock)

/* sock lock wrappers. */
#define sctp_lock_sock(sk)       lock_sock(sk)
#define sctp_release_sock(sk)    release_sock(sk)
#define sctp_bh_lock_sock(sk)    bh_lock_sock(sk)
#define sctp_bh_unlock_sock(sk)  bh_unlock_sock(sk)
#define SCTP_SOCK_SLEEP_PRE(sk)  SOCK_SLEEP_PRE(sk)
#define SCTP_SOCK_SLEEP_POST(sk) SOCK_SLEEP_POST(sk)

/* SCTP SNMP MIB stats handlers */
DECLARE_SNMP_STAT(struct sctp_mib, sctp_statistics);
#define SCTP_INC_STATS(field)      SNMP_INC_STATS(sctp_statistics, field)
#define SCTP_INC_STATS_BH(field)   SNMP_INC_STATS_BH(sctp_statistics, field)
#define SCTP_INC_STATS_USER(field) SNMP_INC_STATS_USER(sctp_statistics, field)
#define SCTP_DEC_STATS(field)      SNMP_DEC_STATS(sctp_statistics, field)

/* Determine if this is a valid kernel address.  */
static inline int sctp_is_valid_kaddr(unsigned long addr)
{
	struct page *page;

	/* Make sure the address is not in the user address space. */
	if (addr < PAGE_OFFSET)
		return 0;

	page = virt_to_page(addr);

	/* Is this page valid? */
	if (!virt_addr_valid(addr) || PageReserved(page))
		return 0;

	return 1;
}

#endif /* !TEST_FRAME */


/* Print debugging messages.  */
#if SCTP_DEBUG
extern int sctp_debug_flag;
#define SCTP_DEBUG_PRINTK(whatever...) \
	((void) (sctp_debug_flag && printk(KERN_DEBUG whatever)))
#define SCTP_ENABLE_DEBUG { sctp_debug_flag = 1; }
#define SCTP_DISABLE_DEBUG { sctp_debug_flag = 0; }

#define SCTP_ASSERT(expr, str, func) \
	if (!(expr)) { \
		SCTP_DEBUG_PRINTK("Assertion Failed: %s(%s) at %s:%s:%d\n", \
			str, (#expr), __FILE__, __FUNCTION__, __LINE__); \
		func; \
	}

#else	/* SCTP_DEBUG */

#define SCTP_DEBUG_PRINTK(whatever...)
#define SCTP_ENABLE_DEBUG
#define SCTP_DISABLE_DEBUG
#define SCTP_ASSERT(expr, str, func)

#endif /* SCTP_DEBUG */


/*
 * Macros for keeping a global reference of object allocations.
 */
#ifdef CONFIG_SCTP_DBG_OBJCNT

extern atomic_t sctp_dbg_objcnt_sock;
extern atomic_t sctp_dbg_objcnt_ep;
extern atomic_t sctp_dbg_objcnt_assoc;
extern atomic_t sctp_dbg_objcnt_transport;
extern atomic_t sctp_dbg_objcnt_chunk;
extern atomic_t sctp_dbg_objcnt_bind_addr;
extern atomic_t sctp_dbg_objcnt_bind_bucket;
extern atomic_t sctp_dbg_objcnt_addr;
extern atomic_t sctp_dbg_objcnt_ssnmap;
extern atomic_t sctp_dbg_objcnt_datamsg;

/* Macros to atomically increment/decrement objcnt counters.  */
#define SCTP_DBG_OBJCNT_INC(name) \
atomic_inc(&sctp_dbg_objcnt_## name)
#define SCTP_DBG_OBJCNT_DEC(name) \
atomic_dec(&sctp_dbg_objcnt_## name)
#define SCTP_DBG_OBJCNT(name) \
atomic_t sctp_dbg_objcnt_## name = ATOMIC_INIT(0)

/* Macro to help create new entries in in the global array of
 * objcnt counters.
 */
#define SCTP_DBG_OBJCNT_ENTRY(name) \
{.label= #name, .counter= &sctp_dbg_objcnt_## name}

void sctp_dbg_objcnt_init(void);
void sctp_dbg_objcnt_exit(void);

#else

#define SCTP_DBG_OBJCNT_INC(name)
#define SCTP_DBG_OBJCNT_DEC(name)

static inline void sctp_dbg_objcnt_init(void) { return; }
static inline void sctp_dbg_objcnt_exit(void) { return; }

#endif /* CONFIG_SCTP_DBG_OBJCOUNT */

#if defined CONFIG_SYSCTL
void sctp_sysctl_register(void);
void sctp_sysctl_unregister(void);
#else
static inline void sctp_sysctl_register(void) { return; }
static inline void sctp_sysctl_unregister(void) { return; }
static inline int sctp_sysctl_jiffies_ms(ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen, void **context) {
	return -ENOSYS;
}
#endif

/* Size of Supported Address Parameter for 'x' address types. */
#define SCTP_SAT_LEN(x) (sizeof(struct sctp_paramhdr) + (x) * sizeof(__u16))

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

int sctp_v6_init(void);
void sctp_v6_exit(void);
void sctp_v6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			int type, int code, int offset, __u32 info);

#else /* #ifdef defined(CONFIG_IPV6) */

static inline int sctp_v6_init(void) { return 0; }
static inline void sctp_v6_exit(void) { return; }

#endif /* #if defined(CONFIG_IPV6) */

/* Some wrappers, in case crypto not available. */
#if defined (CONFIG_CRYPTO_HMAC)
#define sctp_crypto_alloc_tfm crypto_alloc_tfm
#define sctp_crypto_free_tfm crypto_free_tfm
#define sctp_crypto_hmac crypto_hmac
#else
#define sctp_crypto_alloc_tfm(x...) NULL
#define sctp_crypto_free_tfm(x...)
#define sctp_crypto_hmac(x...)
#endif


/* Map an association to an assoc_id. */
static inline sctp_assoc_t sctp_assoc2id(const struct sctp_association *asoc)
{
	return (sctp_assoc_t) asoc;
}

/* Look up the association by its id.  */
struct sctp_association *sctp_id2assoc(struct sock *sk, sctp_assoc_t id);


/* A macro to walk a list of skbs.  */
#define sctp_skb_for_each(pos, head, tmp) \
for (pos = (head)->next;\
     tmp = (pos)->next, pos != ((struct sk_buff *)(head));\
     pos = tmp)


/* A helper to append an entire skb list (list) to another (head). */
static inline void sctp_skb_list_tail(struct sk_buff_head *list,
				      struct sk_buff_head *head)
{
	unsigned long flags;

	sctp_spin_lock_irqsave(&head->lock, flags);
	sctp_spin_lock(&list->lock);

	list_splice((struct list_head *)list, (struct list_head *)head->prev);

	head->qlen += list->qlen;
	list->qlen = 0;

	sctp_spin_unlock(&list->lock);
	sctp_spin_unlock_irqrestore(&head->lock, flags);
}

/**
 *	sctp_list_dequeue - remove from the head of the queue
 *	@list: list to dequeue from
 *
 *	Remove the head of the list. The head item is
 *	returned or %NULL if the list is empty.
 */

static inline struct list_head *sctp_list_dequeue(struct list_head *list)
{
	struct list_head *result = NULL;

	if (list->next != list) {
		result = list->next;
		list->next = result->next;
		list->next->prev = list;
		INIT_LIST_HEAD(result);
	}
	return result;
}

/* Calculate the size (in bytes) occupied by the data of an iovec.  */
static inline size_t get_user_iov_size(struct iovec *iov, int iovlen)
{
	size_t retval = 0;

	for (; iovlen > 0; --iovlen) {
		retval += iov->iov_len;
		iov++;
	}

	return retval;
}

/* Generate a random jitter in the range of -50% ~ +50% of input RTO. */
static inline __s32 sctp_jitter(__u32 rto)
{
	static __u32 sctp_rand;
	__s32 ret;

	/* Avoid divide by zero. */
	if (!rto)
		rto = 1;

	sctp_rand += jiffies;
	sctp_rand ^= (sctp_rand << 12);
	sctp_rand ^= (sctp_rand >> 20);

	/* Choose random number from 0 to rto, then move to -50% ~ +50%
	 * of rto.
	 */
	ret = sctp_rand % rto - (rto >> 1);
	return ret;
}

/* Break down data chunks at this point.  */
static inline int sctp_frag_point(const struct sctp_opt *sp, int pmtu)
{
	int frag = pmtu;
	frag -= SCTP_IP_OVERHEAD + sizeof(struct sctp_data_chunk);
	frag -= sizeof(struct sctp_sack_chunk);

	if (sp->user_frag)
		frag = min_t(int, frag, sp->user_frag);

	return frag;
}

/* Walk through a list of TLV parameters.  Don't trust the
 * individual parameter lengths and instead depend on
 * the chunk length to indicate when to stop.  Make sure
 * there is room for a param header too.
 */
#define sctp_walk_params(pos, chunk, member)\
_sctp_walk_params((pos), (chunk), WORD_ROUND(ntohs((chunk)->chunk_hdr.length)), member)

#define _sctp_walk_params(pos, chunk, end, member)\
for (pos.v = chunk->member;\
     pos.v <= (void *)chunk + end - sizeof(sctp_paramhdr_t) &&\
     pos.v <= (void *)chunk + end - WORD_ROUND(ntohs(pos.p->length)); \
     pos.v += WORD_ROUND(ntohs(pos.p->length)))

#define sctp_walk_errors(err, chunk_hdr)\
_sctp_walk_errors((err), (chunk_hdr), ntohs((chunk_hdr)->length))

#define _sctp_walk_errors(err, chunk_hdr, end)\
for (err = (sctp_errhdr_t *)((void *)chunk_hdr + \
	    sizeof(sctp_chunkhdr_t));\
     (void *)err <= (void *)chunk_hdr + end - sizeof(sctp_errhdr_t) &&\
     (void *)err <= (void *)chunk_hdr + end - \
		    WORD_ROUND(ntohs(err->length));\
     err = (sctp_errhdr_t *)((void *)err + \
	    WORD_ROUND(ntohs(err->length))))

/* Round an int up to the next multiple of 4.  */
#define WORD_ROUND(s) (((s)+3)&~3)

/* Make a new instance of type.  */
#define t_new(type, flags)	(type *)kmalloc(sizeof(type), flags)

/* Compare two timevals.  */
#define tv_lt(s, t) \
   (s.tv_sec < t.tv_sec || (s.tv_sec == t.tv_sec && s.tv_usec < t.tv_usec))

/* Stolen from net/profile.h.  Using it from there is more grief than
 * it is worth.
 */
static inline void tv_add(const struct timeval *entered, struct timeval *leaved)
{
	time_t usecs = leaved->tv_usec + entered->tv_usec;
	time_t secs = leaved->tv_sec + entered->tv_sec;

	if (usecs >= 1000000) {
		usecs -= 1000000;
		secs++;
	}
	leaved->tv_sec = secs;
	leaved->tv_usec = usecs;
}


/* External references. */

extern struct proto sctp_prot;
extern struct proc_dir_entry *proc_net_sctp;
void sctp_put_port(struct sock *sk);

/* Static inline functions. */

/* Convert from an IP version number to an Address Family symbol.  */
static inline int ipver2af(__u8 ipver)
{
	switch (ipver) {
	case 4:
	        return  AF_INET;
	case 6:
		return AF_INET6;
	default:
		return 0;
	};
}

/* Perform some sanity checks. */
static inline int sctp_sanity_check(void)
{
	SCTP_ASSERT(sizeof(struct sctp_ulpevent) <=
		    sizeof(((struct sk_buff *)0)->cb),
		    "SCTP: ulpevent does not fit in skb!\n", return 0);

	return 1;
}

/* Warning: The following hash functions assume a power of two 'size'. */
/* This is the hash function for the SCTP port hash table. */
static inline int sctp_phashfn(__u16 lport)
{
	return (lport & (sctp_port_hashsize - 1));
}

/* This is the hash function for the endpoint hash table. */
static inline int sctp_ep_hashfn(__u16 lport)
{
	return (lport & (sctp_ep_hashsize - 1));
}

/* This is the hash function for the association hash table. */
static inline int sctp_assoc_hashfn(__u16 lport, __u16 rport)
{
	int h = (lport << 16) + rport;
	h ^= h>>8;
	return (h & (sctp_assoc_hashsize - 1));
}

/* This is the hash function for the association hash table.  This is
 * not used yet, but could be used as a better hash function when
 * we have a vtag.
 */
static inline int sctp_vtag_hashfn(__u16 lport, __u16 rport, __u32 vtag)
{
	int h = (lport << 16) + rport;
	h ^= vtag;
	return (h & (sctp_assoc_hashsize-1));
}

/* WARNING: Do not change the layout of the members in sctp_sock! */
struct sctp_sock {
	struct sock	  sk;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	struct ipv6_pinfo *pinet6;
#endif /* CONFIG_IPV6 */
	struct inet_opt	  inet;
	struct sctp_opt	  sctp;
};

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
struct sctp6_sock {
	struct sock	  sk;
	struct ipv6_pinfo *pinet6;
	struct inet_opt	  inet;
	struct sctp_opt	  sctp;
	struct ipv6_pinfo inet6;
};
#endif /* CONFIG_IPV6 */

#define sctp_sk(__sk) (&((struct sctp_sock *)__sk)->sctp)

/* Is a socket of this style? */
#define sctp_style(sk, style) __sctp_style((sk), (SCTP_SOCKET_##style))
int static inline __sctp_style(const struct sock *sk, sctp_socket_type_t style)
{
	return sctp_sk(sk)->type == style;
}

/* Is the association in this state? */
#define sctp_state(asoc, state) __sctp_state((asoc), (SCTP_STATE_##state))
int static inline __sctp_state(const struct sctp_association *asoc,
			       sctp_state_t state)
{
	return asoc->state == state;
}

/* Is the socket in this state? */
#define sctp_sstate(sk, state) __sctp_sstate((sk), (SCTP_SS_##state))
int static inline __sctp_sstate(const struct sock *sk, sctp_sock_state_t state)
{
	return sk->sk_state == state;
}

#endif /* __net_sctp_h__ */
