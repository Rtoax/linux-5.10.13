/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/linux/kmemleak.h
 *
 * Copyright (C) 2008 ARM Limited
 * Written by Catalin Marinas <catalin.marinas@arm.com>
 */

#ifndef __KMEMLEAK_H
#define __KMEMLEAK_H

#include <linux/slab.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_DEBUG_KMEMLEAK

extern void __init kmemleak_init(void) ;
extern void __ref kmemleak_alloc(const void *ptr, size_t size, int min_count,
			   gfp_t gfp) ;
extern void __ref kmemleak_alloc_percpu(const void __percpu *ptr, size_t size,
				  gfp_t gfp) ;
extern void __ref kmemleak_vmalloc(const struct vm_struct *area, size_t size,
			     gfp_t gfp) ;
extern void __ref kmemleak_free(const void *ptr) ;
extern void __ref kmemleak_free_part(const void *ptr, size_t size) ;
extern void __ref kmemleak_free_percpu(const void __percpu *ptr) ;
extern void __ref kmemleak_update_trace(const void *ptr) ;
extern void __ref kmemleak_not_leak(const void *ptr) ;
extern void __ref kmemleak_ignore(const void *ptr) ;
extern void __ref kmemleak_scan_area(const void *ptr, size_t size, gfp_t gfp) ;
extern void __ref kmemleak_no_scan(const void *ptr) ;
extern void __ref kmemleak_alloc_phys(phys_addr_t phys, size_t size, int min_count,
				gfp_t gfp) ;
extern void __ref kmemleak_free_part_phys(phys_addr_t phys, size_t size) ;
extern void __ref kmemleak_not_leak_phys(phys_addr_t phys) ;
extern void __ref kmemleak_ignore_phys(phys_addr_t phys) ;

static inline void kmemleak_alloc_recursive(const void *ptr, size_t size,
					    int min_count, slab_flags_t flags,
					    gfp_t gfp)
{
	if (!(flags & SLAB_NOLEAKTRACE))
		kmemleak_alloc(ptr, size, min_count, gfp);
}

static inline void kmemleak_free_recursive(const void *ptr, slab_flags_t flags)
{
	if (!(flags & SLAB_NOLEAKTRACE))
		kmemleak_free(ptr);
}

static inline void kmemleak_erase(void **ptr)
{
	*ptr = NULL;
}

#else

#endif	/* CONFIG_DEBUG_KMEMLEAK */

#endif	/* __KMEMLEAK_H */
