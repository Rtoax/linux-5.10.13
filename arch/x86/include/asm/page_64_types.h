/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PAGE_64_DEFS_H
#define _ASM_X86_PAGE_64_DEFS_H

#ifndef __ASSEMBLY__
#include <asm/kaslr.h>
#endif

#ifdef CONFIG_KASAN
#define KASAN_STACK_ORDER 1
#else
/*  */
#endif

//`PAGE_SIZE` is `(_AC(1,UL) << PAGE_SHIFT)` or `4096` bytes 
//`THREAD_SIZE` is `(PAGE_SIZE << THREAD_SIZE_ORDER)` or `16384` bytes for the `x86_64`
#define THREAD_SIZE_ORDER	(2 + KASAN_STACK_ORDER/* 1 */)
//每一个活动的线程在 Linux 内核中都有一个很大的栈
#define THREAD_SIZE  (PAGE_SIZE << THREAD_SIZE_ORDER)/* 栈大小， 32K/16K */

#define EXCEPTION_STACK_ORDER (0 + KASAN_STACK_ORDER)
#define EXCEPTION_STKSZ (PAGE_SIZE << EXCEPTION_STACK_ORDER)

//每一个 CPU 也有一个特殊的 per-cpu 栈。首先是给外部中断使用的 `中断栈（interrupt stack）`
#define IRQ_STACK_ORDER (2 + KASAN_STACK_ORDER)
#define IRQ_STACK_SIZE (PAGE_SIZE << IRQ_STACK_ORDER) /* 16KB */

/*
 * The index for the tss.ist[] array. The hardware limit is 7 entries.
 */ /* IST(Interrupt Stack Table) */
#define	IST_INDEX_DF		0
#define	IST_INDEX_NMI		1
#define	IST_INDEX_DB		2   /* Debug, debug exception */
#define	IST_INDEX_MCE		3
#define	IST_INDEX_VC		4

/*
 * Set __PAGE_OFFSET to the most negative possible address +
 * PGDIR_SIZE*17 (pgd slot 273).
 *
 * The gap is to allow a space for LDT remap for PTI (1 pgd slot) and space for
 * a hypervisor (16 slots). Choosing 16 slots for a hypervisor is arbitrary,
 * but it's what Xen requires.
 */
#define __PAGE_OFFSET_BASE_L5	_AC(0xff11000000000000, UL)
#define __PAGE_OFFSET_BASE_L4	_AC(0xffff888000000000, UL)

#ifdef CONFIG_DYNAMIC_MEMORY_LAYOUT
#define __PAGE_OFFSET           page_offset_base
#else
//#define __PAGE_OFFSET           __PAGE_OFFSET_BASE_L4
#endif /* CONFIG_DYNAMIC_MEMORY_LAYOUT */

#define __START_KERNEL_map	_AC(0xffffffff80000000, UL)/* kernel 起始地址 */
//       +-----------+-----------------+---------------+------------------+
//       |           |                 |               |                  |
//       |kernel text|      kernel     |               |    vsyscalls     |
//       | mapping   |       text      |    Modules    |    fix-mapped    |
//       |from phys 0|       data      |               |    addresses     |
//       |           |                 |               |                  |
//       +-----------+-----------------+---------------+------------------+
//__START_KERNEL_map   __START_KERNEL    MODULES_VADDR            0xffffffffffffffff

/* See Documentation/x86/x86_64/mm.rst for a description of the memory map. */

#define __PHYSICAL_MASK_SHIFT	52

#ifdef CONFIG_X86_5LEVEL
#define __VIRTUAL_MASK_SHIFT	(pgtable_l5_enabled() ? 56 : 47)
#else
#define __VIRTUAL_MASK_SHIFT	47
#endif

/*
 * User space process size.  This is the first address outside the user range.
 * There are a few constraints that determine this:
 *
 * On Intel CPUs, if a SYSCALL instruction is at the highest canonical
 * address, then that syscall will enter the kernel with a
 * non-canonical return address, and SYSRET will explode dangerously.
 * We avoid this particular problem by preventing anything executable
 * from being mapped at the maximum canonical address.
 *
 * On AMD CPUs in the Ryzen family, there's a nasty bug in which the
 * CPUs malfunction if they execute code from the highest canonical page.
 * They'll speculate right off the end of the canonical space, and
 * bad things happen.  This is worked around in the same way as the
 * Intel problem.
 *
 * With page table isolation enabled, we map the LDT in ... [stay tuned]
 */
#define TASK_SIZE_MAX	((_AC(1,UL) << __VIRTUAL_MASK_SHIFT) - PAGE_SIZE)
    /*(1UL << 47)   0x0000 8000 0000 0000 
       PAGE_SIZE    0x0000 0000 0000 1000
        =           0x0000 7fff ffff f000 */
#define DEFAULT_MAP_WINDOW	((1UL << 47) - PAGE_SIZE)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define IA32_PAGE_OFFSET	((current->personality & ADDR_LIMIT_3GB) ? \
					0xc0000000 : 0xFFFFe000)

#define TASK_SIZE_LOW		(test_thread_flag(TIF_ADDR32) ? \
					IA32_PAGE_OFFSET : DEFAULT_MAP_WINDOW)
/*  */
#define TASK_SIZE		(test_thread_flag(TIF_ADDR32) ? \
					IA32_PAGE_OFFSET : TASK_SIZE_MAX)
#define TASK_SIZE_OF(child)	((test_tsk_thread_flag(child, TIF_ADDR32)) ? \
					IA32_PAGE_OFFSET : TASK_SIZE_MAX)

#define STACK_TOP		TASK_SIZE_LOW
#define STACK_TOP_MAX		TASK_SIZE_MAX

/*
 * Maximum kernel image size is limited to 1 GiB, due to the fixmap living
 * in the next 1 GiB (see level2_kernel_pgt in arch/x86/kernel/head_64.S).
 *
 * On KASLR use 1 GiB by default, leaving 1 GiB for modules once the
 * page tables are fully set up.
 *
 * If KASLR is disabled we can shrink it to 0.5 GiB and increase the size
 * of the modules area to 1.5 GiB.
 */
#ifdef CONFIG_RANDOMIZE_BASE
#define KERNEL_IMAGE_SIZE	(1024 * 1024 * 1024)
#else
//#define KERNEL_IMAGE_SIZE	(512 * 1024 * 1024)
#endif

#endif /* _ASM_X86_PAGE_64_DEFS_H */
