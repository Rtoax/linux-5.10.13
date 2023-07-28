/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_CMA_H__
#define __MM_CMA_H__

#include <linux/debugfs.h>

/**
 * 连续内存访问 cma，全称（contiguous memory allocation）
 *
 * 在内存初始化时预留一块连续内存，可以在内存碎片化严重时通过调用 dma_alloc_contiguous
 * 接口并且gfp指定为 __GFP_DIRECT_RECLAIM 从预留的那块连续内存中分配大块连续内存。
 */
struct cma {
	/* CMA区域物理地址的起始页帧号 */
	unsigned long   base_pfn;
	/* CMA区域总体的页数 */
	unsigned long   count;
	/* 位图，用于描述页的分配情况 */
	unsigned long   *bitmap;
	/* 位图中每个bit描述的物理页面的order值，其中页面数为2^order值 */
	unsigned int order_per_bit; /* Order of pages represented by one bit */
	struct mutex    lock;
#ifdef CONFIG_CMA_DEBUGFS
	struct hlist_head mem_head;
	spinlock_t mem_head_lock;
	struct debugfs_u32_array dfs_bitmap;
#endif
	char name[CMA_MAX_NAME];
};

extern struct cma cma_areas[MAX_CMA_AREAS];
extern unsigned cma_area_count;

static inline unsigned long cma_bitmap_maxno(struct cma *cma)
{
	return cma->count >> cma->order_per_bit;
}

#endif
