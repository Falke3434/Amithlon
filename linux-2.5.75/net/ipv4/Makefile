#
# Makefile for the Linux TCP/IP (INET) layer.
#

obj-y     := utils.o route.o inetpeer.o protocol.o \
	     ip_input.o ip_fragment.o ip_forward.o ip_options.o \
	     ip_output.o ip_sockglue.o \
	     tcp.o tcp_input.o tcp_output.o tcp_timer.o tcp_ipv4.o tcp_minisocks.o \
	     tcp_diag.o raw.o udp.o arp.o icmp.o devinet.o af_inet.o igmp.o \
	     sysctl_net_ipv4.o fib_frontend.o fib_semantics.o fib_hash.o

obj-$(CONFIG_PROC_FS) += proc.o
obj-$(CONFIG_IP_MULTIPLE_TABLES) += fib_rules.o
obj-$(CONFIG_IP_ROUTE_NAT) += ip_nat_dumb.o
obj-$(CONFIG_IP_MROUTE) += ipmr.o
obj-$(CONFIG_NET_IPIP) += ipip.o
obj-$(CONFIG_NET_IPGRE) += ip_gre.o
obj-$(CONFIG_SYN_COOKIES) += syncookies.o
obj-$(CONFIG_INET_AH) += ah4.o
obj-$(CONFIG_INET_ESP) += esp4.o
obj-$(CONFIG_INET_IPCOMP) += ipcomp.o
obj-$(CONFIG_IP_PNP) += ipconfig.o
obj-$(CONFIG_NETFILTER)	+= netfilter/

obj-y += xfrm4_policy.o xfrm4_state.o xfrm4_input.o xfrm4_tunnel.o
