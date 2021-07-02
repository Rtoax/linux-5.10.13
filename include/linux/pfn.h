/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PFN_H_
#define _LINUX_PFN_H_

#ifndef __ASSEMBLY__
#include <linux/types.h>

/*
 * pfn_t: encapsulates a page-frame number that is optionally backed
 * by memmap (struct page).  Whether a pfn_t has a 'struct page'
 * backing is indicated by flags in the high bits of the value.
 */
typedef struct {
	u64 val;
} pfn_t;
#endif

/**
 *  (x + 0xfff) & 0xffffffffffff000
 *  
 */
#define PFN_ALIGN(x)	(((unsigned long)(x) + (PAGE_SIZE - 1)) & PAGE_MASK)

/**
 *  物理地址转化为页帧号(右移 12bit`页大小`)
 *  
 *  (x + 0xfff) >> 12
 *  
 */
#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)

/**
 *  物理地址转化为页帧号(右移 12bit`页大小`)
 *  
 *  (x) >> 12
 */
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)

/**
 *  页帧号转化为 物理地址 (左移 12bit`页大小`)
 *  
 *  x << 12
 */
#define PFN_PHYS(x)	((phys_addr_t)(x) << PAGE_SHIFT)
#define PHYS_PFN(x)	((unsigned long)((x) >> PAGE_SHIFT))

#endif
