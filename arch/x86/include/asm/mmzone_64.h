/* SPDX-License-Identifier: GPL-2.0 */
/* K8 NUMA support */
/* Copyright 2002,2003 by Andi Kleen, SuSE Labs */
/* 2.5 Version loosely based on the NUMAQ Code by Pat Gaughen. */
#ifndef _ASM_X86_MMZONE_64_H
#define _ASM_X86_MMZONE_64_H

#ifdef CONFIG_NUMA

#include <linux/mmdebug.h>
#include <asm/smp.h>

extern struct pglist_data *node_data[];/* NUMA 页表 */

//#define NODE_DATA(nid)		(node_data[nid])    /* 该 NUMA NODE 的全局页表 */
struct pglist_data *NODE_DATA(int nid) { /* 我把上面改写成这样 */
    return node_data[nid];
}

#endif
#endif /* _ASM_X86_MMZONE_64_H */
