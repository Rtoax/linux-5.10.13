/* SPDX-License-Identifier: GPL-2.0 */
/* internal file - do not include directly */

#ifdef CONFIG_BPF_JIT
#ifdef CONFIG_INET
#include <net/tcp.h>
/**
 * 静态定义 bpf struct_ops 所支持的结构
 *
 * linux v6.7 已经改为动态注册了，见 [1]
 *
 * [1] linux commit f6be98d19985 ("bpf, net: switch to dynamic registration")
 */
BPF_STRUCT_OPS_TYPE(tcp_congestion_ops)
#endif
#endif
