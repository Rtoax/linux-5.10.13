/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* IPv4-specific defines for netfilter. 
 * (C)1998 Rusty Russell -- This code is GPL.
 */
#ifndef _UAPI__LINUX_IP_NETFILTER_H
#define _UAPI__LINUX_IP_NETFILTER_H


#include <linux/netfilter.h>

/* only for userspace compatibility */
#ifndef __KERNEL__

#include <limits.h> /* for INT_MIN, INT_MAX */


/* IP Hooks */
/* After promisc drops, checksum checks. */
/**
 *  这个钩子将在进入网络堆栈后很快被任何传入流量触发。
 *  在做出关于将数据包发送到何处的任何路由决策之前，会处理此挂钩。
 */
#define NF_IP_PRE_ROUTING	0

/* If the packet is destined for this box. */
/**
 *  如果数据包的目的地是本地系统，则在传入数据包被路由后触发此钩子。
 */
#define NF_IP_LOCAL_IN		1

/* If the packet is destined for another interface. */
/**
 *  如果要将数据包转发到另一台主机，则在路由传入数据包后触发此挂钩。
 */
#define NF_IP_FORWARD		2

/* Packets coming from a local process. */
/**
 *  这个钩子会在任何本地创建的出站流量到达网络堆栈时触发。
 */
#define NF_IP_LOCAL_OUT		3

/* Packets about to hit the wire. */
/**
 *  在路由发生之后和就在被放到线路上之前，任何传出或转发的流量都会触发这个钩子。
 */
#define NF_IP_POST_ROUTING	4
#define NF_IP_NUMHOOKS		5


#endif /* ! __KERNEL__ */

enum nf_ip_hook_priorities {
	NF_IP_PRI_FIRST = INT_MIN,
	NF_IP_PRI_RAW_BEFORE_DEFRAG = -450,
	NF_IP_PRI_CONNTRACK_DEFRAG = -400,
	NF_IP_PRI_RAW = -300,
	NF_IP_PRI_SELINUX_FIRST = -225,
	NF_IP_PRI_CONNTRACK = -200,
	NF_IP_PRI_MANGLE = -150,
	NF_IP_PRI_NAT_DST = -100,
	NF_IP_PRI_FILTER = 0,
	NF_IP_PRI_SECURITY = 50,
	NF_IP_PRI_NAT_SRC = 100,
	NF_IP_PRI_SELINUX_LAST = 225,
	NF_IP_PRI_CONNTRACK_HELPER = 300,
	NF_IP_PRI_CONNTRACK_CONFIRM = INT_MAX,
	NF_IP_PRI_LAST = INT_MAX,
};

/* Arguments for setsockopt SOL_IP: */
/* 2.0 firewalling went from 64 through 71 (and +256, +512, etc). */
/* 2.2 firewalling (+ masq) went from 64 through 76 */
/* 2.4 firewalling went 64 through 67. */
#define SO_ORIGINAL_DST 80


#endif /* _UAPI__LINUX_IP_NETFILTER_H */
