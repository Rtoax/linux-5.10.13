/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_IN_ROUTE_H
#define _LINUX_IN_ROUTE_H

/* IPv4 routing cache flags */

#define RTCF_DEAD	RTNH_F_DEAD
#define RTCF_ONLINK	RTNH_F_ONLINK

/* Obsolete flag. About to be deleted */
#define RTCF_NOPMTUDISC RTM_F_NOPMTUDISC

#define RTCF_NOTIFY	0x00010000
#define RTCF_DIRECTDST	0x00020000 /* unused */
#define RTCF_REDIRECTED	0x00040000
#define RTCF_TPROXY	0x00080000 /* unused */

#define RTCF_FAST	0x00200000 /* unused */
#define RTCF_MASQ	0x00400000 /* unused */
#define RTCF_SNAT	0x00800000 /* unused */

/**
 *   设置后，应发送ICMPv4重定向消息作为对传入数据包的响应。
 *   设置这个标志需要满足几个条件，包括输入设备和输出设备相同，
 *   并且设置了相应的procfs send_redirects条目。
 *   还有更多的条件，你将在本章后面看到。此标志在 __mkroute_input() 方法中设置。
 */
#define RTCF_DOREDIRECT 0x01000000
#define RTCF_DIRECTSRC	0x04000000
#define RTCF_DNAT	0x08000000

/**
 *  设置后，目的地址为广播地址。
 *  此标志在 __mkroute_output() 方法和 ip_route_input_slow() 方法中设置。
 */
#define RTCF_BROADCAST	0x10000000

/**
 *  设置后，目的地址为多播地址。
 *  此标志在 ip_route_input_mc() 方法和 __mkroute_output() 方法中设置。
 */
#define RTCF_MULTICAST	0x20000000
#define RTCF_REJECT	0x40000000 /* unused */

/**
 *  设置时，目的地址是本地的。
 *  此标志在以下方法中设置: 
 *      ip_route_input_slow(),
 *      __mkroute_output(),
 *      ip_route_input_mc(),
 *      __ip_route_output_key().
 */
#define RTCF_LOCAL	0x80000000

#define RTCF_NAT	(RTCF_DNAT|RTCF_SNAT)

#define RT_TOS(tos)	((tos)&IPTOS_TOS_MASK)

#endif /* _LINUX_IN_ROUTE_H */
