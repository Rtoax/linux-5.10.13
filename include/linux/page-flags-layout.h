/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PAGE_FLAGS_LAYOUT_H
#define PAGE_FLAGS_LAYOUT_H

#include <linux/numa.h>
#include <generated/bounds.h>

/*
 * When a memory allocation must conform to specific limitations (such
 * as being suitable for DMA) the caller will pass in hints to the
 * allocator in the gfp_mask, in the zone modifier bits.  These bits
 * are used to select a priority ordered list of memory zones which
 * match the requested limits. See gfp_zone() in include/linux/gfp.h
 */
#define ZONES_SHIFT 3

#ifdef CONFIG_SPARSEMEM
#include <asm/sparsemem.h>

/* SECTION_SHIFT	#bits space required to store a section # */
#define SECTIONS_SHIFT	(MAX_PHYSMEM_BITS - SECTION_SIZE_BITS)

#endif /* CONFIG_SPARSEMEM */

#ifndef BUILD_VDSO32_64
/*
 * page->flags layout:
 *
 * There are five possibilities for how page->flags get laid out.  The first
 * pair is for the normal case without sparsemem. The second pair is for
 * sparsemem when there is plenty of space for node and section information.
 * The last is when there is insufficient space in page->flags and a separate
 * lookup is necessary.
 *
 * No sparsemem or sparsemem vmemmap: |       NODE     | ZONE |             ... | FLAGS |
 *      " plus space for last_cpupid: |       NODE     | ZONE | LAST_CPUPID ... | FLAGS |
 * classic sparse with space for node:| SECTION | NODE | ZONE |             ... | FLAGS |
 *      " plus space for last_cpupid: | SECTION | NODE | ZONE | LAST_CPUPID ... | FLAGS |
 * classic sparse no space for node:  | SECTION |     ZONE    | ... | FLAGS |
 */
/**
   +----------+---------+----------+--------+----------+
   |  section |   node  |   zone   |  ...   |   flag   |
   +----------+---------+----------+--------+----------+
 */
#if defined(CONFIG_SPARSEMEM) && !defined(CONFIG_SPARSEMEM_VMEMMAP)
//#define SECTIONS_WIDTH		SECTIONS_SHIFT
#else
#define SECTIONS_WIDTH		0
#endif

#define ZONES_WIDTH		ZONES_SHIFT /* 3 */

#if SECTIONS_WIDTH/* 0 */+ZONES_WIDTH/* 3 */+NODES_SHIFT/* 10 */ <= BITS_PER_LONG/* 64 */ - NR_PAGEFLAGS/* 27 */
#define NODES_WIDTH		NODES_SHIFT/* 10 */
#else
//#ifdef CONFIG_SPARSEMEM_VMEMMAP
//#error "Vmemmap: No space for nodes field in page flags"
//#endif
//#define NODES_WIDTH		0
#endif

#ifdef CONFIG_NUMA_BALANCING
#define LAST__PID_SHIFT 8
#define LAST__PID_MASK  ((1 << LAST__PID_SHIFT)-1)/* 255 */

#define LAST__CPU_SHIFT NR_CPUS_BITS    /* 13 */
#define LAST__CPU_MASK  ((1 << LAST__CPU_SHIFT)-1)/* 0x7FF=2047 */

#define LAST_CPUPID_SHIFT (LAST__PID_SHIFT/* 8 */+LAST__CPU_SHIFT/* 13 */)/* = 21 */
#else
/*  */
#endif

#ifdef CONFIG_KASAN_SW_TAGS
//#define KASAN_TAG_WIDTH 8
#else
#define KASAN_TAG_WIDTH 0
#endif

#if SECTIONS_WIDTH/* 0 */+ZONES_WIDTH/* 3 */+NODES_SHIFT/*10*/+LAST_CPUPID_SHIFT/* 21 */+KASAN_TAG_WIDTH/* 0 */ \
	<= BITS_PER_LONG/* 64 */ - NR_PAGEFLAGS/* 27 */
#define LAST_CPUPID_WIDTH LAST_CPUPID_SHIFT/* 21 */
#else
//#define LAST_CPUPID_WIDTH 0
#endif

#if SECTIONS_WIDTH+NODES_WIDTH+ZONES_WIDTH+LAST_CPUPID_WIDTH+KASAN_TAG_WIDTH \
	> BITS_PER_LONG - NR_PAGEFLAGS
#error "Not enough bits in page flags"
#endif

/*
 * We are going to use the flags for the page to node mapping if its in
 * there.  This includes the case where there is no node, so it is implicit.
 * Note that this #define MUST have a value so that it can be tested with
 * the IS_ENABLED() macro.
 */
#if !(NODES_WIDTH > 0 || NODES_SHIFT == 0)
//#define NODE_NOT_IN_PAGE_FLAGS 1
#endif

#if defined(CONFIG_NUMA_BALANCING) && LAST_CPUPID_WIDTH == 0
//#define LAST_CPUPID_NOT_IN_PAGE_FLAGS
#endif

#endif
#endif /* _LINUX_PAGE_FLAGS_LAYOUT */
