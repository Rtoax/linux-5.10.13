/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PAGE_DEFS_H
#define _ASM_X86_PAGE_DEFS_H

#include <linux/const.h>
#include <linux/types.h>
#include <linux/mem_encrypt.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT		12
#define PAGE_SIZE		(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1))

#define PMD_PAGE_SIZE		(_AC(1, UL) << PMD_SHIFT)   /* 2兆字节 */
#define PMD_PAGE_MASK		(~(PMD_PAGE_SIZE-1))

#define PUD_PAGE_SIZE		(_AC(1, UL) << PUD_SHIFT)
#define PUD_PAGE_MASK		(~(PUD_PAGE_SIZE-1))

#define __VIRTUAL_MASK		((1UL << __VIRTUAL_MASK_SHIFT) - 1)

/* Cast *PAGE_MASK to a signed type so that it is sign-extended if
   virtual addresses are 32-bits but physical addresses are larger
   (ie, 32-bit PAE). */
#define PHYSICAL_PAGE_MASK	(((signed long)PAGE_MASK) & __PHYSICAL_MASK/* 0xffff ffff ffff f000 */)
#define PHYSICAL_PMD_PAGE_MASK	(((signed long)PMD_PAGE_MASK) & __PHYSICAL_MASK)
#define PHYSICAL_PUD_PAGE_MASK	(((signed long)PUD_PAGE_MASK) & __PHYSICAL_MASK)

#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1,UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

#define HUGE_MAX_HSTATE 2

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)/* 所有对物理地址进行直接映射后的虚拟基地址 */

#define VM_DATA_DEFAULT_FLAGS	VM_DATA_FLAGS_TSK_EXEC

#define __PHYSICAL_START /*  */	ALIGN(CONFIG_PHYSICAL_START, \
				      CONFIG_PHYSICAL_ALIGN)

#define __START_KERNEL		(__START_KERNEL_map + __PHYSICAL_START) /*  */
//Linux内核的基本物理地址- 0x1000000;
//Linux内核的基本虚拟地址- 0xffffffff81000000

                            /* 0xffffffff80000000 + 0x1000000 = 0xffffffff81000000 */
//       +-----------+-----------------+---------------+------------------+
//       |           |                 |               |                  |
//       |kernel text|      kernel     |               |    vsyscalls     |
//       | mapping   |       text      |    Modules    |    fix-mapped    |
//       |from phys 0|       data      |               |    addresses     |
//       |           |                 |               |                  |
//       +-----------+-----------------+---------------+------------------+
//__START_KERNEL_map   __START_KERNEL    MODULES_VADDR            0xffffffffffffffff

#ifdef CONFIG_X86_64
#include <asm/page_64_types.h>
#define IOREMAP_MAX_ORDER       (PUD_SHIFT)
#else
/*  */
#endif	/* CONFIG_X86_64 */

#ifndef __ASSEMBLY__

#ifdef CONFIG_DYNAMIC_PHYSICAL_MASK
extern phys_addr_t physical_mask;
#define __PHYSICAL_MASK		physical_mask   /* 0xffff ffff ffff f000 */
#else
//#define __PHYSICAL_MASK		((phys_addr_t)((1ULL << __PHYSICAL_MASK_SHIFT) - 1))
#endif

extern int devmem_is_allowed(unsigned long pagenr);

extern unsigned long max_low_pfn_mapped;
extern unsigned long max_pfn_mapped;

static inline phys_addr_t get_max_mapped(void)
{
	return (phys_addr_t)max_pfn_mapped << PAGE_SHIFT;
}

bool pfn_range_is_mapped(unsigned long start_pfn, unsigned long end_pfn);

extern void initmem_init(void);

#endif	/* !__ASSEMBLY__ */

#endif	/* _ASM_X86_PAGE_DEFS_H */
