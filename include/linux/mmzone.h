/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifndef __ASSEMBLY__
#ifndef __GENERATING_BOUNDS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/numa.h>
#include <linux/init.h>
#include <linux/seqlock.h>
#include <linux/nodemask.h>
#include <linux/pageblock-flags.h>
#include <linux/page-flags-layout.h>
#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <asm/page.h>

/* Free memory management - zoned buddy allocator.  */
#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 11
#else
//#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif
#define MAX_ORDER_NR_PAGES (1 << (MAX_ORDER/* 11 */ - 1))/* 1024 */

/*
 * PAGE_ALLOC_COSTLY_ORDER is the order at which allocations are deemed
 * costly to service.  That is between allocation orders which should
 * coalesce naturally under reasonable reclaim pressure and those which
 * will not.
 */
#define PAGE_ALLOC_COSTLY_ORDER 3

enum migratetype {/* page 迁移类型 */
	MIGRATE_UNMOVABLE,  /* 不可迁移 */
	MIGRATE_MOVABLE,    /* 可迁移 */
	MIGRATE_RECLAIMABLE,/* 可回收 */
	MIGRATE_PCPTYPES,	/* per CPU 页 set *//* the number of types on the pcp lists */
	MIGRATE_HIGHATOMIC = MIGRATE_PCPTYPES,
#ifdef CONFIG_CMA   /* 连续内存管理 */
	/*
	 * MIGRATE_CMA migration type is designed to mimic the way
	 * ZONE_MOVABLE works.  Only movable pages can be allocated
	 * from MIGRATE_CMA pageblocks and page allocator never
	 * implicitly change migration type of MIGRATE_CMA pageblock.
	 *
	 * The way to use it is to change migratetype of a range of
	 * pageblocks to MIGRATE_CMA which can be done by
	 * __free_pageblock_cma() function.  What is important though
	 * is that a range of pageblocks must be aligned to
	 * MAX_ORDER_NR_PAGES should biggest page be bigger then
	 * a single pageblock.
	 */
	MIGRATE_CMA,    /* 连续内存区管理 */
#endif
#ifdef CONFIG_MEMORY_ISOLATION
	MIGRATE_ISOLATE,	/* can't allocate from here */
#endif
	MIGRATE_TYPES   /*  */
};

/* In mm/page_alloc.c; keep in sync also with show_migration_types() there */
extern const char * const migratetype_names[MIGRATE_TYPES];

#ifdef CONFIG_CMA   /* CMA 连续内存管理 */
#  define is_migrate_cma(migratetype) unlikely((migratetype) == MIGRATE_CMA)    /* 连续内存管理 */
#  define is_migrate_cma_page(_page) (get_pageblock_migratetype(_page) == MIGRATE_CMA)
#else
/*  */
#endif

static inline bool is_migrate_movable(int mt)
{
	return is_migrate_cma(mt) || mt == MIGRATE_MOVABLE;
}

#define for_each_migratetype_order(order, type) \
	for (order = 0; order < MAX_ORDER; order++) \
		for (type = 0; type < MIGRATE_TYPES; type++)

extern int page_group_by_mobility_disabled;

#define MIGRATETYPE_MASK ((1UL << PB_migratetype_bits) - 1)

#define get_pageblock_migratetype(page)		/* 连续内存管理  */			\
	get_pfnblock_flags_mask(page, page_to_pfn(page), MIGRATETYPE_MASK)/*  */

struct free_area {  /* 每个zone中都有 MAX_ORDER 个此数据结构 */
	struct list_head	free_list[MIGRATE_TYPES];   /*  page 链表头 */
	unsigned long		nr_free;
};

static inline struct page *get_page_from_free_area(struct free_area *area,
					    int migratetype)
{
    /* 获取  freelist链表的第一个 节点 entry */
	return list_first_entry_or_null(&area->free_list[migratetype],
					struct page, lru);
}

static inline bool free_area_empty(struct free_area *area, int migratetype)
{
	return list_empty(&area->free_list[migratetype]);
}

struct pglist_data;

/*
 * zone->lock and the zone lru_lock are two of the hottest locks in the kernel.
 * So add a wild amount of padding here to ensure that they fall into separate
 * cachelines.  There are very few zone structures in the machine, so space
 * consumption is not a concern here.
 */
#if defined(CONFIG_SMP)
struct zone_padding {
	char x[0];
} ____cacheline_internodealigned_in_smp;
#define ZONE_PADDING(name)	struct zone_padding name;
#else
/*  */
#endif

#ifdef CONFIG_NUMA
enum numa_stat_item {
	NUMA_HIT,		/* allocated in intended node */
	NUMA_MISS,		/* allocated in non intended node */
	NUMA_FOREIGN,		/* was intended here, hit elsewhere */
	NUMA_INTERLEAVE_HIT,	/* interleaver preferred this zone */
	NUMA_LOCAL,		/* allocation from local node */
	NUMA_OTHER,		/* allocation from other node */
	NR_VM_NUMA_STAT_ITEMS
};
#else
/*  */
#endif

/**
 *  
 */
enum zone_stat_item {   /* ZONE 状态 */
	/* First 128 byte cacheline (assuming 64 bit words) */
	NR_FREE_PAGES,      /*  */

    /**
     *  
     */
    NR_ZONE_LRU_BASE, /* Used only for compaction and reclaim retry */
	NR_ZONE_INACTIVE_ANON = NR_ZONE_LRU_BASE,
	NR_ZONE_ACTIVE_ANON,
	NR_ZONE_INACTIVE_FILE,
	NR_ZONE_ACTIVE_FILE,
	NR_ZONE_UNEVICTABLE,
	NR_ZONE_WRITE_PENDING,	/* Count of dirty, writeback and unstable pages */
	
	NR_MLOCK,		/* mlock()ed pages found and moved off LRU */
	NR_PAGETABLE,		/* used for pagetables */
	/* Second 128 byte cacheline */
	NR_BOUNCE,
#if IS_ENABLED(CONFIG_ZSMALLOC)
	NR_ZSPAGES,		/* allocated in zsmalloc */
#endif
	NR_FREE_CMA_PAGES,  /* 连续内存管理 */
	NR_VM_ZONE_STAT_ITEMS /* 所有的 ZONE stat */};

enum node_stat_item {
	NR_LRU_BASE,
	NR_INACTIVE_ANON = NR_LRU_BASE, /* must match order of LRU_[IN]ACTIVE */
	NR_ACTIVE_ANON,		/*  "     "     "   "       "         */
	NR_INACTIVE_FILE,	/*  "     "     "   "       "         */
	NR_ACTIVE_FILE,		/*  "     "     "   "       "         */
	NR_UNEVICTABLE,		/*  "     "     "   "       "         */
	NR_SLAB_RECLAIMABLE_B,  /* slab 可回收 */
	NR_SLAB_UNRECLAIMABLE_B,
	NR_ISOLATED_ANON,	/* Temporary isolated pages from anon lru */
	NR_ISOLATED_FILE,	/* Temporary isolated pages from file lru */

    /**
     *  
     */
	WORKINGSET_NODES,
	WORKINGSET_REFAULT_BASE,
	WORKINGSET_REFAULT_ANON = WORKINGSET_REFAULT_BASE,
	WORKINGSET_REFAULT_FILE,
	WORKINGSET_ACTIVATE_BASE,
	WORKINGSET_ACTIVATE_ANON = WORKINGSET_ACTIVATE_BASE,
	WORKINGSET_ACTIVATE_FILE,
	WORKINGSET_RESTORE_BASE,
	WORKINGSET_RESTORE_ANON = WORKINGSET_RESTORE_BASE,
	WORKINGSET_RESTORE_FILE,
	WORKINGSET_NODERECLAIM,

    
	NR_ANON_MAPPED,	/* Mapped anonymous pages */
	NR_FILE_MAPPED,	/* pagecache pages mapped into pagetables.
			   only modified from process context */
	NR_FILE_PAGES,
	NR_FILE_DIRTY,
	NR_WRITEBACK,
	NR_WRITEBACK_TEMP,	/* Writeback using temporary buffers */
	NR_SHMEM,		/* shmem pages (included tmpfs/GEM pages) */
	NR_SHMEM_THPS,
	NR_SHMEM_PMDMAPPED,
	NR_FILE_THPS,
	NR_FILE_PMDMAPPED,
	NR_ANON_THPS,
	NR_VMSCAN_WRITE,
	NR_VMSCAN_IMMEDIATE,	/* Prioritise for reclaim when writeback ends */
	NR_DIRTIED,		/* page dirtyings since bootup */
	NR_WRITTEN,		/* page writings since bootup */
	NR_KERNEL_MISC_RECLAIMABLE,	/* reclaimable non-slab kernel pages */
	NR_FOLL_PIN_ACQUIRED,	/* via: pin_user_page(), gup flag: FOLL_PIN */
	NR_FOLL_PIN_RELEASED,	/* pages returned via unpin_user_page() */
	NR_KERNEL_STACK_KB,	/* measured in KiB */
#if IS_ENABLED(CONFIG_SHADOW_CALL_STACK)
	NR_KERNEL_SCS_KB,	/* measured in KiB */
#endif
	NR_VM_NODE_STAT_ITEMS
};

/*
 * Returns true if the value is measured in bytes (most vmstat values are
 * measured in pages). This defines the API part, the internal representation
 * might be different.
 */
static __always_inline bool vmstat_item_in_bytes(int idx)
{
	/*
	 * Global and per-node slab counters track slab pages.
	 * It's expected that changes are multiples of PAGE_SIZE.
	 * Internally values are stored in pages.
	 *
	 * Per-memcg and per-lruvec counters track memory, consumed
	 * by individual slab objects. These counters are actually
	 * byte-precise.
	 */
	return (idx == NR_SLAB_RECLAIMABLE_B ||
		idx == NR_SLAB_UNRECLAIMABLE_B);
}

/*
 * We do arithmetic on the LRU lists in various places in the code,
 * so it is important to keep the active lists LRU_ACTIVE higher in
 * the array than the corresponding inactive lists, and to keep
 * the *_FILE lists LRU_FILE higher than the corresponding _ANON lists.
 *
 * This has to be kept in sync with the statistics in zone_stat_item
 * above and the descriptions in vmstat_text in mm/vmstat.c
 */
#define LRU_BASE 0
#define LRU_ACTIVE 1
#define LRU_FILE 2

/**
 *  用于页面回收的 最近最少使用 枚举
 *
 * pglist_data.lruvec.lists[NR_LRU_LISTS]
 */
enum lru_list { /* 最近最少使用 list */
	LRU_INACTIVE_ANON   /* 不活跃的匿名页面 */ = LRU_BASE,   
	LRU_ACTIVE_ANON     /* 活跃的匿名页面 */= LRU_BASE + LRU_ACTIVE,
	LRU_INACTIVE_FILE   /* 不活跃的文件映射页面 */= LRU_BASE + LRU_FILE,
	LRU_ACTIVE_FILE     /* 活跃的文件映射页面 */= LRU_BASE + LRU_FILE + LRU_ACTIVE,
	LRU_UNEVICTABLE,    /* 不可驱逐, 见 AS_UNEVICTABLE */
	NR_LRU_LISTS
};

#define for_each_lru(lru) for (lru = 0; lru < NR_LRU_LISTS; lru++)

#define for_each_evictable_lru(lru) for (lru = 0; lru <= LRU_ACTIVE_FILE; lru++)

static inline bool is_file_lru(enum lru_list lru)
{
	return (lru == LRU_INACTIVE_FILE || lru == LRU_ACTIVE_FILE);
}

static inline bool is_active_lru(enum lru_list lru)
{
	return (lru == LRU_ACTIVE_ANON || lru == LRU_ACTIVE_FILE);
}

#define ANON_AND_FILE 2

enum lruvec_flags {
	LRUVEC_CONGESTED,		/* lruvec has many dirty pages
					 * backed by a congested BDI
					 */
};

/**
 *  最近最少使用 链表，用于页面回收
 */
struct lruvec { 
    /**
     *  各种 LRU 类型 页面的 page->lru
     */
	struct list_head		lists[NR_LRU_LISTS];
	/*
	 * These track the cost of reclaiming one LRU - file or anon -
	 * over the other. As the observed cost of reclaiming one LRU
	 * increases, the reclaim scan balance tips toward the other.
	 */
	unsigned long			anon_cost;
	unsigned long			file_cost;
    
	/* Non-resident age, driven by LRU movement */
	atomic_long_t			nonresident_age;
    
	/* Refaults at the time of last reclaim cycle */
	unsigned long			refaults[ANON_AND_FILE];
    
	/* Various lruvec state flags (enum lruvec_flags) */
	unsigned long			flags;

    /*  */
#ifdef CONFIG_MEMCG
	struct pglist_data *pgdat;
#endif
};

/* Isolate unmapped pages */
#define ISOLATE_UNMAPPED	((__force isolate_mode_t)0x2)
/* Isolate for asynchronous migration */
#define ISOLATE_ASYNC_MIGRATE	((__force isolate_mode_t)0x4)
/* Isolate unevictable pages */
#define ISOLATE_UNEVICTABLE	((__force isolate_mode_t)0x8)

/* LRU Isolation modes. */
typedef unsigned __bitwise isolate_mode_t;

//每个zone都有自己独立的min, low和high三个档位的watermark值.
//在进行内存分配的时候，如果分配器（比如buddy allocator）发
//现当前空余内存的值低于"low"但高于"min"，说明现在内存面临一
//定的压力，那么在此次内存分配完成后，kswapd将被唤醒
enum zone_watermarks {  /* ZONE 的水位: 可用内存==水 */
	WMARK_MIN,  /* 最低水位 */
	WMARK_LOW,  /* 低水位 */
	WMARK_HIGH, /* 高水位 */
	NR_WMARK    /*  */
};

/*
初始化： `init_per_zone_wmark_min()`


Avaliable
Free
Page
|
|+                                  Back to High 
| +                                 WaterMark           +
|  +                                      \         +
|   +                                      \    +
|----+--------------------------------------+-------------------------------WMARK_HIGH
|     +                                   +
|      +                                +
|       + kswapd Wakeup               +
|        + /                        +
|---------+-----------------------+-----------------------------------------WMARK_LOW
|           +       Direct      +
|             +     Reclaim   +
|               +    /      +
|                 + /     +
|-------------------+---+---------------------------------------------------WMARK_MIN
|                    + +
|                     +
|
|
\________________________________________________________________________________> time

*/

#if 0 //我把它们改成了 函数，便于阅读理解
#define min_wmark_pages(z) (z->_watermark[WMARK_MIN] + z->watermark_boost)
#define low_wmark_pages(z) (z->_watermark[WMARK_LOW] + z->watermark_boost)
#define high_wmark_pages(z) (z->_watermark[WMARK_HIGH] + z->watermark_boost)
#define wmark_pages(z, i) (z->_watermark[i] + z->watermark_boost)
#else //对应上面的宏定义

static inline unsigned long min_wmark_pages(struct zone *z)
{
    return (z->_watermark[WMARK_MIN] + z->watermark_boost);
}
static inline unsigned long low_wmark_pages(struct zone *z)
{
    return (z->_watermark[WMARK_LOW] + z->watermark_boost);
}
static inline unsigned long high_wmark_pages(struct zone *z)
{
    /**
     *  高水位
     */
    return (z->_watermark[WMARK_HIGH] + z->watermark_boost);
}
static inline unsigned long wmark_pages(struct zone *z, enum zone_watermarks i)
{
    return (z->_watermark[i] + z->watermark_boost);
}
#endif

struct per_cpu_pages {
	int count;		/* number of pages in the list */
	int high;		/* high watermark, emptying needed */
	int batch;		/* chunk size for buddy add/remove */

	/* Lists of pages, one per migrate type stored on the pcp-lists */
	struct list_head lists[MIGRATE_PCPTYPES];
};

struct per_cpu_pageset {    /* 每个CPU的pageset */
	struct per_cpu_pages pcp;   /* per cpu pages */
#ifdef CONFIG_NUMA
	s8 expire;
	u16 vm_numa_stat_diff[NR_VM_NUMA_STAT_ITEMS];
#endif
#ifdef CONFIG_SMP
	s8 stat_threshold;
	s8 vm_stat_diff[NR_VM_ZONE_STAT_ITEMS];
#endif
};

/**
 *  
 */
struct per_cpu_nodestat {
	s8 stat_threshold;
	s8 vm_node_stat_diff[NR_VM_NODE_STAT_ITEMS];
};

#endif /* !__GENERATING_BOUNDS.H */

//* `ZONE_DMA` - 0-16M;
//* `ZONE_DMA32` - used for 32 bit devices that can only do DMA areas below 4G;
//* `ZONE_NORMAL` - all RAM from the 4GB on the `x86_64`;
//* `ZONE_HIGHMEM` - absent on the `x86_64`; x86_64中没有
//* `ZONE_MOVABLE` - zone which contains movable pages.
enum zone_type {
	/*
	 * ZONE_DMA and ZONE_DMA32 are used when there are peripherals not able
	 * to DMA to all of the addressable memory (ZONE_NORMAL).
	 * On architectures where this area covers the whole 32 bit address
	 * space ZONE_DMA32 is used. ZONE_DMA is left for the ones with smaller
	 * DMA addressing constraints. This distinction is important as a 32bit
	 * DMA mask is assumed when ZONE_DMA32 is defined. Some 64-bit
	 * platforms may need both zones as they support peripherals with
	 * different DMA addressing limitations.
	 *
	 * Some examples:
	 *
	 *  - i386 and x86_64 have a fixed 16M ZONE_DMA and ZONE_DMA32 for the
	 *    rest of the lower 4G.
	 *
	 *  - arm only uses ZONE_DMA, the size, up to 4G, may vary depending on
	 *    the specific device.
	 *
	 *  - arm64 has a fixed 1G ZONE_DMA and ZONE_DMA32 for the rest of the
	 *    lower 4G.
	 *
	 *  - powerpc only uses ZONE_DMA, the size, up to 2G, may vary
	 *    depending on the specific device.
	 *
	 *  - s390 uses ZONE_DMA fixed to the lower 2G.
	 *
	 *  - ia64 and riscv only use ZONE_DMA32.
	 *
	 *  - parisc uses neither.
	 */
#ifdef CONFIG_ZONE_DMA
	ZONE_DMA,       /* DMA */
#endif
#ifdef CONFIG_ZONE_DMA32
	ZONE_DMA32,     /*  */
#endif
	/*
	 * Normal addressable memory is in ZONE_NORMAL. DMA operations can be
	 * performed on pages in ZONE_NORMAL if the DMA devices support
	 * transfers to all addressable memory.
	 */
	ZONE_NORMAL,    /*  */
#ifdef CONFIG_HIGHMEM
	/*
	 * A memory area that is only addressable by the kernel through
	 * mapping portions into its own address space. This is for example
	 * used by i386 to allow the kernel to address the memory beyond
	 * 900MB. The kernel will set up special mappings (page
	 * table entries on i386) for each page that the kernel needs to
	 * access.
	 */
//	ZONE_HIGHMEM,   /*  */
#endif
	/*
	 * ZONE_MOVABLE is similar to ZONE_NORMAL, except that it contains
	 * movable pages with few exceptional cases described below. Main use
	 * cases for ZONE_MOVABLE are to make memory offlining/unplug more
	 * likely to succeed, and to locally limit unmovable allocations - e.g.,
	 * to increase the number of THP/huge pages. Notable special cases are:
	 *
	 * 1. Pinned pages: (long-term) pinning of movable pages might
	 *    essentially turn such pages unmovable. Memory offlining might
	 *    retry a long time.
	 * 2. memblock allocations: kernelcore/movablecore setups might create
	 *    situations where ZONE_MOVABLE contains unmovable allocations
	 *    after boot. Memory offlining and allocations fail early.
	 * 3. Memory holes: kernelcore/movablecore setups might create very rare
	 *    situations where ZONE_MOVABLE contains memory holes after boot,
	 *    for example, if we have sections that are only partially
	 *    populated. Memory offlining and allocations fail early.
	 * 4. PG_hwpoison pages: while poisoned pages can be skipped during
	 *    memory offlining, such pages cannot be allocated.
	 * 5. Unmovable PG_offline pages: in paravirtualized environments,
	 *    hotplugged memory blocks might only partially be managed by the
	 *    buddy (e.g., via XEN-balloon, Hyper-V balloon, virtio-mem). The
	 *    parts not manged by the buddy are unmovable PG_offline pages. In
	 *    some cases (virtio-mem), such pages can be skipped during
	 *    memory offlining, however, cannot be moved/allocated. These
	 *    techniques might use alloc_contig_range() to hide previously
	 *    exposed pages from the buddy again (e.g., to implement some sort
	 *    of memory unplug in virtio-mem).
	 *
	 * In general, no unmovable allocations that degrade memory offlining
	 * should end up in ZONE_MOVABLE. Allocators (like alloc_contig_range())
	 * have to expect that migrating pages in ZONE_MOVABLE can fail (even
	 * if has_unmovable_pages() states that there are no unmovable pages,
	 * there can be false negatives).
	 */
	ZONE_MOVABLE,   /* 用于适应大块连续内存的分配 */
#ifdef CONFIG_ZONE_DEVICE
	ZONE_DEVICE,    /*  */
#endif
	__MAX_NR_ZONES,
    MAX_NR_ZONES = __MAX_NR_ZONES, /* = 5 我加的 */
};

#ifndef __GENERATING_BOUNDS_H

#define ASYNC_AND_SYNC 2

struct zone {   /* 内存 ZONE */
	/* Read-mostly fields */

	/* zone watermarks, access with *_wmark_pages(zone) macros */
    //WMARK_MIN,  /* 最低水位 */
    //WMARK_LOW,  /* 低水位 */
    //WMARK_HIGH, /* 高水位 */
    //NR_WMARK
	unsigned long _watermark[NR_WMARK]; /* 水位:每个zone都有自己独立的min, low和high三个档位的watermark值 */

    /**
     *  水位提高
     */
	unsigned long watermark_boost;      /*  */

	unsigned long nr_reserved_highatomic;

	/*
	 * We don't know if the memory that we're going to allocate will be
	 * freeable or/and it will be released eventually, so to avoid totally
	 * wasting several GB of ram we must reserve some of the lower zone
	 * memory (otherwise we risk to run OOM on the lower zones despite
	 * there being tons of freeable ram on the higher zones).  This array is
	 * recalculated at runtime if the sysctl_lowmem_reserve_ratio sysctl
	 * changes.
	 */
	long lowmem_reserve[MAX_NR_ZONES];

#ifdef CONFIG_NUMA
	int node;   /* 属于哪个 zone */
#endif

    /* 属于哪个node */
	struct pglist_data	*zone_pgdat;    /* node 信息 */

    /*  */
	struct per_cpu_pageset __percpu *pageset;   /* 每个CPU的pageset */

#ifndef CONFIG_SPARSEMEM
	/*
	 * Flags for a pageblock_nr_pages block. See pageblock-flags.h.
	 * In SPARSEMEM, this map is stored in struct mem_section
	 */
	unsigned long		*pageblock_flags;
#endif /* CONFIG_SPARSEMEM */

	/* zone_start_pfn == zone_start_paddr >> PAGE_SHIFT */
	unsigned long		zone_start_pfn; /* zone的起始页帧号 */

	/*
	 * spanned_pages is the total pages spanned by the zone, including
	 * holes, which is calculated as:
	 * 	spanned_pages = zone_end_pfn - zone_start_pfn;
	 *
	 * present_pages is physical pages existing within the zone, which
	 * is calculated as:
	 *	present_pages = spanned_pages - absent_pages(pages in holes);
	 *
	 * managed_pages is present pages managed by the buddy system, which
	 * is calculated as (reserved_pages includes pages allocated by the
	 * bootmem allocator):
	 *	managed_pages = present_pages - reserved_pages;
	 *
	 * So present_pages may be used by memory hotplug or memory power
	 * management logic to figure out unmanaged pages by checking
	 * (present_pages - managed_pages). And managed_pages should be used
	 * by page allocator and vm scanner to calculate all kinds of watermarks
	 * and thresholds.
	 *
	 * Locking rules:
	 *
	 * zone_start_pfn and spanned_pages are protected by span_seqlock.
	 * It is a seqlock because it has to be read outside of zone->lock,
	 * and it is done in the main allocator path.  But, it is written
	 * quite infrequently.
	 *
	 * The span_seq lock is declared along with zone->lock because it is
	 * frequently read in proximity to zone->lock.  It's good to
	 * give them a chance of being in the same cacheline.
	 *
	 * Write access to present_pages at runtime should be protected by
	 * mem_hotplug_begin/end(). Any reader who can't tolerant drift of
	 * present_pages should get_online_mems() to get a stable value.
	 */
	atomic_long_t		managed_pages;  /* 伙伴系统管理的 当前页 数量 */
	unsigned long		spanned_pages;  /*  */
	unsigned long		present_pages;  /* ZONE 中的 物理页 数量 */

	const char		*name;

#ifdef CONFIG_MEMORY_ISOLATION
	/*
	 * Number of isolated pageblock. It is used to solve incorrect
	 * freepage counting problem due to racy retrieving migratetype
	 * of pageblock. Protected by zone->lock.
	 */
	unsigned long		nr_isolate_pageblock;
#endif

#ifdef CONFIG_MEMORY_HOTPLUG
	/* see spanned/present_pages for more description */
	seqlock_t		span_seqlock;
#endif

	int initialized;/* 是否初始化 */

	/* Write-intensive fields used from the page allocator */
	ZONE_PADDING(_pad1_)    /*  */

	/* free areas of different sizes */
	struct free_area	free_area[MAX_ORDER];   /*  */

	/* zone flags, see below */
	unsigned long		flags;

	/* Primarily protects free_area */
	spinlock_t		lock;

	/* Write-intensive fields used by compaction and vmstats. */
	ZONE_PADDING(_pad2_)

	/*
	 * When free pages are below this point, additional steps are taken
	 * when reading the number of free pages to avoid per-cpu counter
	 * drift allowing watermarks to be breached
	 */
	unsigned long percpu_drift_mark;

#if defined CONFIG_COMPACTION || defined CONFIG_CMA
	/* pfn where compaction free scanner should start */
	unsigned long		compact_cached_free_pfn;
	/* pfn where compaction migration scanner should start */
	unsigned long		compact_cached_migrate_pfn[ASYNC_AND_SYNC];
	unsigned long		compact_init_migrate_pfn;
	unsigned long		compact_init_free_pfn;
#endif

#ifdef CONFIG_COMPACTION
	/*
	 * On compaction failure, 1<<compact_defer_shift compactions
	 * are skipped before trying again. The number attempted since
	 * last failure is tracked with compact_considered.
	 * compact_order_failed is the minimum compaction failed order.
	 */
	unsigned int		compact_considered;
	unsigned int		compact_defer_shift;
	int			compact_order_failed;
#endif

#if defined CONFIG_COMPACTION || defined CONFIG_CMA
	/* Set to true when the PG_migrate_skip bits should be cleared */
	bool			compact_blockskip_flush;
#endif

	bool			contiguous;

	ZONE_PADDING(_pad3_)
        
	/* Zone statistics - 溶剂信息 */
	atomic_long_t		vm_stat[NR_VM_ZONE_STAT_ITEMS];
	atomic_long_t		vm_numa_stat[NR_VM_NUMA_STAT_ITEMS];
} ____cacheline_internodealigned_in_smp;

enum pgdat_flags {
	PGDAT_DIRTY,	 /* reclaim scanning has recently found
					 * many dirty file pages at the tail
					 * of the LRU.
					 */
	PGDAT_WRITEBACK, /* reclaim scanning has recently found
					 * many pages under writeback
					 */
	PGDAT_RECLAIM_LOCKED,		/* prevents concurrent reclaim */
};

enum zone_flags {
    /* zone recently boosted(提升) watermarks. 
     * Cleared when kswapd is woken.
     */
	ZONE_BOOSTED_WATERMARK,		
};

static inline unsigned long zone_managed_pages(struct zone *zone)   /* ZONE 的管理数据 */
{
	return (unsigned long)atomic_long_read(&zone->managed_pages);
}

static inline unsigned long zone_end_pfn(const struct zone *zone)
{
	return zone->zone_start_pfn + zone->spanned_pages;
}

static inline bool zone_spans_pfn(const struct zone *zone, unsigned long pfn)
{
	return zone->zone_start_pfn <= pfn && pfn < zone_end_pfn(zone);
}

static inline bool zone_is_initialized(struct zone *zone)
{
	return zone->initialized;
}

static inline bool zone_is_empty(struct zone *zone)
{
	return zone->spanned_pages == 0;
}

/*
 * Return true if [start_pfn, start_pfn + nr_pages) range has a non-empty
 * intersection with the given zone
 */
static inline bool zone_intersects(struct zone *zone,
		unsigned long start_pfn, unsigned long nr_pages)
{
	if (zone_is_empty(zone))
		return false;
	if (start_pfn >= zone_end_pfn(zone) ||
	    start_pfn + nr_pages <= zone->zone_start_pfn)
		return false;

	return true;
}

/*
 * The "priority" of VM scanning is how much of the queues we will scan in one
 * go. A value of 12 for DEF_PRIORITY implies that we will scan 1/4096th of the
 * queues ("queue_length >> 12") during an aging round.
 */
#define DEF_PRIORITY 12

/* Maximum number of zones on a zonelist */
#define MAX_ZONES_PER_ZONELIST (MAX_NUMNODES/* NODE个数 */ * MAX_NR_ZONES/* ZONE个数 */)

enum {
	ZONELIST_FALLBACK,	/* zonelist with fallback(倒退) */
#ifdef CONFIG_NUMA
	/*
	 * The NUMA zonelists are doubled because we need zonelists that
	 * restrict the allocations to a single node for __GFP_THISNODE.
	 *
	 * NUMA 区域列表翻了一番，因为我们需要区域列表来限制对 __GFP_THISNODE 的单个节点的分配。
	 */
	ZONELIST_NOFALLBACK,	/* zonelist without fallback (__GFP_THISNODE) */
#endif
	MAX_ZONELISTS
};

/*
 * This struct contains information about a zone in a zonelist. It is stored
 * here to avoid dereferences into large structures and lookups of tables
 *
 * 避免 去引用到大的数据结构，并且查找表
 */
struct zoneref {    /* 在 zonelist 中的 zone 信息 */
	struct zone *zone;	/* Pointer to actual zone */
	int zone_idx;		/* zone_idx(zoneref->zone) */
};

/*
 * One allocation request operates on a zonelist. A zonelist
 * is a list of zones, the first one is the 'goal' of the
 * allocation, the other zones are fallback zones, in decreasing
 * priority.
 *
 * To speed the reading of the zonelist, the zonerefs contain the zone index
 * of the entry being read. Helper functions to access information given
 * a struct zoneref are
 *
 * zonelist_zone()	- Return the struct zone * for an entry in _zonerefs
 * zonelist_zone_idx()	- Return the index of the zone for an entry
 * zonelist_node_idx()	- Return the index of the node for an entry
 */
struct zonelist {/* ZONE list */
	struct zoneref _zonerefs[MAX_ZONES_PER_ZONELIST /* ZONEs*NODEs */+ 1];
};

#ifndef CONFIG_DISCONTIGMEM
/* The array of struct pages - for discontigmem use pgdat->lmem_map */
extern struct page *mem_map;
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
struct deferred_split {
	spinlock_t split_queue_lock;
	struct list_head split_queue;
	unsigned long split_queue_len;
};
#endif

/*
 * On NUMA machines, each NUMA node would have a pg_data_t to describe
 * it's memory layout. On UMA machines there is a single pglist_data which
 * describes the whole memory.
 *
 * Memory statistics and page replacement data structures are maintained on a
 * per-zone basis.
 */
typedef struct pglist_data {/* 描述 NUMA 内存布局 */
	/*
	 * node_zones contains just the zones for THIS node. Not all of the
	 * zones may be populated, but it is the full list. It is referenced by
	 * this node's node_zonelists as well as other node's node_zonelists.
	 */
	struct zone node_zones[MAX_NR_ZONES];/* 这个 NODE 上的所有 zone */

	/*
	 * node_zonelists contains references to all zones in all nodes.
	 * Generally the first zones will be references to this node's
	 * node_zones.
	 *
	 * 提供一种机制，可以访问所有 NODE 的 zonelist
	 * 通常第一个zone 是当前 node 的 node_zones 的引用
	 *
	 * 里面是一个 zoneref 数组
	 */
	struct zonelist node_zonelists[MAX_ZONELISTS/* 2 */];  /*  */

	int nr_zones; /* number of populated zones in this node 此节点中的人口稠密区数 */
    
#ifdef CONFIG_FLAT_NODE_MEM_MAP	/* means !SPARSEMEM */
	struct page *node_mem_map;
#ifdef CONFIG_PAGE_EXTENSION/* 页扩展 */
	struct page_ext *node_page_ext;/* 页扩展 */
#endif
#endif

#if defined(CONFIG_MEMORY_HOTPLUG) || defined(CONFIG_DEFERRED_STRUCT_PAGE_INIT)
	/*
	 * Must be held any time you expect node_start_pfn,
	 * node_present_pages, node_spanned_pages or nr_zones to stay constant.
	 * Also synchronizes pgdat->first_deferred_pfn during deferred page
	 * init.
	 *
	 * pgdat_resize_lock() and pgdat_resize_unlock() are provided to
	 * manipulate node_size_lock without checking for CONFIG_MEMORY_HOTPLUG
	 * or CONFIG_DEFERRED_STRUCT_PAGE_INIT.
	 *
	 * Nests above zone->lock and zone->span_seqlock
	 */
	spinlock_t node_size_lock;
#endif

    /**
     *  
     */
	unsigned long node_start_pfn;   /* 起始页帧号 */
	unsigned long node_present_pages; /* total number of physical pages *//* 物理页总个数 */
	unsigned long node_spanned_pages; /* total size of physical page
					     range, including holes *//* 物理页总大小，包括空洞 */
	int node_id;

    /**
     *  等待队列
     *
     *  在`free_area_init_core()`中初始化
     *  分配路径上的唤醒函数`wakeup_kswapd()`, 根据低水位标志`ALLOC_WMARK_LOW`
     */
	wait_queue_head_t kswapd_wait;
	wait_queue_head_t pfmemalloc_wait;
	struct task_struct *kswapd;	/* Protected by mem_hotplug_begin/end() */

    /*  */                   
	int kswapd_order;

    /**
     *  从 alloc_context.highest_zoneidx 传递过来
     *  在 balance_pgdat() 中被使用
     *
     *  可以扫描和回收的最高 ZONE，扫描顺序: 从 高ZONE 到 低ZONE
     */
	enum zone_type kswapd_highest_zoneidx;

	int kswapd_failures;		/* Number of 'reclaimed == 0' runs */

#ifdef CONFIG_COMPACTION
    /* 内存规整 */
	int kcompactd_max_order;
	enum zone_type kcompactd_highest_zoneidx;
	wait_queue_head_t kcompactd_wait;   /* 内存规整线程等待 */
	struct task_struct *kcompactd;
#endif

	/*
	 * This is a per-node reserve of pages that are not available
	 * to userspace allocations.
	 */
	unsigned long		totalreserve_pages;

#ifdef CONFIG_NUMA
	/*
	 * node reclaim becomes active if more unmapped pages exist.
	 *
	 * 如果 未映射的页面 比较多，NODE回收将激活
	 */
	unsigned long		min_unmapped_pages;
	unsigned long		min_slab_pages;
    
#endif /* CONFIG_NUMA */

	/* Write-intensive fields used by page reclaim */
	ZONE_PADDING(_pad1_)

    /**
     *  
     */
	spinlock_t		lru_lock;   /* 最近最少使用 */

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
	/*
	 * If memory initialisation on large machines is deferred then this
	 * is the first PFN that needs to be initialised.
	 */
	unsigned long first_deferred_pfn;/* 第一个需要初始化的 PFN */
#endif /* CONFIG_DEFERRED_STRUCT_PAGE_INIT */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	struct deferred_split deferred_split_queue;
#endif

	/* Fields commonly accessed by the page reclaim scanner */

	/*
	 * NOTE: THIS IS UNUSED IF MEMCG IS ENABLED.
	 *
	 * Use mem_cgroup_lruvec() to look up lruvecs.
	 *
	 * 只有当`mem_cgroup_disabled`时才使用这个结构，否则使用 mem_cgroup
	 * ==========================================================
	 * `mem_cgroup_page_lruvec`
	 */
	struct lruvec		__lruvec;

	unsigned long		flags;

	ZONE_PADDING(_pad2_)

	/* Per-node vmstats */
	struct per_cpu_nodestat __percpu *per_cpu_nodestats;

    /**
     *  NODE 的统计信息
     */
	atomic_long_t		vm_stat[NR_VM_NODE_STAT_ITEMS];
    
} pg_data_t;

#define node_present_pages(nid)	(NODE_DATA(nid)->node_present_pages)
#define node_spanned_pages(nid)	(NODE_DATA(nid)->node_spanned_pages)
#ifdef CONFIG_FLAT_NODE_MEM_MAP
//#define pgdat_page_nr(pgdat, pagenr)	((pgdat)->node_mem_map + (pagenr))
#else
#define pgdat_page_nr(pgdat, pagenr)	pfn_to_page((pgdat)->node_start_pfn + (pagenr))
#endif
#define nid_page_nr(nid, pagenr) 	pgdat_page_nr(NODE_DATA(nid),(pagenr))

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid) pgdat_end_pfn(NODE_DATA(nid))

static inline unsigned long pgdat_end_pfn(pg_data_t *pgdat)
{
	return pgdat->node_start_pfn + pgdat->node_spanned_pages;
}

static inline bool pgdat_is_empty(pg_data_t *pgdat)
{
	return !pgdat->node_start_pfn && !pgdat->node_spanned_pages;
}

#include <linux/memory_hotplug.h>

void build_all_zonelists(pg_data_t *pgdat);
void wakeup_kswapd(struct zone *zone, gfp_t gfp_mask, int order,
		   enum zone_type highest_zoneidx);
bool __zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
			 int highest_zoneidx, unsigned int alloc_flags,
			 long free_pages);
bool zone_watermark_ok(struct zone *z, unsigned int order,
		unsigned long mark, int highest_zoneidx,
		unsigned int alloc_flags);
bool zone_watermark_ok_safe(struct zone *z, unsigned int order,
		unsigned long mark, int highest_zoneidx);
/*
 * Memory initialization context, use to differentiate memory added by
 * the platform statically or via memory hotplug interface.
 */
enum meminit_context {
	MEMINIT_EARLY,
	MEMINIT_HOTPLUG,
};

extern void init_currently_empty_zone(struct zone *zone, unsigned long start_pfn,
				     unsigned long size);

extern void lruvec_init(struct lruvec *lruvec);

/**
 *  lruvec 对应的 NODE 
 */
static inline struct pglist_data *lruvec_pgdat(struct lruvec *lruvec)
{
#ifdef CONFIG_MEMCG
	return lruvec->pgdat;
#else
	return container_of(lruvec, struct pglist_data, __lruvec);
#endif
}

extern unsigned long lruvec_lru_size(struct lruvec *lruvec, enum lru_list lru, int zone_idx);

#ifdef CONFIG_HAVE_MEMORYLESS_NODES
//int local_memory_node(int node_id);
#else
static inline int local_memory_node(int node_id) { return node_id; };
#endif

/*
 * zone_idx() returns 0 for the ZONE_DMA zone, 1 for the ZONE_NORMAL zone, etc.
 *
 * 这个计算需要注意
 * 	struct test T[10];
 *  struct test *t2 = &T[2];
 *  printf("idx = %d\n", t2 - T);
 *
 *  结果为 idx = 2
 */
#define zone_idx(zone)		((zone) - (zone)->zone_pgdat->node_zones)   /* 数组成员地址 - 数组起始地址 */

/*
 * Returns true if a zone has pages managed by the buddy allocator.
 * All the reclaim decisions have to use this function rather than
 * populated_zone(). If the whole zone is reserved then we can easily
 * end up with populated_zone() && !managed_zone().
 */
static inline bool managed_zone(struct zone *zone)
{
	return zone_managed_pages(zone);
}

/* Returns true if a zone has memory */
static inline bool populated_zone(struct zone *zone)
{
	return zone->present_pages;
}

#ifdef CONFIG_NUMA
static inline int zone_to_nid(struct zone *zone)    /*  */
{
	return zone->node;
}

static inline void zone_set_nid(struct zone *zone, int nid)
{
	zone->node = nid;
}
#else
/*  */
#endif

extern int movable_zone;

#ifdef CONFIG_HIGHMEM
//static inline int zone_movable_is_highmem(void)
//{
//#ifdef CONFIG_NEED_MULTIPLE_NODES
//	return movable_zone == ZONE_HIGHMEM;
//#else
//	return (ZONE_MOVABLE - 1) == ZONE_HIGHMEM;
//#endif
//}
#endif

static inline int is_highmem_idx(enum zone_type idx)
{
#ifdef CONFIG_HIGHMEM
//	return (idx == ZONE_HIGHMEM ||
//		(idx == ZONE_MOVABLE && zone_movable_is_highmem()));
#else
	return 0;
#endif
}

/**
 * is_highmem - helper function to quickly check if a struct zone is a
 *              highmem zone or not.  This is an attempt to keep references
 *              to ZONE_{DMA/NORMAL/HIGHMEM/etc} in general code to a minimum.
 * @zone - pointer to struct zone variable
 */
static inline int is_highmem(struct zone *zone)
{
#ifdef CONFIG_HIGHMEM
//	return is_highmem_idx(zone_idx(zone));
#else
	return 0;
#endif
}

/* These two functions are used to setup the per zone pages min values */
struct ctl_table;

int min_free_kbytes_sysctl_handler(struct ctl_table *, int, void *, size_t *,
		loff_t *);
int watermark_scale_factor_sysctl_handler(struct ctl_table *, int, void *,
		size_t *, loff_t *);
extern int sysctl_lowmem_reserve_ratio[MAX_NR_ZONES];
int lowmem_reserve_ratio_sysctl_handler(struct ctl_table *, int, void *,
		size_t *, loff_t *);
int percpu_pagelist_fraction_sysctl_handler(struct ctl_table *, int,
		void *, size_t *, loff_t *);
int sysctl_min_unmapped_ratio_sysctl_handler(struct ctl_table *, int,
		void *, size_t *, loff_t *);
int sysctl_min_slab_ratio_sysctl_handler(struct ctl_table *, int,
		void *, size_t *, loff_t *);
int numa_zonelist_order_handler(struct ctl_table *, int,
		void *, size_t *, loff_t *);
extern int percpu_pagelist_fraction;
extern char numa_zonelist_order[];
#define NUMA_ZONELIST_ORDER_LEN	16

#ifndef CONFIG_NEED_MULTIPLE_NODES

//extern struct pglist_data contig_page_data;
//#define NODE_DATA(nid)		(&contig_page_data)
//#define NODE_MEM_MAP(nid)	mem_map

#else /* CONFIG_NEED_MULTIPLE_NODES */

#include <asm/mmzone.h>

#endif /* !CONFIG_NEED_MULTIPLE_NODES */

extern struct pglist_data *first_online_pgdat(void);
extern struct pglist_data *next_online_pgdat(struct pglist_data *pgdat);
extern struct zone *next_zone(struct zone *zone);

/**
 * for_each_online_pgdat - helper macro to iterate over all online nodes
 * @pgdat - pointer to a pg_data_t variable
 */
#define for_each_online_pgdat(pgdat)	/* 遍历页表 */		\
	for (pgdat = first_online_pgdat();		\
	     pgdat;					\
	     pgdat = next_online_pgdat(pgdat))
/**
 * for_each_zone - helper macro to iterate over all memory zones
 * @zone - pointer to struct zone variable
 *
 * The user only needs to declare the zone variable, for_each_zone
 * fills it in.
 */
#define for_each_zone(zone)			        \
	for (zone = (first_online_pgdat())->node_zones; \
	     zone;					\
	     zone = next_zone(zone))

#define for_each_populated_zone(zone)		        \
	for (zone = (first_online_pgdat())->node_zones; \
	     zone;					\
	     zone = next_zone(zone))			\
		if (!populated_zone(zone))		\
			; /* do nothing */		\
		else

static inline struct zone *zonelist_zone(struct zoneref *zoneref)
{
	return zoneref->zone;
}

static inline int zonelist_zone_idx(struct zoneref *zoneref)
{
	return zoneref->zone_idx;
}

static inline int zonelist_node_idx(struct zoneref *zoneref)
{
	return zone_to_nid(zoneref->zone);
}

struct zoneref *__next_zones_zonelist(struct zoneref *z,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes);

/**
 * next_zones_zonelist - Returns the next zone at or below highest_zoneidx within
 *                       the allowed nodemask using a cursor within a zonelist as a starting point
 *
 *  返回下一个 highest_zoneidx 及其下方的node(nodemask以内)
 *
 * @z - The cursor used as a starting point for the search
 * @highest_zoneidx - The zone index of the highest zone to return
 * @nodes - An optional nodemask to filter the zonelist with
 *
 * This function returns the next zone at or below a given zone index that is
 * within the allowed nodemask using a cursor as the starting point for the
 * search. The zoneref returned is a cursor that represents the current zone
 * being examined. It should be advanced by one before calling
 * next_zones_zonelist again.
 */
static __always_inline struct zoneref *next_zones_zonelist(struct zoneref *z,
                    					enum zone_type highest_zoneidx,
                    					nodemask_t *nodes)
{
    /**
     *  nodemask 为空 并且当前的 zoneidx <= highest_zoneidx
     */
	if (likely(!nodes && zonelist_zone_idx(z) <= highest_zoneidx))
		return z;

    /**
     *  nodemask 不为空, 返回一个 zoneidx <= highest_zoneidx 的 zoneref 
     */
	return __next_zones_zonelist(z, highest_zoneidx, nodes);
}

/**
 * first_zones_zonelist - Returns the first zone at or below highest_zoneidx within 
 *                          the allowed nodemask in a zonelist
 *
 *  返回第一个 highest_zoneidx 及其下方的node(nodemask以内)
 *
 * @zonelist - The zonelist to search for a suitable zone
 * @highest_zoneidx - The zone index of the highest zone to return
 * @nodes - An optional nodemask to filter the zonelist with
 * @return - Zoneref pointer for the first suitable zone found (see below)
 *
 * This function returns the first zone at or below a given zone index that is
 * within the allowed nodemask. The zoneref returned is a cursor that can be
 * used to iterate the zonelist with next_zones_zonelist by advancing it by
 * one before calling.
 *
 * When no eligible zone is found, zoneref->zone is NULL (zoneref itself is
 * never NULL). This may happen either genuinely, or due to concurrent nodemask
 * update due to cpuset modification.
 */
/*
 * The preferred zone is used for statistics but crucially it is
 * also used as the starting point for the zonelist iterator. It
 * may get reset for allocations that ignore memory policies.
 *
 * 首选区域用于统计，但至关重要的是，它也用作 zonelist 迭代器的起点。 
 * 对于忽略内存策略的分配，它可能会被重置。
 */
static inline struct zoneref *first_zones_zonelist(struct zonelist *zonelist,
                    					enum zone_type highest_zoneidx,
                    					nodemask_t *nodes)
{
	return next_zones_zonelist(zonelist->_zonerefs,
							highest_zoneidx, nodes);
}

/**
 * for_each_zone_zonelist_nodemask - helper macro to iterate over valid zones in a zonelist at or below a given zone index and within a nodemask
 * @zone - The current zone in the iterator
 * @z - The current pointer within zonelist->_zonerefs being iterated
 * @zlist - The zonelist being iterated
 * @highidx - The zone index of the highest zone to return
 * @nodemask - Nodemask allowed by the allocator
 *
 * This iterator iterates though all zones at or below a given zone index and
 * within a given nodemask
 */
#define for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, nodemask) \
	for (z = first_zones_zonelist(zlist, highidx, nodemask), zone = zonelist_zone(z);	\
		zone;							\
		z = next_zones_zonelist(++z, highidx, nodemask),	\
			zone = zonelist_zone(z))

/**
 *  
 */
#define for_next_zone_zonelist_nodemask(zone, z, highidx, nodemask) \
	for (zone = z->zone;	\
		zone;							\
		z = next_zones_zonelist(++z, highidx, nodemask),	\
			zone = zonelist_zone(z))


/**
 * for_each_zone_zonelist - helper macro to iterate over valid zones in a zonelist at or below a given zone index
 * @zone - The current zone in the iterator
 * @z - The current pointer within zonelist->zones being iterated
 * @zlist - The zonelist being iterated
 * @highidx - The zone index of the highest zone to return
 *
 * This iterator iterates though all zones at or below a given zone index.
 */
#define for_each_zone_zonelist(zone, z, zlist, highidx) \
	for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, NULL)

#ifdef CONFIG_SPARSEMEM
#include <asm/sparsemem.h>
#endif

#ifdef CONFIG_FLATMEM
#define pfn_to_nid(pfn)		(0)
#endif

#ifdef CONFIG_SPARSEMEM

/*
 * SECTION_SHIFT    		#bits space required to store a section #
 *
 * PA_SECTION_SHIFT		physical address to/from section number
 * PFN_SECTION_SHIFT		pfn to/from section number
 */
#define PA_SECTION_SHIFT	(SECTION_SIZE_BITS)
#define PFN_SECTION_SHIFT	(SECTION_SIZE_BITS - PAGE_SHIFT)

#define NR_MEM_SECTIONS		(1UL << SECTIONS_SHIFT)

#define PAGES_PER_SECTION       (1UL << PFN_SECTION_SHIFT)
#define PAGE_SECTION_MASK	(~(PAGES_PER_SECTION-1))

#define SECTION_BLOCKFLAGS_BITS \
	((1UL << (PFN_SECTION_SHIFT - pageblock_order)) * NR_PAGEBLOCK_BITS)

#if (MAX_ORDER - 1 + PAGE_SHIFT) > SECTION_SIZE_BITS
#error Allocator MAX_ORDER exceeds SECTION_SIZE
#endif

static inline unsigned long pfn_to_section_nr(unsigned long pfn)
{
	return pfn >> PFN_SECTION_SHIFT;
}
static inline unsigned long section_nr_to_pfn(unsigned long sec)
{
	return sec << PFN_SECTION_SHIFT;
}

#define SECTION_ALIGN_UP(pfn)	(((pfn) + PAGES_PER_SECTION - 1) & PAGE_SECTION_MASK)
#define SECTION_ALIGN_DOWN(pfn)	((pfn) & PAGE_SECTION_MASK)

#define SUBSECTION_SHIFT 21
#define SUBSECTION_SIZE (1UL << SUBSECTION_SHIFT)

#define PFN_SUBSECTION_SHIFT (SUBSECTION_SHIFT - PAGE_SHIFT)
#define PAGES_PER_SUBSECTION (1UL << PFN_SUBSECTION_SHIFT)
#define PAGE_SUBSECTION_MASK (~(PAGES_PER_SUBSECTION-1))

#if SUBSECTION_SHIFT > SECTION_SIZE_BITS
#error Subsection size exceeds section size
#else
#define SUBSECTIONS_PER_SECTION (1UL << (SECTION_SIZE_BITS - SUBSECTION_SHIFT))
#endif

#define SUBSECTION_ALIGN_UP(pfn) ALIGN((pfn), PAGES_PER_SUBSECTION)
#define SUBSECTION_ALIGN_DOWN(pfn) ((pfn) & PAGE_SUBSECTION_MASK)

struct mem_section_usage {
#ifdef CONFIG_SPARSEMEM_VMEMMAP
	DECLARE_BITMAP(subsection_map, SUBSECTIONS_PER_SECTION);
#endif
	/* See declaration of similar field in struct zone */
	unsigned long pageblock_flags[0];
};

void subsection_map_init(unsigned long pfn, unsigned long nr_pages);

struct page;
struct page_ext;
struct mem_section {
	/*
	 * This is, logically, a pointer to an array of struct
	 * pages.  However, it is stored with some other magic.
	 * (see sparse.c::sparse_init_one_section())
	 *
	 * Additionally during early boot we encode node id of
	 * the location of the section here to guide allocation.
	 * (see sparse.c::memory_present())
	 *
	 * Making it a UL at least makes someone do a cast
	 * before using it wrong.
	 */
	unsigned long section_mem_map;

	struct mem_section_usage *usage;
#ifdef CONFIG_PAGE_EXTENSION
	/*
	 * If SPARSEMEM, pgdat doesn't have page_ext pointer. We use
	 * section. (see page_ext.h about this.)
	 */
	struct page_ext *page_ext;
	unsigned long pad;
#endif
	/*
	 * WARNING: mem_section must be a power-of-2 in size for the
	 * calculation and use of SECTION_ROOT_MASK to make sense.
	 */
};

#ifdef CONFIG_SPARSEMEM_EXTREME
#define SECTIONS_PER_ROOT       (PAGE_SIZE / sizeof (struct mem_section))
#else
#define SECTIONS_PER_ROOT	1
#endif

#define SECTION_NR_TO_ROOT(sec)	((sec) / SECTIONS_PER_ROOT)
#define NR_SECTION_ROOTS	DIV_ROUND_UP(NR_MEM_SECTIONS, SECTIONS_PER_ROOT)
#define SECTION_ROOT_MASK	(SECTIONS_PER_ROOT - 1)

#ifdef CONFIG_SPARSEMEM_EXTREME
extern struct mem_section **mem_section;
#else
extern struct mem_section mem_section[NR_SECTION_ROOTS][SECTIONS_PER_ROOT];
#endif

static inline unsigned long *section_to_usemap(struct mem_section *ms)
{
	return ms->usage->pageblock_flags;
}

static inline struct mem_section *__nr_to_section(unsigned long nr)
{
#ifdef CONFIG_SPARSEMEM_EXTREME
	if (!mem_section)
		return NULL;
#endif
	if (!mem_section[SECTION_NR_TO_ROOT(nr)])
		return NULL;
	return &mem_section[SECTION_NR_TO_ROOT(nr)][nr & SECTION_ROOT_MASK];
}
extern unsigned long __section_nr(struct mem_section *ms);
extern size_t mem_section_usage_size(void);

/*
 * We use the lower bits of the mem_map pointer to store
 * a little bit of information.  The pointer is calculated
 * as mem_map - section_nr_to_pfn(pnum).  The result is
 * aligned to the minimum alignment of the two values:
 *   1. All mem_map arrays are page-aligned.
 *   2. section_nr_to_pfn() always clears PFN_SECTION_SHIFT
 *      lowest bits.  PFN_SECTION_SHIFT is arch-specific
 *      (equal SECTION_SIZE_BITS - PAGE_SHIFT), and the
 *      worst combination is powerpc with 256k pages,
 *      which results in PFN_SECTION_SHIFT equal 6.
 * To sum it up, at least 6 bits are available.
 */
#define	SECTION_MARKED_PRESENT	(1UL<<0)
#define SECTION_HAS_MEM_MAP	(1UL<<1)
#define SECTION_IS_ONLINE	(1UL<<2)
#define SECTION_IS_EARLY	(1UL<<3)
#define SECTION_MAP_LAST_BIT	(1UL<<4)
#define SECTION_MAP_MASK	(~(SECTION_MAP_LAST_BIT-1))
#define SECTION_NID_SHIFT	3

static inline struct page *__section_mem_map_addr(struct mem_section *section)
{
	unsigned long map = section->section_mem_map;
	map &= SECTION_MAP_MASK;
	return (struct page *)map;
}

static inline int present_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_MARKED_PRESENT));
}

static inline int present_section_nr(unsigned long nr)
{
	return present_section(__nr_to_section(nr));
}

static inline int valid_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_HAS_MEM_MAP));
}

static inline int early_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_IS_EARLY));
}

static inline int valid_section_nr(unsigned long nr)
{
	return valid_section(__nr_to_section(nr));
}

static inline int online_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_IS_ONLINE));
}

static inline int online_section_nr(unsigned long nr)
{
	return online_section(__nr_to_section(nr));
}

#ifdef CONFIG_MEMORY_HOTPLUG
void online_mem_sections(unsigned long start_pfn, unsigned long end_pfn);
#ifdef CONFIG_MEMORY_HOTREMOVE
void offline_mem_sections(unsigned long start_pfn, unsigned long end_pfn);
#endif
#endif

static inline struct mem_section *__pfn_to_section(unsigned long pfn)
{
	return __nr_to_section(pfn_to_section_nr(pfn));
}

extern unsigned long __highest_present_section_nr;

static inline int subsection_map_index(unsigned long pfn)
{
	return (pfn & ~(PAGE_SECTION_MASK)) / PAGES_PER_SUBSECTION;
}

#ifdef CONFIG_SPARSEMEM_VMEMMAP
static inline int pfn_section_valid(struct mem_section *ms, unsigned long pfn)
{
	int idx = subsection_map_index(pfn);

	return test_bit(idx, ms->usage->subsection_map);
}
#else
static inline int pfn_section_valid(struct mem_section *ms, unsigned long pfn)
{
	return 1;
}
#endif

#ifndef CONFIG_HAVE_ARCH_PFN_VALID
static inline int pfn_valid(unsigned long pfn)
{
	struct mem_section *ms;

	if (pfn_to_section_nr(pfn) >= NR_MEM_SECTIONS)
		return 0;
	ms = __nr_to_section(pfn_to_section_nr(pfn));
	if (!valid_section(ms))
		return 0;
	/*
	 * Traditionally early sections always returned pfn_valid() for
	 * the entire section-sized span.
	 */
	return early_section(ms) || pfn_section_valid(ms, pfn);
}
#endif

static inline int pfn_in_present_section(unsigned long pfn)
{
	if (pfn_to_section_nr(pfn) >= NR_MEM_SECTIONS)
		return 0;
	return present_section(__nr_to_section(pfn_to_section_nr(pfn)));
}

static inline unsigned long next_present_section_nr(unsigned long section_nr)
{
	while (++section_nr <= __highest_present_section_nr) {
		if (present_section_nr(section_nr))
			return section_nr;
	}

	return -1;
}

/*
 * These are _only_ used during initialisation, therefore they
 * can use __initdata ...  They could have names to indicate
 * this restriction.
 */
#ifdef CONFIG_NUMA
#define pfn_to_nid(pfn)							\
({									\
	unsigned long __pfn_to_nid_pfn = (pfn);				\
	page_to_nid(pfn_to_page(__pfn_to_nid_pfn));			\
})
#else
#define pfn_to_nid(pfn)		(0)
#endif

void sparse_init(void);
#else
/*  */
#endif /* CONFIG_SPARSEMEM */

/*
 * During memory init memblocks map pfns to nids. The search is expensive and
 * this caches recent lookups. The implementation of __early_pfn_to_nid
 * may treat start/end as pfns or sections.
 */
struct mminit_pfnnid_cache {/* 页帧号 到 NODE 的缓存 */
	unsigned long last_start;
	unsigned long last_end;
	int last_nid;
};

/*
 * If it is possible to have holes within a MAX_ORDER_NR_PAGES, then we
 * need to check pfn validity within that MAX_ORDER_NR_PAGES block.
 * pfn_valid_within() should be used in this case; we optimise this away
 * when we have no holes within a MAX_ORDER_NR_PAGES block.
 */
#ifdef CONFIG_HOLES_IN_ZONE /* ZONE 中 有 空洞 */
#define pfn_valid_within(pfn) pfn_valid(pfn)
#else
#define pfn_valid_within(pfn) (1)
#endif

#ifdef CONFIG_ARCH_HAS_HOLES_MEMORYMODEL
/*
 * pfn_valid() is meant to be able to tell if a given PFN has valid memmap
 * associated with it or not. This means that a struct page exists for this
 * pfn. The caller cannot assume the page is fully initialized in general.
 * Hotplugable pages might not have been onlined yet. pfn_to_online_page()
 * will ensure the struct page is fully online and initialized. Special pages
 * (e.g. ZONE_DEVICE) are never onlined and should be treated accordingly.
 *
 * In FLATMEM, it is expected that holes always have valid memmap as long as
 * there is valid PFNs either side of the hole. In SPARSEMEM, it is assumed
 * that a valid section has a memmap for the entire section.
 *
 * However, an ARM, and maybe other embedded architectures in the future
 * free memmap backing holes to save memory on the assumption the memmap is
 * never used. The page_zone linkages are then broken even though pfn_valid()
 * returns true. A walker of the full memmap must then do this additional
 * check to ensure the memmap they are looking at is sane by making sure
 * the zone and PFN linkages are still valid. This is expensive, but walkers
 * of the full memmap are extremely rare.
 */
bool memmap_valid_within(unsigned long pfn,
					struct page *page, struct zone *zone);
#else
static inline bool memmap_valid_within(unsigned long pfn,
					struct page *page, struct zone *zone)
{
	return true;
}
#endif /* CONFIG_ARCH_HAS_HOLES_MEMORYMODEL */

#endif /* !__GENERATING_BOUNDS.H */
#endif /* !__ASSEMBLY__ */
#endif /* _LINUX_MMZONE_H */
