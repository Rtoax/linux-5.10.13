/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__LINUX_NETFILTER_H
#define _UAPI__LINUX_NETFILTER_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/in.h>
#include <linux/in6.h>

/* Responses from hook functions. */
#define NF_DROP 0   /* 丢弃数据包 */
#define NF_ACCEPT 1 /* 数据包像通常那样继续在内核网络栈中传输 */
#define NF_STOLEN 2 /* 数据包不继续传输，由钩子方法进行处理 */
#define NF_QUEUE 3  /* 数据包将排序，供用户空间使用 */
#define NF_REPEAT 4 /* 再次调用钩子函数 */
#define NF_STOP 5	/* Deprecated, for userspace nf_queue compatibility. */
#define NF_MAX_VERDICT NF_STOP

/* we overload the higher bits for encoding auxiliary data such as the queue
 * number or errno values. Not nice, but better than additional function
 * arguments. */
#define NF_VERDICT_MASK 0x000000ff

/* extra verdict flags have mask 0x0000ff00 */
#define NF_VERDICT_FLAG_QUEUE_BYPASS	0x00008000

/* queue number (NF_QUEUE) or errno (NF_DROP) */
#define NF_VERDICT_QMASK 0xffff0000
#define NF_VERDICT_QBITS 16

#define NF_QUEUE_NR(x) ((((x) << 16) & NF_VERDICT_QMASK) | NF_QUEUE)

#define NF_DROP_ERR(x) (((-x) << 16) | NF_DROP)

/* only for userspace compatibility */
#ifndef __KERNEL__

/* NF_VERDICT_BITS should be 8 now, but userspace might break if this changes */
#define NF_VERDICT_BITS 16
#endif

enum nf_inet_hooks {
    /**
     *  在 IPv4 中，这个挂接点位于 方法 ip_rcv() 中，而在 IPv6中，它位于方法 ipv6_rcv() 中。
     *  所有入栈数据包遇到的第一个挂载点，它处于路由子系统查找之前
     */
	NF_INET_PRE_ROUTING,
    /**
     *  在 IPv4 中，这个挂载点位于 ip_local_deliver(),IPv6 位于 ip6_input()
     *  对于 所有发送给当前主机的入栈数据包，经过挂接点 NF_INET_PRE_ROUTING 并执行路由子系统查找后
     *  都将到达这个挂接点
     */
	NF_INET_LOCAL_IN,
    /**
     *  IPv4 中，这个挂接点位于 ip_forward() ,IPv6 位于 ip6_forward()
     *  对于所有要转发的数据包，经过挂接点 NF_INET_PRE_ROUTING 并执行路由选择子系统查找后，
     *  都将到达这个挂接点
     */
	NF_INET_FORWARD,
    /**
     *  
     *  
     */
	NF_INET_LOCAL_OUT,
    /**
     *  在 IPv4 中，这个关节点位于 ip_output() 中，IPv6 中位于方法 ip6_finish_output2() 中，
     *  所有要转发的数据包都在经过挂接点 NF_INET_FORWARD 后到达这个挂接点，
     *  另外，当前主机生成的数据包经过挂接点 NF_INET_LOCAL_OUT 后将到达这个挂接点。
     */
	NF_INET_POST_ROUTING,
    /**
     *  
     *  
     */
	NF_INET_NUMHOOKS,
    /**
     *  
     *  
     */
	NF_INET_INGRESS = NF_INET_NUMHOOKS,
};

enum nf_dev_hooks {
	NF_NETDEV_INGRESS,
	NF_NETDEV_NUMHOOKS
};

enum {
	NFPROTO_UNSPEC =  0,
	NFPROTO_INET   =  1,
	NFPROTO_IPV4   =  2,
	NFPROTO_ARP    =  3,
	NFPROTO_NETDEV =  5,
	NFPROTO_BRIDGE =  7,
	NFPROTO_IPV6   = 10,
	NFPROTO_DECNET = 12,
	NFPROTO_NUMPROTO,
};

union nf_inet_addr {
	__u32		all[4];
	__be32		ip;
	__be32		ip6[4];
	struct in_addr	in;
	struct in6_addr	in6;
};

#endif /* _UAPI__LINUX_NETFILTER_H */
