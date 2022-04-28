/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MEMORY_MODEL_H
#define __ASM_MEMORY_MODEL_H

#include <linux/pfn.h>

#ifndef __ASSEMBLY__

/**
 * @brief 平坦内存
 *
 */
#if defined(CONFIG_FLATMEM)

#ifndef ARCH_PFN_OFFSET
#define ARCH_PFN_OFFSET		(0UL)
#endif

/**
 * @brief 不连续内存
 *
 */
#elif defined(CONFIG_DISCONTIGMEM)

#ifndef arch_pfn_to_nid
#define arch_pfn_to_nid(pfn)	pfn_to_nid(pfn)
#endif

#ifndef arch_local_page_offset
#define arch_local_page_offset(pfn, nid)	\
	((pfn) - NODE_DATA(nid)->node_start_pfn)
#endif

#endif /* CONFIG_DISCONTIGMEM */

/**
 * supports 3 memory models.  平坦内存
 *
 * 这可能要考虑到 分段 和 分页，如果 整个物理内存可以分成一个 段，那么就可以认为是平坦模型
 */
#if defined(CONFIG_FLATMEM)

#define __pfn_to_page(pfn)	(mem_map + ((pfn) - ARCH_PFN_OFFSET))
#define __page_to_pfn(page)	((unsigned long)((page) - mem_map) + \
				 ARCH_PFN_OFFSET)

/**
 * @brief 不连续内存
 *
 */
#elif defined(CONFIG_DISCONTIGMEM)

#define __pfn_to_page(pfn)			\
({	unsigned long __pfn = (pfn);		\
	unsigned long __nid = arch_pfn_to_nid(__pfn);  \
	NODE_DATA(__nid)->node_mem_map + arch_local_page_offset(__pfn, __nid);\
})

#define __page_to_pfn(pg)						\
({	const struct page *__pg = (pg);					\
	struct pglist_data *__pgdat = NODE_DATA(page_to_nid(__pg));	\
	(unsigned long)(__pg - __pgdat->node_mem_map) +			\
	 __pgdat->node_start_pfn;					\
})

/**
 * @brief 稀疏内存 (默认配置)
 *
 * $ cat /boot/config-5.14.0-75.el9.x86_64 | grep CONFIG_SPA
 * CONFIG_SPARSE_IRQ=y
 * CONFIG_SPARSEMEM_MANUAL=y
 * CONFIG_SPARSEMEM=y
 * CONFIG_SPARSEMEM_EXTREME=y
 * CONFIG_SPARSEMEM_VMEMMAP_ENABLE=y
 * CONFIG_SPARSEMEM_VMEMMAP=y
 *
 */
#elif defined(CONFIG_SPARSEMEM_VMEMMAP)

/**
 *  memmap is virtually contiguous.
 *
 *  稀疏内存 SPARSEMEM中`__pfn_to_page`和`__page_to_pfn`的实现如下：
 *
 * arch/arm64/include/asm/pgtable.h
 * #define vmemmap ((struct page *)VMEMMAP_START - (memstart_addr >> PAGE_SHIFT))
 *
 * arch/x86/include/asm/pgtable_64.h
 * #define vmemmap ((struct page *)VMEMMAP_START)
 */
#define __pfn_to_page(pfn)	(vmemmap + (pfn))
#define __page_to_pfn(page)	(unsigned long)((page) - vmemmap)

#elif defined(CONFIG_SPARSEMEM)
/*
 * Note: section's mem_map is encoded to reflect its start_pfn.
 * section[i].section_mem_map == mem_map's address - start_pfn;
 */
#define __page_to_pfn(pg)					\
({	const struct page *__pg = (pg);				\
	int __sec = page_to_section(__pg);			\
	(unsigned long)(__pg - __section_mem_map_addr(__nr_to_section(__sec)));	\
})

#define __pfn_to_page(pfn)				\
({	unsigned long __pfn = (pfn);			\
	struct mem_section *__sec = __pfn_to_section(__pfn);	\
	__section_mem_map_addr(__sec) + __pfn;		\
})
#endif /* CONFIG_FLATMEM/DISCONTIGMEM/SPARSEMEM */

/*
 * Convert a physical address to a Page Frame Number and back
 */
#define	__phys_to_pfn(paddr)	PHYS_PFN(paddr)
#define	__pfn_to_phys(pfn)	PFN_PHYS(pfn)

/**
 * 根据给出页地址求出对应的页帧号。
 * 两个结构相减，得出的是两者之间的对象个数，
 * 加上起始帧号偏移，即给出页地址的相对绝对页号。
 */
#define page_to_pfn __page_to_pfn


/**
 * 根据给出的页帧号计算出对应的页地址。
 * 页基地址加上页帧号是相对偏移的页地址，
 * 减去一个偏移页帧号即页对象对应的地址。
 */
#define pfn_to_page __pfn_to_page

#endif /* __ASSEMBLY__ */

#endif
