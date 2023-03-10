/*
 *	UDP over IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Based on linux/ipv4/udp.c
 *
 *	$Id: udp.c,v 1.65 2002/02/01 22:01:04 davem Exp $
 *
 *	Fixes:
 *	Hideaki YOSHIFUJI	:	sin6_scope_id support
 *	YOSHIFUJI Hideaki @USAGI and:	Support IPV6_V6ONLY socket option, which
 *	Alexey Kuznetsov		allow both IPv4 and IPv6 sockets to bind
 *					a single port at the same time.
 *      Kazunori MIYAZAWA @USAGI:       change process style to use ip6_append_data
 *      YOSHIFUJI Hideaki @USAGI:	convert /proc/net/udp6 to seq_file.
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/init.h>
#include <asm/uaccess.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/inet_common.h>

#include <net/checksum.h>
#include <net/xfrm.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

DEFINE_SNMP_STAT(struct udp_mib, udp_stats_in6);

/* Grrr, addr_type already calculated by caller, but I don't want
 * to add some silly "cookie" argument to this method just for that.
 */
static int udp_v6_get_port(struct sock *sk, unsigned short snum)
{
	struct sock *sk2;
	struct hlist_node *node;

	write_lock_bh(&udp_hash_lock);
	if (snum == 0) {
		int best_size_so_far, best, result, i;

		if (udp_port_rover > sysctl_local_port_range[1] ||
		    udp_port_rover < sysctl_local_port_range[0])
			udp_port_rover = sysctl_local_port_range[0];
		best_size_so_far = 32767;
		best = result = udp_port_rover;
		for (i = 0; i < UDP_HTABLE_SIZE; i++, result++) {
			int size;
			struct hlist_head *list;

			list = &udp_hash[result & (UDP_HTABLE_SIZE - 1)];
			if (hlist_empty(list)) {
				if (result > sysctl_local_port_range[1])
					result = sysctl_local_port_range[0] +
						((result - sysctl_local_port_range[0]) &
						 (UDP_HTABLE_SIZE - 1));
				goto gotit;
			}
			size = 0;
			sk_for_each(sk2, node, list)
				if (++size >= best_size_so_far)
					goto next;
			best_size_so_far = size;
			best = result;
		next:;
		}
		result = best;
		for(;; result += UDP_HTABLE_SIZE) {
			if (result > sysctl_local_port_range[1])
				result = sysctl_local_port_range[0]
					+ ((result - sysctl_local_port_range[0]) &
					   (UDP_HTABLE_SIZE - 1));
			if (!udp_lport_inuse(result))
				break;
		}
gotit:
		udp_port_rover = snum = result;
	} else {
		sk_for_each(sk2, node,
			    &udp_hash[snum & (UDP_HTABLE_SIZE - 1)]) {
			if (inet_sk(sk2)->num == snum &&
			    sk2 != sk &&
			    sk2->sk_bound_dev_if == sk->sk_bound_dev_if &&
			    (!sk2->sk_reuse || !sk->sk_reuse) &&
			    ipv6_rcv_saddr_equal(sk, sk2))
				goto fail;
		}
	}

	inet_sk(sk)->num = snum;
	if (sk_unhashed(sk)) {
		sk_add_node(sk, &udp_hash[snum & (UDP_HTABLE_SIZE - 1)]);
		sock_prot_inc_use(sk->sk_prot);
	}
	write_unlock_bh(&udp_hash_lock);
	return 0;

fail:
	write_unlock_bh(&udp_hash_lock);
	return 1;
}

static void udp_v6_hash(struct sock *sk)
{
	BUG();
}

static void udp_v6_unhash(struct sock *sk)
{
 	write_lock_bh(&udp_hash_lock);
	if (sk_del_node_init(sk)) {
		inet_sk(sk)->num = 0;
		sock_prot_dec_use(sk->sk_prot);
	}
	write_unlock_bh(&udp_hash_lock);
}

static struct sock *udp_v6_lookup(struct in6_addr *saddr, u16 sport,
				  struct in6_addr *daddr, u16 dport, int dif)
{
	struct sock *sk, *result = NULL;
	struct hlist_node *node;
	unsigned short hnum = ntohs(dport);
	int badness = -1;

 	read_lock(&udp_hash_lock);
	sk_for_each(sk, node, &udp_hash[hnum & (UDP_HTABLE_SIZE - 1)]) {
		struct inet_opt *inet = inet_sk(sk);

		if (inet->num == hnum && sk->sk_family == PF_INET6) {
			struct ipv6_pinfo *np = inet6_sk(sk);
			int score = 0;
			if (inet->dport) {
				if (inet->dport != sport)
					continue;
				score++;
			}
			if (!ipv6_addr_any(&np->rcv_saddr)) {
				if (ipv6_addr_cmp(&np->rcv_saddr, daddr))
					continue;
				score++;
			}
			if (!ipv6_addr_any(&np->daddr)) {
				if (ipv6_addr_cmp(&np->daddr, saddr))
					continue;
				score++;
			}
			if (sk->sk_bound_dev_if) {
				if (sk->sk_bound_dev_if != dif)
					continue;
				score++;
			}
			if(score == 4) {
				result = sk;
				break;
			} else if(score > badness) {
				result = sk;
				badness = score;
			}
		}
	}
	if (result)
		sock_hold(result);
 	read_unlock(&udp_hash_lock);
	return result;
}

/*
 *
 */

int udpv6_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in6	*usin = (struct sockaddr_in6 *) uaddr;
	struct inet_opt      	*inet = inet_sk(sk);
	struct ipv6_pinfo      	*np = inet6_sk(sk);
	struct in6_addr		*daddr;
	struct dst_entry	*dst;
	struct flowi		fl;
	struct ip6_flowlabel	*flowlabel = NULL;
	int			addr_type;
	int			err;

	if (usin->sin6_family == AF_INET) {
		if (__ipv6_only_sock(sk))
			return -EAFNOSUPPORT;
		err = udp_connect(sk, uaddr, addr_len);
		goto ipv4_connected;
	}

	if (addr_len < SIN6_LEN_RFC2133)
	  	return -EINVAL;

	if (usin->sin6_family != AF_INET6) 
	  	return -EAFNOSUPPORT;

	memset(&fl, 0, sizeof(fl));
	if (np->sndflow) {
		fl.fl6_flowlabel = usin->sin6_flowinfo&IPV6_FLOWINFO_MASK;
		if (fl.fl6_flowlabel&IPV6_FLOWLABEL_MASK) {
			flowlabel = fl6_sock_lookup(sk, fl.fl6_flowlabel);
			if (flowlabel == NULL)
				return -EINVAL;
			ipv6_addr_copy(&usin->sin6_addr, &flowlabel->dst);
		}
	}

	addr_type = ipv6_addr_type(&usin->sin6_addr);

	if (addr_type == IPV6_ADDR_ANY) {
		/*
		 *	connect to self
		 */
		usin->sin6_addr.s6_addr[15] = 0x01;
	}

	daddr = &usin->sin6_addr;

	if (addr_type == IPV6_ADDR_MAPPED) {
		struct sockaddr_in sin;

		if (__ipv6_only_sock(sk)) {
			err = -ENETUNREACH;
			goto out;
		}
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = daddr->s6_addr32[3];
		sin.sin_port = usin->sin6_port;

		err = udp_connect(sk, (struct sockaddr*) &sin, sizeof(sin));

ipv4_connected:
		if (err)
			goto out;
		
		ipv6_addr_set(&np->daddr, 0, 0, htonl(0x0000ffff), inet->daddr);

		if (ipv6_addr_any(&np->saddr)) {
			ipv6_addr_set(&np->saddr, 0, 0, htonl(0x0000ffff),
				      inet->saddr);
		}

		if (ipv6_addr_any(&np->rcv_saddr)) {
			ipv6_addr_set(&np->rcv_saddr, 0, 0, htonl(0x0000ffff),
				      inet->rcv_saddr);
		}
		goto out;
	}

	if (addr_type&IPV6_ADDR_LINKLOCAL) {
		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    usin->sin6_scope_id) {
			if (sk->sk_bound_dev_if &&
			    sk->sk_bound_dev_if != usin->sin6_scope_id) {
				err = -EINVAL;
				goto out;
			}
			sk->sk_bound_dev_if = usin->sin6_scope_id;
			if (!sk->sk_bound_dev_if &&
			    (addr_type & IPV6_ADDR_MULTICAST))
				fl.oif = np->mcast_oif;
		}

		/* Connect to link-local address requires an interface */
		if (!sk->sk_bound_dev_if) {
			err = -EINVAL;
			goto out;
		}
	}

	ipv6_addr_copy(&np->daddr, daddr);
	np->flow_label = fl.fl6_flowlabel;

	inet->dport = usin->sin6_port;

	/*
	 *	Check for a route to destination an obtain the
	 *	destination cache for it.
	 */

	fl.proto = IPPROTO_UDP;
	ipv6_addr_copy(&fl.fl6_dst, &np->daddr);
	ipv6_addr_copy(&fl.fl6_src, &np->saddr);
	fl.oif = sk->sk_bound_dev_if;
	fl.fl_ip_dport = inet->dport;
	fl.fl_ip_sport = inet->sport;

	if (!fl.oif && (addr_type&IPV6_ADDR_MULTICAST))
		fl.oif = np->mcast_oif;

	if (flowlabel) {
		if (flowlabel->opt && flowlabel->opt->srcrt) {
			struct rt0_hdr *rt0 = (struct rt0_hdr *) flowlabel->opt->srcrt;
			ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
		}
	} else if (np->opt && np->opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *)np->opt->srcrt;
		ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
	}

	dst = ip6_route_output(sk, &fl);

	if ((err = dst->error) != 0) {
		dst_release(dst);
		goto out;
	}

	/* get the source address used in the appropriate device */

	err = ipv6_get_saddr(dst, daddr, &fl.fl6_src);

	if (err) {
		dst_release(dst);
		goto out;
	}

	if (ipv6_addr_any(&np->saddr))
		ipv6_addr_copy(&np->saddr, &fl.fl6_src);

	if (ipv6_addr_any(&np->rcv_saddr)) {
		ipv6_addr_copy(&np->rcv_saddr, &fl.fl6_src);
		inet->rcv_saddr = LOOPBACK4_IPV6;
	}

	ip6_dst_store(sk, dst,
		      !ipv6_addr_cmp(&fl.fl6_dst, &np->daddr) ?
		      &np->daddr : NULL);

	sk->sk_state = TCP_ESTABLISHED;
out:
	fl6_sock_release(flowlabel);
	return err;
}

static void udpv6_close(struct sock *sk, long timeout)
{
	inet_sock_release(sk);
}

/*
 * 	This should be easy, if there is something there we
 * 	return it, otherwise we block.
 */

static int udpv6_recvmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg, int len,
		  int noblock, int flags, int *addr_len)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
  	struct sk_buff *skb;
  	int copied, err;

  	if (addr_len)
  		*addr_len=sizeof(struct sockaddr_in6);
  
	if (flags & MSG_ERRQUEUE)
		return ipv6_recv_error(sk, msg, len);

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

 	copied = skb->len - sizeof(struct udphdr);
  	if (copied > len) {
  		copied = len;
  		msg->msg_flags |= MSG_TRUNC;
  	}

	if (skb->ip_summed==CHECKSUM_UNNECESSARY) {
		err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov,
					      copied);
	} else if (msg->msg_flags&MSG_TRUNC) {
		if ((unsigned short)csum_fold(skb_checksum(skb, 0, skb->len, skb->csum)))
			goto csum_copy_err;
		err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov,
					      copied);
	} else {
		err = skb_copy_and_csum_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov);
		if (err == -EINVAL)
			goto csum_copy_err;
	}
	if (err)
		goto out_free;

	sock_recv_timestamp(msg, sk, skb);

	/* Copy the address. */
	if (msg->msg_name) {
		struct sockaddr_in6 *sin6;
	  
		sin6 = (struct sockaddr_in6 *) msg->msg_name;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = skb->h.uh->source;
		sin6->sin6_flowinfo = 0;
		sin6->sin6_scope_id = 0;

		if (skb->protocol == htons(ETH_P_IP)) {
			struct inet_opt *inet = inet_sk(sk);

			ipv6_addr_set(&sin6->sin6_addr, 0, 0,
				      htonl(0xffff), skb->nh.iph->saddr);
			if (inet->cmsg_flags)
				ip_cmsg_recv(msg, skb);
		} else {
			ipv6_addr_copy(&sin6->sin6_addr, &skb->nh.ipv6h->saddr);

			if (np->rxopt.all)
				datagram_recv_ctl(sk, msg, skb);
			if (ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL) {
				struct inet6_skb_parm *opt = (struct inet6_skb_parm *) skb->cb;
				sin6->sin6_scope_id = opt->iif;
			}
		}
  	}
	err = copied;

out_free:
	skb_free_datagram(sk, skb);
out:
	return err;

csum_copy_err:
	/* Clear queue. */
	if (flags&MSG_PEEK) {
		int clear = 0;
		spin_lock_irq(&sk->sk_receive_queue.lock);
		if (skb == skb_peek(&sk->sk_receive_queue)) {
			__skb_unlink(skb, &sk->sk_receive_queue);
			clear = 1;
		}
		spin_unlock_irq(&sk->sk_receive_queue.lock);
		if (clear)
			kfree_skb(skb);
	}

	/* Error for blocking case is chosen to masquerade
	   as some normal condition.
	 */
	err = (flags&MSG_DONTWAIT) ? -EAGAIN : -EHOSTUNREACH;
	UDP6_INC_STATS_USER(UdpInErrors);
	goto out_free;
}

static void udpv6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
	       int type, int code, int offset, __u32 info)
{
	struct ipv6_pinfo *np;
	struct ipv6hdr *hdr = (struct ipv6hdr*)skb->data;
	struct net_device *dev = skb->dev;
	struct in6_addr *saddr = &hdr->saddr;
	struct in6_addr *daddr = &hdr->daddr;
	struct udphdr *uh = (struct udphdr*)(skb->data+offset);
	struct sock *sk;
	int err;

	sk = udp_v6_lookup(daddr, uh->dest, saddr, uh->source, dev->ifindex);
   
	if (sk == NULL)
		return;

	np = inet6_sk(sk);

	if (!icmpv6_err_convert(type, code, &err) && !np->recverr)
		goto out;

	if (sk->sk_state != TCP_ESTABLISHED && !np->recverr)
		goto out;

	if (np->recverr)
		ipv6_icmp_error(sk, skb, err, uh->dest, ntohl(info), (u8 *)(uh+1));

	sk->sk_err = err;
	sk->sk_error_report(sk);
out:
	sock_put(sk);
}

static inline int udpv6_queue_rcv_skb(struct sock * sk, struct sk_buff *skb)
{
	if (!xfrm6_policy_check(sk, XFRM_POLICY_IN, skb)) {
		kfree_skb(skb);
		return -1;
	}

	if (sk->sk_filter && skb->ip_summed != CHECKSUM_UNNECESSARY) {
		if ((unsigned short)csum_fold(skb_checksum(skb, 0, skb->len, skb->csum))) {
			UDP6_INC_STATS_BH(UdpInErrors);
			kfree_skb(skb);
			return 0;
		}
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	if (sock_queue_rcv_skb(sk,skb)<0) {
		UDP6_INC_STATS_BH(UdpInErrors);
		kfree_skb(skb);
		return 0;
	}
	UDP6_INC_STATS_BH(UdpInDatagrams);
	return 0;
}

static struct sock *udp_v6_mcast_next(struct sock *sk,
				      u16 loc_port, struct in6_addr *loc_addr,
				      u16 rmt_port, struct in6_addr *rmt_addr,
				      int dif)
{
	struct hlist_node *node;
	struct sock *s = sk;
	unsigned short num = ntohs(loc_port);

	sk_for_each_from(s, node) {
		struct inet_opt *inet = inet_sk(s);

		if (inet->num == num && s->sk_family == PF_INET6) {
			struct ipv6_pinfo *np = inet6_sk(s);
			if (inet->dport) {
				if (inet->dport != rmt_port)
					continue;
			}
			if (!ipv6_addr_any(&np->daddr) &&
			    ipv6_addr_cmp(&np->daddr, rmt_addr))
				continue;

			if (s->sk_bound_dev_if && s->sk_bound_dev_if != dif)
				continue;

			if (!ipv6_addr_any(&np->rcv_saddr)) {
				if (!ipv6_addr_cmp(&np->rcv_saddr, loc_addr))
					return s;
			}
			if(!inet6_mc_check(s, loc_addr, rmt_addr))
				continue;
			return s;
		}
	}
	return NULL;
}

/*
 * Note: called only from the BH handler context,
 * so we don't need to lock the hashes.
 */
static void udpv6_mcast_deliver(struct udphdr *uh,
				struct in6_addr *saddr, struct in6_addr *daddr,
				struct sk_buff *skb)
{
	struct sock *sk, *sk2;
	struct sk_buff *buff;
	int dif;

	read_lock(&udp_hash_lock);
	sk = sk_head(&udp_hash[ntohs(uh->dest) & (UDP_HTABLE_SIZE - 1)]);
	dif = skb->dev->ifindex;
	sk = udp_v6_mcast_next(sk, uh->dest, daddr, uh->source, saddr, dif);
	if (!sk)
		goto free_skb;

	buff = NULL;
	sk2 = sk;
	while ((sk2 = udp_v6_mcast_next(sk_next(sk2), uh->dest, daddr,
					uh->source, saddr, dif))) {
		if (!buff) {
			buff = skb_clone(skb, GFP_ATOMIC);
			if (!buff)
				continue;
		}
		if (sock_queue_rcv_skb(sk2, buff) >= 0)
			buff = NULL;
	}
	if (buff)
		kfree_skb(buff);
	if (sock_queue_rcv_skb(sk, skb) < 0) {
free_skb:
		kfree_skb(skb);
	}
	read_unlock(&udp_hash_lock);
}

static int udpv6_rcv(struct sk_buff **pskb, unsigned int *nhoffp)
{
	struct sk_buff *skb = *pskb;
	struct sock *sk;
  	struct udphdr *uh;
	struct net_device *dev = skb->dev;
	struct in6_addr *saddr, *daddr;
	u32 ulen = 0;

	if (!pskb_may_pull(skb, sizeof(struct udphdr)))
		goto short_packet;

	saddr = &skb->nh.ipv6h->saddr;
	daddr = &skb->nh.ipv6h->daddr;
	uh = skb->h.uh;

	ulen = ntohs(uh->len);

	/* Check for jumbo payload */
	if (ulen == 0)
		ulen = skb->len;

	if (ulen > skb->len || ulen < sizeof(*uh))
		goto short_packet;

	if (uh->check == 0) {
		/* RFC 2460 section 8.1 says that we SHOULD log
		   this error. Well, it is reasonable.
		 */
		if (net_ratelimit())
			printk(KERN_INFO "IPv6: udp checksum is 0\n");
		goto discard;
	}

	if (ulen < skb->len) {
		if (__pskb_trim(skb, ulen))
			goto discard;
		saddr = &skb->nh.ipv6h->saddr;
		daddr = &skb->nh.ipv6h->daddr;
		uh = skb->h.uh;
	}

	if (skb->ip_summed==CHECKSUM_HW) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		if (csum_ipv6_magic(saddr, daddr, ulen, IPPROTO_UDP, skb->csum)) {
			NETDEBUG(if (net_ratelimit()) printk(KERN_DEBUG "udp v6 hw csum failure.\n"));
			skb->ip_summed = CHECKSUM_NONE;
		}
	}
	if (skb->ip_summed != CHECKSUM_UNNECESSARY)
		skb->csum = ~csum_ipv6_magic(saddr, daddr, ulen, IPPROTO_UDP, 0);

	/* 
	 *	Multicast receive code 
	 */
	if (ipv6_addr_type(daddr) & IPV6_ADDR_MULTICAST) {
		udpv6_mcast_deliver(uh, saddr, daddr, skb);
		return 0;
	}

	/* Unicast */

	/* 
	 * check socket cache ... must talk to Alan about his plans
	 * for sock caches... i'll skip this for now.
	 */
	sk = udp_v6_lookup(saddr, uh->source, daddr, uh->dest, dev->ifindex);

	if (sk == NULL) {
		if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb))
			goto discard;

		if (skb->ip_summed != CHECKSUM_UNNECESSARY &&
		    (unsigned short)csum_fold(skb_checksum(skb, 0, skb->len, skb->csum)))
			goto discard;
		UDP6_INC_STATS_BH(UdpNoPorts);

		icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0, dev);

		kfree_skb(skb);
		return(0);
	}
	
	/* deliver */
	
	udpv6_queue_rcv_skb(sk, skb);
	sock_put(sk);
	return(0);

short_packet:	
	if (net_ratelimit())
		printk(KERN_DEBUG "UDP: short packet: %d/%u\n", ulen, skb->len);

discard:
	UDP6_INC_STATS_BH(UdpInErrors);
	kfree_skb(skb);
	return(0);	
}
/*
 * Throw away all pending data and cancel the corking. Socket is locked.
 */
static void udp_v6_flush_pending_frames(struct sock *sk)
{
	struct udp_opt *up = udp_sk(sk);

	if (up->pending) {
		up->pending = 0;
		ip6_flush_pending_frames(sk);
        }
}

/*
 *	Sending
 */

static int udp_v6_push_pending_frames(struct sock *sk, struct udp_opt *up)
{
	struct sk_buff *skb;
	struct udphdr *uh;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct flowi *fl = np->cork.fl;
	int err = 0;

	/* Grab the skbuff where UDP header space exists. */
	if ((skb = skb_peek(&sk->sk_write_queue)) == NULL)
		goto out;

	/*
	 * Create a UDP header
	 */
	uh = skb->h.uh;
	uh->source = fl->fl_ip_sport;
	uh->dest = fl->fl_ip_dport;
	uh->len = htons(up->len);
	uh->check = 0;

	if (sk->sk_no_check == UDP_CSUM_NOXMIT) {
		skb->ip_summed = CHECKSUM_NONE;
		goto send;
	}

	if (skb_queue_len(&sk->sk_write_queue) == 1) {
		skb->csum = csum_partial((char *)uh,
				sizeof(struct udphdr), skb->csum);
		uh->check = csum_ipv6_magic(&fl->fl6_src,
					    &fl->fl6_dst,
					    up->len, fl->proto, skb->csum);
	} else {
		u32 tmp_csum = 0;

		skb_queue_walk(&sk->sk_write_queue, skb) {
			tmp_csum = csum_add(tmp_csum, skb->csum);
		}
		tmp_csum = csum_partial((char *)uh,
				sizeof(struct udphdr), tmp_csum);
                tmp_csum = csum_ipv6_magic(&fl->fl6_src,
					   &fl->fl6_dst,
					   up->len, fl->proto, tmp_csum);
                uh->check = tmp_csum;

	}
	if (uh->check == 0)
		uh->check = -1;

send:
	err = ip6_push_pending_frames(sk);
out:
	up->len = 0;
	up->pending = 0;
	return err;
}

static int udpv6_sendmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg, int len)
{
	struct ipv6_txoptions opt_space;
	struct udp_opt *up = udp_sk(sk);
	struct inet_opt *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) msg->msg_name;
	struct in6_addr *daddr;
	struct ipv6_txoptions *opt = NULL;
	struct ip6_flowlabel *flowlabel = NULL;
	struct flowi fl;
	struct dst_entry *dst;
	int addr_len = msg->msg_namelen;
	int ulen = len;
	int addr_type;
	int hlimit = -1;
	int corkreq = up->corkflag || msg->msg_flags&MSG_MORE;
	int err;
	
	/* Rough check on arithmetic overflow,
	   better check is made in ip6_build_xmit
	   */
	if (len < 0 || len > INT_MAX - sizeof(struct udphdr))
		return -EMSGSIZE;
	
	if (up->pending) {
		/*
		 * There are pending frames.
		 * The socket lock must be held while it's corked.
		 */
		lock_sock(sk);
		if (likely(up->pending))
			goto do_append_data;
		release_sock(sk);
	}
	ulen += sizeof(struct udphdr);

	memset(&fl, 0, sizeof(fl));

	if (sin6) {
		if (sin6->sin6_family == AF_INET) {
			if (__ipv6_only_sock(sk))
				return -ENETUNREACH;
			return udp_sendmsg(iocb, sk, msg, len);
		}

		if (addr_len < SIN6_LEN_RFC2133)
			return -EINVAL;

		if (sin6->sin6_family && sin6->sin6_family != AF_INET6)
			return -EINVAL;

		if (sin6->sin6_port == 0)
			return -EINVAL;

		up->dport = sin6->sin6_port;
		daddr = &sin6->sin6_addr;

		if (np->sndflow) {
			fl.fl6_flowlabel = sin6->sin6_flowinfo&IPV6_FLOWINFO_MASK;
			if (fl.fl6_flowlabel&IPV6_FLOWLABEL_MASK) {
				flowlabel = fl6_sock_lookup(sk, fl.fl6_flowlabel);
				if (flowlabel == NULL)
					return -EINVAL;
				daddr = &flowlabel->dst;
			}
		}

		/*
		 * Otherwise it will be difficult to maintain
		 * sk->sk_dst_cache.
		 */
		if (sk->sk_state == TCP_ESTABLISHED &&
		    !ipv6_addr_cmp(daddr, &np->daddr))
			daddr = &np->daddr;

		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    sin6->sin6_scope_id &&
		    ipv6_addr_type(daddr)&IPV6_ADDR_LINKLOCAL)
			fl.oif = sin6->sin6_scope_id;
	} else {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -ENOTCONN;

		up->dport = inet->dport;
		daddr = &np->daddr;
		fl.fl6_flowlabel = np->flow_label;
	}

	addr_type = ipv6_addr_type(daddr);

	if (addr_type == IPV6_ADDR_MAPPED) {
		struct sockaddr_in sin;

		if (__ipv6_only_sock(sk))
			return -ENETUNREACH;

		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = daddr->s6_addr32[3];
		sin.sin_port = up->dport;
		msg->msg_name = (struct sockaddr *)(&sin);
		msg->msg_namelen = sizeof(sin);
		fl6_sock_release(flowlabel);

		return udp_sendmsg(iocb, sk, msg, len);
	}

	if (!fl.oif)
		fl.oif = sk->sk_bound_dev_if;

	if (msg->msg_controllen) {
		opt = &opt_space;
		memset(opt, 0, sizeof(struct ipv6_txoptions));

		err = datagram_send_ctl(msg, &fl, opt, &hlimit);
		if (err < 0) {
			fl6_sock_release(flowlabel);
			return err;
		}
		if ((fl.fl6_flowlabel&IPV6_FLOWLABEL_MASK) && !flowlabel) {
			flowlabel = fl6_sock_lookup(sk, fl.fl6_flowlabel);
			if (flowlabel == NULL)
				return -EINVAL;
		}
		if (!(opt->opt_nflen|opt->opt_flen))
			opt = NULL;
	}
	if (opt == NULL)
		opt = np->opt;
	if (flowlabel)
		opt = fl6_merge_options(&opt_space, flowlabel, opt);

	fl.proto = IPPROTO_UDP;
	ipv6_addr_copy(&fl.fl6_dst, daddr);
	if (ipv6_addr_any(&fl.fl6_src) && !ipv6_addr_any(&np->saddr))
		ipv6_addr_copy(&fl.fl6_src, &np->saddr);
	fl.fl_ip_dport = up->dport;
	fl.fl_ip_sport = inet->sport;
	
	/* merge ip6_build_xmit from ip6_output */
	if (opt && opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *) opt->srcrt;
		ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
	}

	if (!fl.oif && ipv6_addr_is_multicast(&fl.fl6_dst))
		fl.oif = np->mcast_oif;

	err = ip6_dst_lookup(sk, &dst, &fl);
	if (err)
		goto out;

	if (hlimit < 0) {
		if (ipv6_addr_is_multicast(&fl.fl6_dst))
			hlimit = np->mcast_hops;
		else
			hlimit = np->hop_limit;
		if (hlimit < 0)
			hlimit = dst_metric(dst, RTAX_HOPLIMIT);
	}

	if (msg->msg_flags&MSG_CONFIRM)
		goto do_confirm;
back_from_confirm:

	lock_sock(sk);
	if (unlikely(up->pending)) {
		/* The socket is already corked while preparing it. */
		/* ... which is an evident application bug. --ANK */
		release_sock(sk);

		NETDEBUG(if (net_ratelimit()) printk(KERN_DEBUG "udp cork app bug 2\n"));
		err = -EINVAL;
		goto out;
	}

	up->pending = 1;

do_append_data:
	up->len += ulen;
	err = ip6_append_data(sk, ip_generic_getfrag, msg->msg_iov, ulen, sizeof(struct udphdr),
			      hlimit, opt, &fl, (struct rt6_info*)dst,
			      corkreq ? msg->msg_flags|MSG_MORE : msg->msg_flags);
	if (err)
		udp_v6_flush_pending_frames(sk);
	else if (!corkreq)
		err = udp_v6_push_pending_frames(sk, up);

	ip6_dst_store(sk, dst,
		      !ipv6_addr_cmp(&fl.fl6_dst, &np->daddr) ?
		      &np->daddr : NULL);
	if (err > 0)
		err = np->recverr ? net_xmit_errno(err) : 0;
	release_sock(sk);
out:
	fl6_sock_release(flowlabel);
	if (!err) {
		UDP6_INC_STATS_USER(UdpOutDatagrams);
		return len;
	}
	return err;

do_confirm:
	dst_confirm(dst);
	if (!(msg->msg_flags&MSG_PROBE) || len)
		goto back_from_confirm;
	err = 0;
	goto out;
}

static int udpv6_destroy_sock(struct sock *sk)
{
	lock_sock(sk);
	udp_v6_flush_pending_frames(sk);
	release_sock(sk);

	inet6_destroy_sock(sk);

	return 0;
}

/*
 *	Socket option code for UDP
 */
static int udpv6_setsockopt(struct sock *sk, int level, int optname, 
			  char *optval, int optlen)
{
	struct udp_opt *up = udp_sk(sk);
	int val;
	int err = 0;

	if (level != SOL_UDP)
		return ipv6_setsockopt(sk, level, optname, optval, optlen);

	if(optlen<sizeof(int))
		return -EINVAL;

	if (get_user(val, (int *)optval))
		return -EFAULT;

	switch(optname) {
	case UDP_CORK:
		if (val != 0) {
			up->corkflag = 1;
		} else {
			up->corkflag = 0;
			lock_sock(sk);
			udp_v6_push_pending_frames(sk, up);
			release_sock(sk);
		}
		break;
		
	case UDP_ENCAP:
		up->encap_type = val;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	};

	return err;
}

static int udpv6_getsockopt(struct sock *sk, int level, int optname, 
			  char *optval, int *optlen)
{
	struct udp_opt *up = udp_sk(sk);
	int val, len;

	if (level != SOL_UDP)
		return ipv6_getsockopt(sk, level, optname, optval, optlen);

	if(get_user(len,optlen))
		return -EFAULT;

	len = min_t(unsigned int, len, sizeof(int));
	
	if(len < 0)
		return -EINVAL;

	switch(optname) {
	case UDP_CORK:
		val = up->corkflag;
		break;

	case UDP_ENCAP:
		val = up->encap_type;
		break;

	default:
		return -ENOPROTOOPT;
	};

  	if(put_user(len, optlen))
  		return -EFAULT;
	if(copy_to_user(optval, &val,len))
		return -EFAULT;
  	return 0;
}

static struct inet6_protocol udpv6_protocol = {
	.handler	=	udpv6_rcv,
	.err_handler	=	udpv6_err,
	.flags		=	INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

/* ------------------------------------------------------------------------ */
#ifdef CONFIG_PROC_FS

static void udp6_sock_seq_show(struct seq_file *seq, struct sock *sp, int bucket)
{
	struct inet_opt *inet = inet_sk(sp);
	struct ipv6_pinfo *np = inet6_sk(sp);
	struct in6_addr *dest, *src;
	__u16 destp, srcp;

	dest  = &np->daddr;
	src   = &np->rcv_saddr;
	destp = ntohs(inet->dport);
	srcp  = ntohs(inet->sport);
	seq_printf(seq,
		   "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X "
		   "%02X %08X:%08X %02X:%08lX %08X %5d %8d %lu %d %p\n",
		   bucket,
		   src->s6_addr32[0], src->s6_addr32[1],
		   src->s6_addr32[2], src->s6_addr32[3], srcp,
		   dest->s6_addr32[0], dest->s6_addr32[1],
		   dest->s6_addr32[2], dest->s6_addr32[3], destp,
		   sp->sk_state, 
		   atomic_read(&sp->sk_wmem_alloc),
		   atomic_read(&sp->sk_rmem_alloc),
		   0, 0L, 0,
		   sock_i_uid(sp), 0,
		   sock_i_ino(sp),
		   atomic_read(&sp->sk_refcnt), sp);
}

static int udp6_seq_show(struct seq_file *seq, void *v)
{
	if (v == (void *)1)
		seq_printf(seq,
			   "  sl  "
			   "local_address                         "
			   "remote_address                        "
			   "st tx_queue rx_queue tr tm->when retrnsmt"
			   "   uid  timeout inode\n");
	else
		udp6_sock_seq_show(seq, v, ((struct udp_iter_state *)seq->private)->bucket);
	return 0;
}

static struct file_operations udp6_seq_fops;
static struct udp_seq_afinfo udp6_seq_afinfo = {
	.owner		= THIS_MODULE,
	.name		= "udp6",
	.family		= AF_INET6,
	.seq_show	= udp6_seq_show,
	.seq_fops	= &udp6_seq_fops,
};

int __init udp6_proc_init(void)
{
	return udp_proc_register(&udp6_seq_afinfo);
}

void udp6_proc_exit(void) {
	udp_proc_unregister(&udp6_seq_afinfo);
}
#endif /* CONFIG_PROC_FS */

/* ------------------------------------------------------------------------ */

struct proto udpv6_prot = {
	.name =		"UDP",
	.close =	udpv6_close,
	.connect =	udpv6_connect,
	.disconnect =	udp_disconnect,
	.ioctl =	udp_ioctl,
	.destroy =	udpv6_destroy_sock,
	.setsockopt =	udpv6_setsockopt,
	.getsockopt =	udpv6_getsockopt,
	.sendmsg =	udpv6_sendmsg,
	.recvmsg =	udpv6_recvmsg,
	.backlog_rcv =	udpv6_queue_rcv_skb,
	.hash =		udp_v6_hash,
	.unhash =	udp_v6_unhash,
	.get_port =	udp_v6_get_port,
};

extern struct proto_ops inet6_dgram_ops;

static struct inet_protosw udpv6_protosw = {
	.type =      SOCK_DGRAM,
	.protocol =  IPPROTO_UDP,
	.prot =      &udpv6_prot,
	.ops =       &inet6_dgram_ops,
	.capability =-1,
	.no_check =  UDP_CSUM_DEFAULT,
	.flags =     INET_PROTOSW_PERMANENT,
};


void __init udpv6_init(void)
{
	if (inet6_add_protocol(&udpv6_protocol, IPPROTO_UDP) < 0)
		printk(KERN_ERR "udpv6_init: Could not register protocol\n");
	inet6_register_protosw(&udpv6_protosw);
}
