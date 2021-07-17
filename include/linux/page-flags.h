/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Macros for manipulating and testing page->flags
 */

#ifndef PAGE_FLAGS_H
#define PAGE_FLAGS_H

#include <linux/types.h>
#include <linux/bug.h>
#include <linux/mmdebug.h>
#ifndef __GENERATING_BOUNDS_H
#include <linux/mm_types.h>
#include <generated/bounds.h>
#endif /* !__GENERATING_BOUNDS_H */

/*
 * Various page->flags bits:
 *
 * PG_reserved is set for special pages. The "struct page" of such a page
 * should in general not be touched (e.g. set dirty) except by its owner.
 * Pages marked as PG_reserved include:
 * - Pages part of the kernel image (including vDSO) and similar (e.g. BIOS,
 *   initrd, HW tables)
 * - Pages reserved or allocated early during boot (before the page allocator
 *   was initialized). This includes (depending on the architecture) the
 *   initial vmemmap, initial page tables, crashkernel, elfcorehdr, and much
 *   much more. Once (if ever) freed, PG_reserved is cleared and they will
 *   be given to the page allocator.
 * - Pages falling into physical memory gaps - not IORESOURCE_SYSRAM. Trying
 *   to read/write these pages might end badly. Don't touch!
 * - The zero page(s)
 * - Pages not added to the page allocator when onlining a section because
 *   they were excluded via the online_page_callback() or because they are
 *   PG_hwpoison.
 * - Pages allocated in the context of kexec/kdump (loaded kernel image,
 *   control pages, vmcoreinfo)
 * - MMIO/DMA pages. Some architectures don't allow to ioremap pages that are
 *   not marked PG_reserved (as they might be in use by somebody else who does
 *   not respect the caching strategy).
 * - Pages part of an offline section (struct pages of offline sections should
 *   not be trusted as they will be initialized when first onlined).
 * - MCA pages on ia64
 * - Pages holding CPU notes for POWER Firmware Assisted Dump
 * - Device memory (e.g. PMEM, DAX, HMM)
 * Some PG_reserved pages will be excluded from the hibernation image.
 * PG_reserved does in general not hinder anybody from dumping or swapping
 * and is no longer required for remap_pfn_range(). ioremap might require it.
 * Consequently, PG_reserved for a page mapped into user space can indicate
 * the zero page, the vDSO, MMIO pages or device memory.
 *
 * The PG_private bitflag is set on pagecache pages if they contain filesystem
 * specific data (which is normally at page->private). It can be used by
 * private allocations for its own usage.
 *
 * During initiation of disk I/O, PG_locked is set. This bit is set before I/O
 * and cleared when writeback _starts_ or when read _completes_. PG_writeback
 * is set before writeback starts and cleared when it finishes.
 *
 * PG_locked also pins a page in pagecache, and blocks truncation of the file
 * while it is held.
 *
 * page_waitqueue(page) is a wait queue of all tasks waiting for the page
 * to become unlocked.
 *
 * PG_swapbacked is set when a page uses swap as a backing storage.  This are
 * usually PageAnon or shmem pages but please note that even anonymous pages
 * might lose their PG_swapbacked flag when they simply can be dropped (e.g. as
 * a result of MADV_FREE).
 *
 * PG_uptodate tells whether the page's contents is valid.  When a read
 * completes, the page becomes uptodate, unless a disk I/O error happened.
 *
 * PG_referenced, PG_reclaim are used for page reclaim for anonymous and
 * file-backed pagecache (see mm/vmscan.c).
 *
 * PG_error is set to indicate that an I/O error occurred on this page.
 *
 * PG_arch_1 is an architecture specific page state bit.  The generic code
 * guarantees that this bit is cleared for a page when it first is entered into
 * the page cache.
 *
 * PG_hwpoison indicates that a page got corrupted in hardware and contains
 * data with incorrect ECC bits that triggered a machine check. Accessing is
 * not safe since it may cause another machine check. Don't touch!
 */

/*
 * Don't use the *_dontuse flags.  Use the macros.  Otherwise you'll break
 * locked- and dirty-page accounting.
 *
 * The page flags field is split into two parts, the main flags area
 * which extends from the low bits upwards, and the fields area which
 * extends from the high bits downwards.
 *
 *  | FIELD | ... | FLAGS |
 *  N-1           ^       0
 *               (NR_PAGEFLAGS)
 *
 * The fields area is reserved for fields mapping zone, node (for NUMA) and
 * SPARSEMEM section (for variants of SPARSEMEM that require section ids like
 * SPARSEMEM_EXTREME with !SPARSEMEM_VMEMMAP).
 */
/**
 *  struct page->flags
 * 
 * 63    62 61  60 59             44 43                                               0  
 *  +------+------+-----------------+-------------------------------------------------+
 *  | node | zone |    LAST_CPUPID  |                   flags                         |
 *  +------+------+-----------------+-------------------------------------------------+
 *
 */
enum pageflags {
	PG_locked,		/* 页面已经上锁 Page is locked. Don't touch. 见`lock_page()`*/
	PG_referenced,  /* 页面是否被引用过, 和`PG_active`用于控制页面的活跃程序，在 kswapd 中使用 */
	PG_uptodate,    /* 标识页面的数据已经从块设备成功读取 */
	PG_dirty,       /* 页面内容发生过改变，但是还没有和外部存储器进行同步 */
	PG_lru,         /* 最近最少使用 zone->inactive_list */
	PG_active,      /* 是否活跃，和`PG_referenced`用于控制页面的活跃程序，在 kswapd 中使用 */
	PG_workingset,  /*  */
	PG_waiters,		/* 标识有进程正在等待这个页面。Page has waiters, check its waitqueue. Must be bit #7 and in the same byte as "PG_locked" */
	PG_error,       /* 页面操作过程发生过错误 */
	PG_slab,        /* 页面用于slab */
	PG_owner_priv_1,/* Owner use. If pagecache, fs may use*/
	PG_arch_1,      /* 与架构相关的页面状态位 */
	PG_reserved,    /* 该页面不可换出 */
	PG_private,		/* If pagecache, has fs-private data */
	PG_private_2,	/* If pagecache, has fs aux data */
	PG_writeback,	/* 页面正在向块设备回写，Page is under writeback */
	PG_head,		/* A head page */
	PG_mappedtodisk,/* Has blocks allocated on-disk */
	PG_reclaim,		/* 页面马上要被回收，To be reclaimed asap */
	PG_swapbacked,	/* 可以交换到磁盘(通常是匿名页) Page is backed by RAM/swap */
	PG_unevictable,	/* 页面不可回收 Page is "unevictable"  */
#ifdef CONFIG_MMU
	PG_mlocked,		/* 页面对应的VMA处于 mlocked 状态，Page is vma mlocked */
#endif
#ifdef CONFIG_ARCH_USES_PG_UNCACHED
	PG_uncached,	/* Page has been mapped as uncached */
#endif
#ifdef CONFIG_MEMORY_FAILURE
	PG_hwpoison,    /* hardware poisoned page. Don't touch */
#endif
#if defined(CONFIG_IDLE_PAGE_TRACKING) && defined(CONFIG_64BIT)
	PG_young,       /*  */
	PG_idle,        /*  */
#endif
#ifdef CONFIG_64BIT
	PG_arch_2,      /*  */
#endif
	__NR_PAGEFLAGS, /*  */

	/* Filesystems */
	PG_checked = PG_owner_priv_1,

	/* SwapBacked */
	PG_swapcache = PG_owner_priv_1,	/* 页面处于交换缓存中，Swap page: swp_entry_t in private */

	/* Two page bits are conscripted by FS-Cache to maintain local caching
	 * state.  These bits are set on pages belonging to the netfs's inodes
	 * when those inodes are being locally cached.
	 */
	PG_fscache = PG_private_2,	/* page backed by cache */

	/* XEN */
	/* Pinned in Xen as a read-only pagetable page. */
	PG_pinned = PG_owner_priv_1,
	/* Pinned as part of domain save (see xen_mm_pin_all()). */
	PG_savepinned = PG_dirty,
	/* Has a grant mapping of another (foreign) domain's page. */
	PG_foreign = PG_owner_priv_1,
	/* Remapped by swiotlb-xen. */
	PG_xen_remapped = PG_owner_priv_1,

	/* SLOB */
	PG_slob_free = PG_private,

	/**
	 *  Compound pages. Stored in first tail page's flags 
	 *  复合页面
	 */
	PG_double_map = PG_workingset,

	/* non-lru isolated movable page */
	PG_isolated = PG_reclaim,

	/* Only valid for buddy pages. Used to track pages that are reported */
	PG_reported = PG_uptodate,
};

#ifndef __GENERATING_BOUNDS_H

struct page;	/* forward declaration */

static inline struct page *compound_head(struct page *page) /* compound:化合物 */
{
	unsigned long head = READ_ONCE(page->compound_head);

	if (unlikely(head & 1))
		return (struct page *) (head - 1);  /*  */
	return page;
}

static __always_inline int PageTail(struct page *page)  /* TODO */
{
	return READ_ONCE(page->compound_head) & 1;
}

static __always_inline int PageCompound(struct page *page)
{
	return test_bit(PG_head, &page->flags) || PageTail(page);
}

#define	PAGE_POISON_PATTERN	-1l
static inline int PagePoisoned(const struct page *page)
{
	return page->flags == PAGE_POISON_PATTERN;
}

#ifdef CONFIG_DEBUG_VM
void page_init_poison(struct page *page, size_t size);
#else
/*  */
#endif

/*
 * Page flags policies wrt compound pages
 *
 * PF_POISONED_CHECK
 *     check if this struct page poisoned/uninitialized
 *
 * PF_ANY:
 *     the page flag is relevant for small, head and tail pages.
 *
 * PF_HEAD:
 *     for compound page all operations related to the page flag applied to
 *     head page.
 *
 * PF_ONLY_HEAD:
 *     for compound page, callers only ever operate on the head page.
 *
 * PF_NO_TAIL:
 *     modifications of the page flag must be done on small or head pages,
 *     checks can be done on tail pages too.
 *
 * PF_NO_COMPOUND:
 *     the page flag is not relevant for compound pages.
 *
 * PF_SECOND:
 *     the page flag is stored in the first tail page.
 */
#define PF_POISONED_CHECK(page) ({					\
		VM_BUG_ON_PGFLAGS(PagePoisoned(page), page);		\
		page; })
#define PF_ANY(page, enforce)	PF_POISONED_CHECK(page)
#define PF_HEAD(page, enforce)	PF_POISONED_CHECK(compound_head(page))
#define PF_ONLY_HEAD(page, enforce) ({					\
		VM_BUG_ON_PGFLAGS(PageTail(page), page);		\
		PF_POISONED_CHECK(page); })
#define PF_NO_TAIL(page, enforce) ({					\
		VM_BUG_ON_PGFLAGS(enforce && PageTail(page), page);	\
		PF_POISONED_CHECK(compound_head(page)); })
#define PF_NO_COMPOUND(page, enforce) ({				\
		VM_BUG_ON_PGFLAGS(enforce && PageCompound(page), page);	\
		PF_POISONED_CHECK(page); })
#define PF_SECOND(page, enforce) ({					\
		VM_BUG_ON_PGFLAGS(!PageHead(page), page);		\
		PF_POISONED_CHECK(&page[1]); })

/*
 * Macros to create function definitions for page flags
 */
#define TESTPAGEFLAG(uname, lname, policy)				\
static __always_inline int Page##uname(struct page *page)		\
	{ return test_bit(PG_##lname, &policy(page, 0)->flags); }

#define SETPAGEFLAG(uname, lname, policy)				\
static __always_inline void SetPage##uname(struct page *page)		\
	{ set_bit(PG_##lname, &policy(page, 1)->flags); }

#define CLEARPAGEFLAG(uname, lname, policy)				\
static __always_inline void ClearPage##uname(struct page *page)		\
	{ clear_bit(PG_##lname, &policy(page, 1)->flags); }

#define __SETPAGEFLAG(uname, lname, policy)				\
static __always_inline void __SetPage##uname(struct page *page)		\
	{ __set_bit(PG_##lname, &policy(page, 1)->flags); }

#define __CLEARPAGEFLAG(uname, lname, policy)				\
static __always_inline void __ClearPage##uname(struct page *page)	\
	{ __clear_bit(PG_##lname, &policy(page, 1)->flags); }

#define TESTSETFLAG(uname, lname, policy)				\
static __always_inline int TestSetPage##uname(struct page *page)	\
	{ return test_and_set_bit(PG_##lname, &policy(page, 1)->flags); }

#define TESTCLEARFLAG(uname, lname, policy)				\
static __always_inline int TestClearPage##uname(struct page *page)	\
	{ return test_and_clear_bit(PG_##lname, &policy(page, 1)->flags); }

#define PAGEFLAG(uname, lname, policy)					\
	TESTPAGEFLAG(uname, lname, policy)				\
	SETPAGEFLAG(uname, lname, policy)				\
	CLEARPAGEFLAG(uname, lname, policy)

#define __PAGEFLAG(uname, lname, policy)				\
	TESTPAGEFLAG(uname, lname, policy)				\
	__SETPAGEFLAG(uname, lname, policy)				\
	__CLEARPAGEFLAG(uname, lname, policy)

#define TESTSCFLAG(uname, lname, policy)				\
	TESTSETFLAG(uname, lname, policy)				\
	TESTCLEARFLAG(uname, lname, policy)

#define TESTPAGEFLAG_FALSE(uname)					\
static inline int Page##uname(const struct page *page) { return 0; }

#define SETPAGEFLAG_NOOP(uname)						\
static inline void SetPage##uname(struct page *page) {  }

#define CLEARPAGEFLAG_NOOP(uname)					\
static inline void ClearPage##uname(struct page *page) {  }

#define __CLEARPAGEFLAG_NOOP(uname)					\
static inline void __ClearPage##uname(struct page *page) {  }

#define TESTSETFLAG_FALSE(uname)					\
static inline int TestSetPage##uname(struct page *page) { return 0; }

#define TESTCLEARFLAG_FALSE(uname)					\
static inline int TestClearPage##uname(struct page *page) { return 0; }

#define PAGEFLAG_FALSE(uname) TESTPAGEFLAG_FALSE(uname)			\
	SETPAGEFLAG_NOOP(uname) CLEARPAGEFLAG_NOOP(uname)

#define TESTSCFLAG_FALSE(uname)						\
	TESTSETFLAG_FALSE(uname) TESTCLEARFLAG_FALSE(uname)

/**********************************************************************************************************************\
 *
 *   定义的函数是我写的，为什么会使用驼峰式命名呢？
 *
\**********************************************************************************************************************/
{/* +++ 2021年7月6日 */}


static __always_inline int PageLocked(struct page *page);
static __always_inline void __SetPageLocked(struct page *page);
static __always_inline void __ClearPageLocked(struct page *page);

static __always_inline int PageSlab(struct page *page);
static __always_inline void __SetPageSlab(struct page *page);
static __always_inline void __ClearPageSlab(struct page *page);

static __always_inline int PageSlobFree(struct page *page);
static __always_inline void __SetPageSlobFree(struct page *page);
static __always_inline void __ClearPageSlobFree(struct page *page);


static __always_inline int PageWaiters(struct page *page);
static __always_inline void SetPageWaiters(struct page *page);
static __always_inline void ClearPageWaiters(struct page *page);
static __always_inline void __ClearPageWaiters(struct page *page);


static __always_inline int PageError(struct page *page);
static __always_inline void SetPageError(struct page *page);
static __always_inline void ClearPageError(struct page *page);
static __always_inline int TestClearPageError(struct page *page);

static __always_inline int PageReferenced(struct page *page);
static __always_inline void SetPageReferenced(struct page *page);
static __always_inline void ClearPageReferenced(struct page *page); /* 清除   PG_referenced 位*/
/* 返回 页面 PG_referenced 标志位的值，并清除该标志位 */
static __always_inline int TestClearPageReferenced(struct page *page); 
static __always_inline void __SetPageReferenced(struct page *page);


static __always_inline int PageDirty(struct page *page);
static __always_inline void SetPageDirty(struct page *page);
static __always_inline void ClearPageDirty(struct page *page);
static __always_inline int TestClearPageDirty(struct page *page);
static __always_inline void __ClearPageDirty(struct page *page);
static __always_inline void __SetPageDirty(struct page *page);


/**
一下函数通过此脚本生成 荣涛 2021年7月6日
下面的函数有些可能没被定义，根据具体情况具体分析。

//[rongtao@localhost ~]$ cat page.sh 
#!/bin/bash

template="
static __always_inline int PageXXXX(struct page *page);\n
static __always_inline void SetPageXXXX(struct page *page);\n
static __always_inline void ClearPageXXXX(struct page *page);\n
static __always_inline int TestClearPageXXXX(struct page *page);\n
static __always_inline void __ClearPageXXXX(struct page *page);\n
static __always_inline void __SetPageXXXX(struct page *page);\n \n
"

flags="LRU Active Workingset Checked Pinned SavePinned Foreign XenRemapped Reserved SwapBacked Private Private2 Own
erPriv1 WriteBack MappedToDisk Reclaim Readahead SwapCache Unevictable Mlocked Uncached HWPoison Young Idle  Uptodate Head Huge HeadHuge DoubleMap TransHuge TransCompound TransCompoundMap Buddy Offline Kmemcg Table Guard Isolated "
for flag in $flags
do
echo -e $template | sed -e "s/XXXX/$flag/g"
done

#echo -e $template

*/
#if 0
static __always_inline int PageLRU(struct page *page);
 static __always_inline void SetPageLRU(struct page *page){PG_lru;}
 static __always_inline void ClearPageLRU(struct page *page);
 static __always_inline int TestClearPageLRU(struct page *page);
 static __always_inline void __ClearPageLRU(struct page *page);
 static __always_inline void __SetPageLRU(struct page *page);
 

static __always_inline int PageActive(struct page *page);
 static __always_inline void SetPageActive(struct page *page);
 static __always_inline void ClearPageActive(struct page *page);
 static __always_inline int TestClearPageActive(struct page *page);
 static __always_inline void __ClearPageActive(struct page *page);
 static __always_inline void __SetPageActive(struct page *page);
 

static __always_inline int PageWorkingset(struct page *page);
 static __always_inline void SetPageWorkingset(struct page *page);
 static __always_inline void ClearPageWorkingset(struct page *page);
 static __always_inline int TestClearPageWorkingset(struct page *page);
 static __always_inline void __ClearPageWorkingset(struct page *page);
 static __always_inline void __SetPageWorkingset(struct page *page);
 

static __always_inline int PageChecked(struct page *page);
 static __always_inline void SetPageChecked(struct page *page);
 static __always_inline void ClearPageChecked(struct page *page);
 static __always_inline int TestClearPageChecked(struct page *page);
 static __always_inline void __ClearPageChecked(struct page *page);
 static __always_inline void __SetPageChecked(struct page *page);
 

static __always_inline int PagePinned(struct page *page);
 static __always_inline void SetPagePinned(struct page *page);
 static __always_inline void ClearPagePinned(struct page *page);
 static __always_inline int TestClearPagePinned(struct page *page);
 static __always_inline void __ClearPagePinned(struct page *page);
 static __always_inline void __SetPagePinned(struct page *page);
 

static __always_inline int PageSavePinned(struct page *page);
 static __always_inline void SetPageSavePinned(struct page *page);
 static __always_inline void ClearPageSavePinned(struct page *page);
 static __always_inline int TestClearPageSavePinned(struct page *page);
 static __always_inline void __ClearPageSavePinned(struct page *page);
 static __always_inline void __SetPageSavePinned(struct page *page);
 

static __always_inline int PageForeign(struct page *page);
 static __always_inline void SetPageForeign(struct page *page);
 static __always_inline void ClearPageForeign(struct page *page);
 static __always_inline int TestClearPageForeign(struct page *page);
 static __always_inline void __ClearPageForeign(struct page *page);
 static __always_inline void __SetPageForeign(struct page *page);
 

static __always_inline int PageXenRemapped(struct page *page);
 static __always_inline void SetPageXenRemapped(struct page *page);
 static __always_inline void ClearPageXenRemapped(struct page *page);
 static __always_inline int TestClearPageXenRemapped(struct page *page);
 static __always_inline void __ClearPageXenRemapped(struct page *page);
 static __always_inline void __SetPageXenRemapped(struct page *page);
 

static __always_inline int PageReserved(struct page *page);
 static __always_inline void SetPageReserved(struct page *page);
 static __always_inline void ClearPageReserved(struct page *page);
 static __always_inline int TestClearPageReserved(struct page *page);
 static __always_inline void __ClearPageReserved(struct page *page);
 static __always_inline void __SetPageReserved(struct page *page);
 

static __always_inline int PageSwapBacked(struct page *page);
 static __always_inline void SetPageSwapBacked(struct page *page);
 static __always_inline void ClearPageSwapBacked(struct page *page);
 static __always_inline int TestClearPageSwapBacked(struct page *page);
 static __always_inline void __ClearPageSwapBacked(struct page *page);
 static __always_inline void __SetPageSwapBacked(struct page *page);
 

static __always_inline int PagePrivate(struct page *page);
 static __always_inline void SetPagePrivate(struct page *page);
 static __always_inline void ClearPagePrivate(struct page *page);
 static __always_inline int TestClearPagePrivate(struct page *page);
 static __always_inline void __ClearPagePrivate(struct page *page);
 static __always_inline void __SetPagePrivate(struct page *page);
 

static __always_inline int PagePrivate2(struct page *page);
 static __always_inline void SetPagePrivate2(struct page *page);
 static __always_inline void ClearPagePrivate2(struct page *page);
 static __always_inline int TestClearPagePrivate2(struct page *page);
 static __always_inline void __ClearPagePrivate2(struct page *page);
 static __always_inline void __SetPagePrivate2(struct page *page);
 

static __always_inline int PageOwnerPriv1(struct page *page);
 static __always_inline void SetPageOwnerPriv1(struct page *page);
 static __always_inline void ClearPageOwnerPriv1(struct page *page);
 static __always_inline int TestClearPageOwnerPriv1(struct page *page);
 static __always_inline void __ClearPageOwnerPriv1(struct page *page);
 static __always_inline void __SetPageOwnerPriv1(struct page *page);
 

static __always_inline int PageWriteBack(struct page *page);
 static __always_inline void SetPageWriteBack(struct page *page);
 static __always_inline void ClearPageWriteBack(struct page *page);
 static __always_inline int TestClearPageWriteBack(struct page *page);
 static __always_inline void __ClearPageWriteBack(struct page *page);
 static __always_inline void __SetPageWriteBack(struct page *page);
 

static __always_inline int PageMappedToDisk(struct page *page);
 static __always_inline void SetPageMappedToDisk(struct page *page);
 static __always_inline void ClearPageMappedToDisk(struct page *page);
 static __always_inline int TestClearPageMappedToDisk(struct page *page);
 static __always_inline void __ClearPageMappedToDisk(struct page *page);
 static __always_inline void __SetPageMappedToDisk(struct page *page);
 

static __always_inline int PageReclaim(struct page *page);
 static __always_inline void SetPageReclaim(struct page *page);
 static __always_inline void ClearPageReclaim(struct page *page);
 static __always_inline int TestClearPageReclaim(struct page *page);
 static __always_inline void __ClearPageReclaim(struct page *page);
 static __always_inline void __SetPageReclaim(struct page *page);
 

static __always_inline int PageReadahead(struct page *page);
 static __always_inline void SetPageReadahead(struct page *page);
 static __always_inline void ClearPageReadahead(struct page *page);
 static __always_inline int TestClearPageReadahead(struct page *page);
 static __always_inline void __ClearPageReadahead(struct page *page);
 static __always_inline void __SetPageReadahead(struct page *page);
 

static __always_inline int PageSwapCache(struct page *page);
 static __always_inline void SetPageSwapCache(struct page *page);
 static __always_inline void ClearPageSwapCache(struct page *page);
 static __always_inline int TestClearPageSwapCache(struct page *page);
 static __always_inline void __ClearPageSwapCache(struct page *page);
 static __always_inline void __SetPageSwapCache(struct page *page);
 

static __always_inline int PageUnevictable(struct page *page);  /* 不可回收 */
 static __always_inline void SetPageUnevictable(struct page *page);
 static __always_inline void ClearPageUnevictable(struct page *page);
 static __always_inline int TestClearPageUnevictable(struct page *page);
 static __always_inline void __ClearPageUnevictable(struct page *page);
 static __always_inline void __SetPageUnevictable(struct page *page);
 

static __always_inline int PageMlocked(struct page *page);
 static __always_inline void SetPageMlocked(struct page *page);
 static __always_inline void ClearPageMlocked(struct page *page);
 static __always_inline int TestClearPageMlocked(struct page *page);
 static __always_inline void __ClearPageMlocked(struct page *page);
 static __always_inline void __SetPageMlocked(struct page *page);
 

static __always_inline int PageUncached(struct page *page);
 static __always_inline void SetPageUncached(struct page *page);
 static __always_inline void ClearPageUncached(struct page *page);
 static __always_inline int TestClearPageUncached(struct page *page);
 static __always_inline void __ClearPageUncached(struct page *page);
 static __always_inline void __SetPageUncached(struct page *page);
 

static __always_inline int PageHWPoison(struct page *page);
 static __always_inline void SetPageHWPoison(struct page *page);
 static __always_inline void ClearPageHWPoison(struct page *page);
 static __always_inline int TestClearPageHWPoison(struct page *page);
 static __always_inline void __ClearPageHWPoison(struct page *page);
 static __always_inline void __SetPageHWPoison(struct page *page);
 

static __always_inline int PageYoung(struct page *page);
 static __always_inline void SetPageYoung(struct page *page);
 static __always_inline void ClearPageYoung(struct page *page);
 static __always_inline int TestClearPageYoung(struct page *page);
 static __always_inline void __ClearPageYoung(struct page *page);
 static __always_inline void __SetPageYoung(struct page *page);
 

static __always_inline int PageIdle(struct page *page);
 static __always_inline void SetPageIdle(struct page *page);
 static __always_inline void ClearPageIdle(struct page *page);
 static __always_inline int TestClearPageIdle(struct page *page);
 static __always_inline void __ClearPageIdle(struct page *page);
 static __always_inline void __SetPageIdle(struct page *page);
 

static __always_inline int PageUptodate(struct page *page);
 static __always_inline void SetPageUptodate(struct page *page);
 static __always_inline void ClearPageUptodate(struct page *page);
 static __always_inline int TestClearPageUptodate(struct page *page);
 static __always_inline void __ClearPageUptodate(struct page *page);
 static __always_inline void __SetPageUptodate(struct page *page);
 

static __always_inline int PageHead(struct page *page);
 static __always_inline void SetPageHead(struct page *page);
 static __always_inline void ClearPageHead(struct page *page);
 static __always_inline int TestClearPageHead(struct page *page);
 static __always_inline void __ClearPageHead(struct page *page);
 static __always_inline void __SetPageHead(struct page *page);
 

static __always_inline int PageHuge(struct page *page);
 static __always_inline void SetPageHuge(struct page *page);
 static __always_inline void ClearPageHuge(struct page *page);
 static __always_inline int TestClearPageHuge(struct page *page);
 static __always_inline void __ClearPageHuge(struct page *page);
 static __always_inline void __SetPageHuge(struct page *page);
 

static __always_inline int PageHeadHuge(struct page *page);
 static __always_inline void SetPageHeadHuge(struct page *page);
 static __always_inline void ClearPageHeadHuge(struct page *page);
 static __always_inline int TestClearPageHeadHuge(struct page *page);
 static __always_inline void __ClearPageHeadHuge(struct page *page);
 static __always_inline void __SetPageHeadHuge(struct page *page);
 

static __always_inline int PageDoubleMap(struct page *page);
 static __always_inline void SetPageDoubleMap(struct page *page);
 static __always_inline void ClearPageDoubleMap(struct page *page);
 static __always_inline int TestClearPageDoubleMap(struct page *page);
 static __always_inline void __ClearPageDoubleMap(struct page *page);
 static __always_inline void __SetPageDoubleMap(struct page *page);
 

static __always_inline int PageTransHuge(struct page *page);
 static __always_inline void SetPageTransHuge(struct page *page);
 static __always_inline void ClearPageTransHuge(struct page *page);
 static __always_inline int TestClearPageTransHuge(struct page *page);
 static __always_inline void __ClearPageTransHuge(struct page *page);
 static __always_inline void __SetPageTransHuge(struct page *page);
 

static __always_inline int PageTransCompound(struct page *page);
 static __always_inline void SetPageTransCompound(struct page *page);
 static __always_inline void ClearPageTransCompound(struct page *page);
 static __always_inline int TestClearPageTransCompound(struct page *page);
 static __always_inline void __ClearPageTransCompound(struct page *page);
 static __always_inline void __SetPageTransCompound(struct page *page);
 

static __always_inline int PageTransCompoundMap(struct page *page);
 static __always_inline void SetPageTransCompoundMap(struct page *page);
 static __always_inline void ClearPageTransCompoundMap(struct page *page);
 static __always_inline int TestClearPageTransCompoundMap(struct page *page);
 static __always_inline void __ClearPageTransCompoundMap(struct page *page);
 static __always_inline void __SetPageTransCompoundMap(struct page *page);
 

static __always_inline int PageBuddy(struct page *page);
 static __always_inline void SetPageBuddy(struct page *page);
 static __always_inline void ClearPageBuddy(struct page *page);
 static __always_inline int TestClearPageBuddy(struct page *page);
 static __always_inline void __ClearPageBuddy(struct page *page);
 static __always_inline void __SetPageBuddy(struct page *page);
 

static __always_inline int PageOffline(struct page *page);
 static __always_inline void SetPageOffline(struct page *page);
 static __always_inline void ClearPageOffline(struct page *page);
 static __always_inline int TestClearPageOffline(struct page *page);
 static __always_inline void __ClearPageOffline(struct page *page);
 static __always_inline void __SetPageOffline(struct page *page);
 

static __always_inline int PageKmemcg(struct page *page);
 static __always_inline void SetPageKmemcg(struct page *page);
 static __always_inline void ClearPageKmemcg(struct page *page);
 static __always_inline int TestClearPageKmemcg(struct page *page);
 static __always_inline void __ClearPageKmemcg(struct page *page);
 static __always_inline void __SetPageKmemcg(struct page *page);
 

static __always_inline int PageTable(struct page *page);
 static __always_inline void SetPageTable(struct page *page);
 static __always_inline void ClearPageTable(struct page *page);
 static __always_inline int TestClearPageTable(struct page *page);
 static __always_inline void __ClearPageTable(struct page *page);
 static __always_inline void __SetPageTable(struct page *page);
 

static __always_inline int PageGuard(struct page *page);
 static __always_inline void SetPageGuard(struct page *page);
 static __always_inline void ClearPageGuard(struct page *page);
 static __always_inline int TestClearPageGuard(struct page *page);
 static __always_inline void __ClearPageGuard(struct page *page);
 static __always_inline void __SetPageGuard(struct page *page);
 

static __always_inline int PageIsolated(struct page *page);
 static __always_inline void SetPageIsolated(struct page *page);
 static __always_inline void ClearPageIsolated(struct page *page);
 static __always_inline int TestClearPageIsolated(struct page *page);
 static __always_inline void __ClearPageIsolated(struct page *page);
 static __always_inline void __SetPageIsolated(struct page *page);

#endif

{/* +++ 2021年7月6日 */}

__PAGEFLAG(Locked, locked, PF_NO_TAIL);
PAGEFLAG(Waiters, waiters, PF_ONLY_HEAD) ;
__CLEARPAGEFLAG(Waiters, waiters, PF_ONLY_HEAD);
PAGEFLAG(Error, error, PF_NO_TAIL) 
TESTCLEARFLAG(Error, error, PF_NO_TAIL)
PAGEFLAG(Referenced, referenced, PF_HEAD)
TESTCLEARFLAG(Referenced, referenced, PF_HEAD)
__SETPAGEFLAG(Referenced, referenced, PF_HEAD)
PAGEFLAG(Dirty, dirty, PF_HEAD) 
TESTSCFLAG(Dirty, dirty, PF_HEAD)
__CLEARPAGEFLAG(Dirty, dirty, PF_HEAD)
PAGEFLAG(LRU, lru, PF_HEAD) 
__CLEARPAGEFLAG(LRU, lru, PF_HEAD)
PAGEFLAG(Active, active, PF_HEAD)
__CLEARPAGEFLAG(Active, active, PF_HEAD)
TESTCLEARFLAG(Active, active, PF_HEAD)
PAGEFLAG(Workingset, workingset, PF_HEAD)
TESTCLEARFLAG(Workingset, workingset, PF_HEAD)
__PAGEFLAG(Slab, slab, PF_NO_TAIL)
__PAGEFLAG(SlobFree, slob_free, PF_NO_TAIL)
PAGEFLAG(Checked, checked, PF_NO_COMPOUND)	   /* Used by some filesystems */

/* Xen */
PAGEFLAG(Pinned, pinned, PF_NO_COMPOUND)
TESTSCFLAG(Pinned, pinned, PF_NO_COMPOUND)
PAGEFLAG(SavePinned, savepinned, PF_NO_COMPOUND);
PAGEFLAG(Foreign, foreign, PF_NO_COMPOUND);
PAGEFLAG(XenRemapped, xen_remapped, PF_NO_COMPOUND)
TESTCLEARFLAG(XenRemapped, xen_remapped, PF_NO_COMPOUND)
    {/* +++ */}

PAGEFLAG(Reserved, reserved, PF_NO_COMPOUND)
__CLEARPAGEFLAG(Reserved, reserved, PF_NO_COMPOUND)
__SETPAGEFLAG(Reserved, reserved, PF_NO_COMPOUND)
PAGEFLAG(SwapBacked, swapbacked, PF_NO_TAIL)
__CLEARPAGEFLAG(SwapBacked, swapbacked, PF_NO_TAIL)
__SETPAGEFLAG(SwapBacked, swapbacked, PF_NO_TAIL)
    {/* +++ */}

/*
 * Private page markings that may be used by the filesystem that owns the page
 * for its own purposes.
 * - PG_private and PG_private_2 cause releasepage() and co to be invoked
 */
PAGEFLAG(Private, private, PF_ANY)
__SETPAGEFLAG(Private, private, PF_ANY)
__CLEARPAGEFLAG(Private, private, PF_ANY)
PAGEFLAG(Private2, private_2, PF_ANY) 
TESTSCFLAG(Private2, private_2, PF_ANY)
PAGEFLAG(OwnerPriv1, owner_priv_1, PF_ANY)
TESTCLEARFLAG(OwnerPriv1, owner_priv_1, PF_ANY)

/*
 * Only test-and-set exist for PG_writeback.  The unconditional operators are
 * risky: they bypass page accounting.
 */
TESTPAGEFLAG(Writeback, writeback, PF_NO_TAIL)
TESTSCFLAG(Writeback, writeback, PF_NO_TAIL)
PAGEFLAG(MappedToDisk, mappedtodisk, PF_NO_TAIL)

/* PG_readahead is only used for reads; PG_reclaim is only for writes */
PAGEFLAG(Reclaim, reclaim, PF_NO_TAIL)
TESTCLEARFLAG(Reclaim, reclaim, PF_NO_TAIL)
PAGEFLAG(Readahead, reclaim, PF_NO_COMPOUND)
TESTCLEARFLAG(Readahead, reclaim, PF_NO_COMPOUND)
    {/* +++ */}


#if __rtoax_gcc_compile_with_E__


static __always_inline int PageLocked(struct page *page) { 
    return test_bit(PG_locked, &({ VM_BUG_ON_PGFLAGS(0 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void __SetPageLocked(struct page *page) { 
    __set_bit(PG_locked, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void __ClearPageLocked(struct page *page) { 
    __clear_bit(PG_locked, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }

static __always_inline int PageWaiters(struct page *page) { 
    return test_bit(PG_waiters, &({ VM_BUG_ON_PGFLAGS(PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void SetPageWaiters(struct page *page) { 
    set_bit(PG_waiters, &({ VM_BUG_ON_PGFLAGS(PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void ClearPageWaiters(struct page *page) { 
    clear_bit(PG_waiters, &({ VM_BUG_ON_PGFLAGS(PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void __ClearPageWaiters(struct page *page) { 
    __clear_bit(PG_waiters, &({ VM_BUG_ON_PGFLAGS(PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    }

static __always_inline int PageError(struct page *page) { 
    return test_bit(PG_error, &({ VM_BUG_ON_PGFLAGS(0 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void SetPageError(struct page *page) { 
    set_bit(PG_error, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void ClearPageError(struct page *page) { 
    clear_bit(PG_error, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline int TestClearPageError(struct page *page) { 
    return test_and_clear_bit(PG_error, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }

static __always_inline int PageReferenced(struct page *page) { 
    return test_bit(PG_referenced, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void SetPageReferenced(struct page *page) { 
    set_bit(PG_referenced, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void ClearPageReferenced(struct page *page) { 
    clear_bit(PG_referenced, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
    }
 
static __always_inline int TestClearPageReferenced(struct page *page) { 
    return test_and_clear_bit(PG_referenced, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
    }
 
static __always_inline void __SetPageReferenced(struct page *page) { 
    __set_bit(PG_referenced, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
    }

static __always_inline int PageDirty(struct page *page) { 
    return test_bit(PG_dirty, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void SetPageDirty(struct page *page) { 
    set_bit(PG_dirty, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void ClearPageDirty(struct page *page) { 
    clear_bit(PG_dirty, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline int TestSetPageDirty(struct page *page) { 
    return test_and_set_bit(PG_dirty, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline int TestClearPageDirty(struct page *page) { 
    return test_and_clear_bit(PG_dirty, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
    }
 
static __always_inline void __ClearPageDirty(struct page *page) { 
    __clear_bit(PG_dirty, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
    }

static __always_inline int PageLRU(struct page *page) { 
    return test_bit(PG_lru, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void SetPageLRU(struct page *page) { 
    set_bit(PG_lru, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void ClearPageLRU(struct page *page) { 
    clear_bit(PG_lru, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void __ClearPageLRU(struct page *page) { 
    __clear_bit(PG_lru, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
    }

static __always_inline int PageActive(struct page *page) { 
    return test_bit(PG_active, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void SetPageActive(struct page *page) { 
    set_bit(PG_active, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void ClearPageActive(struct page *page) { 
    clear_bit(PG_active, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void __ClearPageActive(struct page *page) { 
    __clear_bit(PG_active, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
    }
 
static __always_inline int TestClearPageActive(struct page *page) { 
    return test_and_clear_bit(PG_active, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
    }

static __always_inline int PageWorkingset(struct page *page) { 
    return test_bit(PG_workingset, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void SetPageWorkingset(struct page *page) { 
    set_bit(PG_workingset, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
}
static __always_inline void ClearPageWorkingset(struct page *page) { 
    clear_bit(PG_workingset, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
    }
 
static __always_inline int TestClearPageWorkingset(struct page *page) { 
    return test_and_clear_bit(PG_workingset, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    })->flags); 
    }

static __always_inline int PageSlab(struct page *page) { 
    return test_bit(PG_slab, &({ VM_BUG_ON_PGFLAGS(0 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void __SetPageSlab(struct page *page) { 
    __set_bit(PG_slab, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void __ClearPageSlab(struct page *page) { 
    __clear_bit(PG_slab, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }

static __always_inline int PageSlobFree(struct page *page) { 
    return test_bit(PG_slob_free, &({ VM_BUG_ON_PGFLAGS(0 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void __SetPageSlobFree(struct page *page) { 
    __set_bit(PG_slob_free, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void __ClearPageSlobFree(struct page *page) { 
    __clear_bit(PG_slob_free, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }

static __always_inline int PageChecked(struct page *page) { 
    return test_bit(PG_checked, &({ VM_BUG_ON_PGFLAGS(0 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void SetPageChecked(struct page *page) { 
    set_bit(PG_checked, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void ClearPageChecked(struct page *page) { 
    clear_bit(PG_checked, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    }



static __always_inline int PagePinned(struct page *page) { 
    return test_bit(PG_pinned, &({ VM_BUG_ON_PGFLAGS(0 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void SetPagePinned(struct page *page) { 
    set_bit(PG_pinned, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void ClearPagePinned(struct page *page) { 
    clear_bit(PG_pinned, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    }
 
static __always_inline int TestSetPagePinned(struct page *page) { 
    return test_and_set_bit(PG_pinned, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline int TestClearPagePinned(struct page *page) { 
    return test_and_clear_bit(PG_pinned, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    }

static __always_inline int PageSavePinned(struct page *page) { 
    return test_bit(PG_savepinned, &({ VM_BUG_ON_PGFLAGS(0 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void SetPageSavePinned(struct page *page) { 
    set_bit(PG_savepinned, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void ClearPageSavePinned(struct page *page) { 
    clear_bit(PG_savepinned, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    };

static __always_inline int PageForeign(struct page *page) { 
    return test_bit(PG_foreign, &({ VM_BUG_ON_PGFLAGS(0 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void SetPageForeign(struct page *page) { 
    set_bit(PG_foreign, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void ClearPageForeign(struct page *page) { 
    clear_bit(PG_foreign, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    };

static __always_inline int PageXenRemapped(struct page *page) { 
    return test_bit(PG_xen_remapped, &({ VM_BUG_ON_PGFLAGS(0 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void SetPageXenRemapped(struct page *page) { 
    set_bit(PG_xen_remapped, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void ClearPageXenRemapped(struct page *page) { 
    clear_bit(PG_xen_remapped, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    }
 
static __always_inline int TestClearPageXenRemapped(struct page *page) { 
    return test_and_clear_bit(PG_xen_remapped, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    }


static __always_inline int PageReserved(struct page *page) { 
    return test_bit(PG_reserved, &({ VM_BUG_ON_PGFLAGS(0 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void SetPageReserved(struct page *page) { 
    set_bit(PG_reserved, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void ClearPageReserved(struct page *page) { 
    clear_bit(PG_reserved, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    }
 
static __always_inline void __ClearPageReserved(struct page *page) { 
    __clear_bit(PG_reserved, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    }
 
static __always_inline void __SetPageReserved(struct page *page) { 
    __set_bit(PG_reserved, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    }

/**
 *  
 */
static __always_inline int PageSwapBacked(struct page *page) { 
    return test_bit(PG_swapbacked, &({ VM_BUG_ON_PGFLAGS(0 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void SetPageSwapBacked(struct page *page) { 
    set_bit(PG_swapbacked, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void ClearPageSwapBacked(struct page *page) { 
    clear_bit(PG_swapbacked, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }
 
static __always_inline void __ClearPageSwapBacked(struct page *page) { 
    __clear_bit(PG_swapbacked, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }
 
static __always_inline void __SetPageSwapBacked(struct page *page) { 
    __set_bit(PG_swapbacked, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }







static __always_inline int PagePrivate(struct page *page) { 
    return test_bit(PG_private, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
}
static __always_inline void SetPagePrivate(struct page *page) { 
    set_bit(PG_private, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
}
static __always_inline void ClearPagePrivate(struct page *page) { 
    clear_bit(PG_private, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
}
static __always_inline void __SetPagePrivate(struct page *page) { 
    __set_bit(PG_private, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
    }
 
static __always_inline void __ClearPagePrivate(struct page *page) { 
    __clear_bit(PG_private, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
    }

static __always_inline int PagePrivate2(struct page *page) { 
    return test_bit(PG_private_2, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
}
static __always_inline void SetPagePrivate2(struct page *page) { 
    set_bit(PG_private_2, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
}
static __always_inline void ClearPagePrivate2(struct page *page) { 
    clear_bit(PG_private_2, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
}
static __always_inline int TestSetPagePrivate2(struct page *page) { 
    return test_and_set_bit(PG_private_2, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
}
static __always_inline int TestClearPagePrivate2(struct page *page) { 
    return test_and_clear_bit(PG_private_2, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
    }

static __always_inline int PageOwnerPriv1(struct page *page) { 
    return test_bit(PG_owner_priv_1, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
}
static __always_inline void SetPageOwnerPriv1(struct page *page) { 
    set_bit(PG_owner_priv_1, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
}
static __always_inline void ClearPageOwnerPriv1(struct page *page) { 
    clear_bit(PG_owner_priv_1, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
    }
 
static __always_inline int TestClearPageOwnerPriv1(struct page *page) { 
    return test_and_clear_bit(PG_owner_priv_1, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    })->flags); 
    }






static __always_inline int PageWriteback(struct page *page) { 
    return test_bit(PG_writeback, &({ VM_BUG_ON_PGFLAGS(0 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }
 
static __always_inline int TestSetPageWriteback(struct page *page) { 
    return test_and_set_bit(PG_writeback, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline int TestClearPageWriteback(struct page *page) { 
    return test_and_clear_bit(PG_writeback, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }

static __always_inline int PageMappedToDisk(struct page *page) { 
    return test_bit(PG_mappedtodisk, &({ VM_BUG_ON_PGFLAGS(0 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void SetPageMappedToDisk(struct page *page) { 
    set_bit(PG_mappedtodisk, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void ClearPageMappedToDisk(struct page *page) { 
    clear_bit(PG_mappedtodisk, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }



static __always_inline int PageReclaim(struct page *page) { 
    return test_bit(PG_reclaim, &({ VM_BUG_ON_PGFLAGS(0 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void SetPageReclaim(struct page *page) { 
    set_bit(PG_reclaim, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
}
static __always_inline void ClearPageReclaim(struct page *page) { 
    clear_bit(PG_reclaim, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }
 
static __always_inline int TestClearPageReclaim(struct page *page) { 
    return test_and_clear_bit(PG_reclaim, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); 
    compound_head(page); 
    }); 
    })->flags); 
    }

static __always_inline int PageReadahead(struct page *page) { 
    return test_bit(PG_reclaim, &({ VM_BUG_ON_PGFLAGS(0 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void SetPageReadahead(struct page *page) { 
    set_bit(PG_reclaim, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
}
static __always_inline void ClearPageReadahead(struct page *page) { 
    clear_bit(PG_reclaim, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    }
 
static __always_inline int TestClearPageReadahead(struct page *page) { 
    return test_and_clear_bit(PG_reclaim, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); 
    ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); 
    page; 
    }); 
    })->flags); 
    }

#endif //__rtoax_gcc_compile_with_E__


#ifdef CONFIG_HIGHMEM
/*
 * Must use a macro here due to header dependency issues. page_zone() is not
 * available at this point.
 */{}
//#define PageHighMem(__p) is_highmem_idx(page_zonenum(__p))
{}
#else
PAGEFLAG_FALSE(HighMem)
#endif
{}

#ifdef CONFIG_SWAP
/**
 *  已经为页面分配了 交换空间
 */
static __always_inline int PageSwapCache(struct page *page)
{
#ifdef CONFIG_THP_SWAP
	page = compound_head(page);
#endif
	return PageSwapBacked(page) && test_bit(PG_swapcache, &page->flags);

}
{/* +++ */}
SETPAGEFLAG(SwapCache, swapcache, PF_NO_TAIL)
CLEARPAGEFLAG(SwapCache, swapcache, PF_NO_TAIL)
#else
/*  */
#endif

PAGEFLAG(Unevictable, unevictable, PF_HEAD)
	__CLEARPAGEFLAG(Unevictable, unevictable, PF_HEAD)
	TESTCLEARFLAG(Unevictable, unevictable, PF_HEAD)

#ifdef CONFIG_MMU
PAGEFLAG(Mlocked, mlocked, PF_NO_TAIL)
	__CLEARPAGEFLAG(Mlocked, mlocked, PF_NO_TAIL)
	TESTSCFLAG(Mlocked, mlocked, PF_NO_TAIL)
#else
/*  */
#endif

#ifdef CONFIG_ARCH_USES_PG_UNCACHED
PAGEFLAG(Uncached, uncached, PF_NO_COMPOUND)
#else
/*  */
#endif

#ifdef CONFIG_MEMORY_FAILURE
PAGEFLAG(HWPoison, hwpoison, PF_ANY){}
TESTSCFLAG(HWPoison, hwpoison, PF_ANY){}
{}
#define __PG_HWPOISON (1UL << PG_hwpoison)

extern bool take_page_off_buddy(struct page *page);
#else
/*  */
#endif


#if __rtoax_gcc_compile_with_E__

static __always_inline void SetPageSwapCache(struct page *page) { 
    set_bit(PG_swapcache, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    ); }
    )->flags); }
    

static __always_inline void ClearPageSwapCache(struct page *page) { 
    clear_bit(PG_swapcache, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    ); }
    )->flags); }
    





static __always_inline int PageUnevictable(struct page *page) { 
    return test_bit(PG_unevictable, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    )->flags); }
     
static __always_inline void SetPageUnevictable(struct page *page) { 
    set_bit(PG_unevictable, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    )->flags); }
     
static __always_inline void ClearPageUnevictable(struct page *page) { 
    clear_bit(PG_unevictable, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    )->flags); }
    
 
static __always_inline void __ClearPageUnevictable(struct page *page) { 
    __clear_bit(PG_unevictable, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    )->flags); }
    
 
static __always_inline int TestClearPageUnevictable(struct page *page) { 
    return test_and_clear_bit(PG_unevictable, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    )->flags); }
    



static __always_inline int PageMlocked(struct page *page) { 
    return test_bit(PG_mlocked, &({ VM_BUG_ON_PGFLAGS(0 && PageTail(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    ); }
    )->flags); }
     
static __always_inline void SetPageMlocked(struct page *page) { 
    set_bit(PG_mlocked, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    ); }
    )->flags); }
     
static __always_inline void ClearPageMlocked(struct page *page) { 
    clear_bit(PG_mlocked, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    ); }
    )->flags); }
    
 
static __always_inline void __ClearPageMlocked(struct page *page) { 
    __clear_bit(PG_mlocked, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    ); }
    )->flags); }
    
 
static __always_inline int TestSetPageMlocked(struct page *page) { 
    return test_and_set_bit(PG_mlocked, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    ); }
    )->flags); }
     
static __always_inline int TestClearPageMlocked(struct page *page) { 
    return test_and_clear_bit(PG_mlocked, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }
    ); }
    )->flags); }
    







static __always_inline int PageUncached(struct page *page) { 
    return test_bit(PG_uncached, &({ VM_BUG_ON_PGFLAGS(0 && PageCompound(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; }
    ); }
    )->flags); }
     
static __always_inline void SetPageUncached(struct page *page) { 
    set_bit(PG_uncached, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; }
    ); }
    )->flags); }
     
static __always_inline void ClearPageUncached(struct page *page) { 
    clear_bit(PG_uncached, &({ VM_BUG_ON_PGFLAGS(1 && PageCompound(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; }
    ); }
    )->flags); }
    






static __always_inline int PageHWPoison(struct page *page) { 
    return test_bit(PG_hwpoison, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; }
    )->flags); }
     
static __always_inline void SetPageHWPoison(struct page *page) { 
    set_bit(PG_hwpoison, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; }
    )->flags); }
     
static __always_inline void ClearPageHWPoison(struct page *page) { 
    clear_bit(PG_hwpoison, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; }
    )->flags); }
    

static __always_inline int TestSetPageHWPoison(struct page *page) { 
    return test_and_set_bit(PG_hwpoison, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; }
    )->flags); }
     
static __always_inline int TestClearPageHWPoison(struct page *page) { 
    return test_and_clear_bit(PG_hwpoison, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; }
    )->flags); }
    



#endif //__rtoax_gcc_compile_with_E__


#if defined(CONFIG_IDLE_PAGE_TRACKING) && defined(CONFIG_64BIT)
TESTPAGEFLAG(Young, young, PF_ANY)
SETPAGEFLAG(Young, young, PF_ANY)
TESTCLEARFLAG(Young, young, PF_ANY)
PAGEFLAG(Idle, idle, PF_ANY)
#endif
{}

#if __rtoax_gcc_compile_with_E__


static __always_inline int PageYoung(struct page *page) { 
    return test_bit(PG_young, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); }

static __always_inline void SetPageYoung(struct page *page) { 
    set_bit(PG_young, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); }

static __always_inline int TestClearPageYoung(struct page *page) { 
    return test_and_clear_bit(PG_young, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); }

static __always_inline int PageIdle(struct page *page) { 
    return test_bit(PG_idle, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); } 
static __always_inline void SetPageIdle(struct page *page) { 
    set_bit(PG_idle, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); } 
static __always_inline void ClearPageIdle(struct page *page) { 
    clear_bit(PG_idle, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); }


#endif //__rtoax_gcc_compile_with_E__

/*
 * PageReported() is used to track reported free pages within the Buddy
 * allocator. We can use the non-atomic version of the test and set
 * operations as both should be shielded with the zone lock to prevent
 * any possible races on the setting or clearing of the bit.
 */
{/*++++ 2021年7月6日*/}
//__PAGEFLAG(Reported, reported, PF_NO_COMPOUND)
static __always_inline int PageReported(struct page *page)        
{ return test_bit(PG_reported, &PF_NO_COMPOUND(page, 0)->flags); }

static __always_inline void __SetPageReported(struct page *page)
{ __set_bit(PG_reported, &PF_NO_COMPOUND(page, 1)->flags); }

static __always_inline void __ClearPageReported(struct page *page)
{ __clear_bit(PG_reported, &PF_NO_COMPOUND(page, 1)->flags); }




{/*++++ 2021年7月6日*/}
/*
 * On an anonymous page mapped into a user virtual memory area,
 * page->mapping points to its anon_vma, not to a struct address_space;
 * with the PAGE_MAPPING_ANON bit set to distinguish it.  See rmap.h.
 *
 * On an anonymous page in a VM_MERGEABLE area, if CONFIG_KSM is enabled,
 * the PAGE_MAPPING_MOVABLE bit may be set along with the PAGE_MAPPING_ANON
 * bit; and then page->mapping points, not to an anon_vma, but to a private
 * structure which KSM associates with that merged page.  See ksm.h.
 *
 * PAGE_MAPPING_KSM without PAGE_MAPPING_ANON is used for non-lru movable
 * page and then page->mapping points a struct address_space.
 *
 * Please note that, confusingly, "page_mapping" refers to the inode
 * address_space which maps the page from disk; whereas "page_mapped"
 * refers to user virtual address space into which the page is mapped.
 *
 * 见`struct page->mapping[0-1]`
 */
#define PAGE_MAPPING_ANON	    0x1 /* 匿名页面 */
#define PAGE_MAPPING_MOVABLE	0x2
#define PAGE_MAPPING_KSM	(PAGE_MAPPING_ANON | PAGE_MAPPING_MOVABLE)
#define PAGE_MAPPING_FLAGS	(PAGE_MAPPING_ANON | PAGE_MAPPING_MOVABLE)

static __always_inline int PageMappingFlags(struct page *page)
{
	return ((unsigned long)page->mapping & PAGE_MAPPING_FLAGS) != 0;
}

/**
 *  如果为匿名页面 返回 true
 *  否则返回 false
 */
static __always_inline int PageAnon(struct page *page)  /* 匿名页面: malloc/mmap */
{
	page = compound_head(page);
	return ((unsigned long)page->mapping & PAGE_MAPPING_ANON) != 0;
}

static __always_inline int __PageMovable(struct page *page)
{
	return ((unsigned long)page->mapping & PAGE_MAPPING_FLAGS) ==
				PAGE_MAPPING_MOVABLE;
}

#ifdef CONFIG_KSM
/*
 * A KSM page is one of those write-protected "shared pages" or "merged pages"
 * which KSM maps into multiple mms, wherever identical anonymous page content
 * is found in VM_MERGEABLE vmas.  It's a PageAnon page, pointing not to any
 * anon_vma, but to that page's node of the stable tree.
 */
static __always_inline int PageKsm(struct page *page)   /* 同页合并 */
{
	page = compound_head(page);
	return ((unsigned long)page->mapping & PAGE_MAPPING_FLAGS) == PAGE_MAPPING_KSM;
}
#else
//TESTPAGEFLAG_FALSE(Ksm)
#endif

u64 stable_page_flags(struct page *page);

static inline int PageUptodate(struct page *page)
{
	int ret;
	page = compound_head(page);
	ret = test_bit(PG_uptodate, &(page)->flags);
	/*
	 * Must ensure that the data we read out of the page is loaded
	 * _after_ we've loaded page->flags to check for PageUptodate.
	 * We can skip the barrier if the page is not uptodate, because
	 * we wouldn't be reading anything from it.
	 *
	 * See SetPageUptodate() for the other side of the story.
	 */
	if (ret)
		smp_rmb();

	return ret;
}

/**
 *  设置   PG_uptodate 位，标识内容有效
 */
static __always_inline void __SetPageUptodate(struct page *page)    /*  */
{
	VM_BUG_ON_PAGE(PageTail(page), page);
	smp_wmb();
	__set_bit(PG_uptodate, &page->flags);
}

static __always_inline void SetPageUptodate(struct page *page)  /* TODO */
{
	VM_BUG_ON_PAGE(PageTail(page), page);
	/*
	 * Memory barrier must be issued before setting the PG_uptodate bit,
	 * so that all previous stores issued in order to bring the page
	 * uptodate are actually visible before PageUptodate becomes true.
	 *
	 * 保证前面所有的设置标志位都已经写完成
	 */
	smp_wmb();
	set_bit(PG_uptodate, &page->flags);
}

CLEARPAGEFLAG(Uptodate, uptodate, PF_NO_TAIL) {/* {}++ */}

#if __rtoax_gcc_compile_with_E__

static __always_inline void ClearPageUptodate(struct page *page) { 
    clear_bit(PG_uptodate, &({ VM_BUG_ON_PGFLAGS(1 && PageTail(page), page); 
            ({ VM_BUG_ON_PGFLAGS(PagePoisoned(compound_head(page)), compound_head(page)); compound_head(page); }); })->flags); 
}
#endif //__rtoax_gcc_compile_with_E__

int test_clear_page_writeback(struct page *page);
int __test_set_page_writeback(struct page *page, bool keep_write);

#define test_set_page_writeback(page)			\
	__test_set_page_writeback(page, false)
#define test_set_page_writeback_keepwrite(page)	\
	__test_set_page_writeback(page, true)

static inline void set_page_writeback(struct page *page)
{
	test_set_page_writeback(page);
}

static inline void set_page_writeback_keepwrite(struct page *page)
{
	test_set_page_writeback_keepwrite(page);
}

__PAGEFLAG(Head, head, PF_ANY) CLEARPAGEFLAG(Head, head, PF_ANY){/* {}++ */}

#if __rtoax_gcc_compile_with_E__

static __always_inline int PageHead(struct page *page) { 
    return test_bit(PG_head, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); } 
static __always_inline void __SetPageHead(struct page *page) { 
    __set_bit(PG_head, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); } 
static __always_inline void __ClearPageHead(struct page *page) { 
    __clear_bit(PG_head, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); } 
static __always_inline void ClearPageHead(struct page *page) { 
    clear_bit(PG_head, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); }

#endif //__rtoax_gcc_compile_with_E__

static __always_inline void set_compound_head(struct page *page, struct page *head)
{
	WRITE_ONCE(page->compound_head, (unsigned long)head + 1);
}

static __always_inline void clear_compound_head(struct page *page)
{
	WRITE_ONCE(page->compound_head, 0);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline void ClearPageCompound(struct page *page)
{
	BUG_ON(!PageHead(page));
	ClearPageHead(page);
}
#endif

#define PG_head_mask ((1UL << PG_head))

#ifdef CONFIG_HUGETLB_PAGE
int PageHuge(struct page *page);
int PageHeadHuge(struct page *page);
bool page_huge_active(struct page *page);
#else
//TESTPAGEFLAG_FALSE(Huge)
//TESTPAGEFLAG_FALSE(HeadHuge)
//
//static inline bool page_huge_active(struct page *page)
//{
//	return 0;
//}
#endif
#if __rtoax_gcc_compile_with_E__


#endif //__rtoax_gcc_compile_with_E__


#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/*
 * PageHuge() only returns true for hugetlbfs pages, but not for
 * normal or transparent huge pages.
 *
 * PageTransHuge() returns true for both transparent huge and
 * hugetlbfs pages, but not normal pages. PageTransHuge() can only be
 * called only in the core VM paths where hugetlbfs pages can't exist.
 */
static inline int PageTransHuge(struct page *page)
{
	VM_BUG_ON_PAGE(PageTail(page), page);
	return PageHead(page);
}

/*
 * PageTransCompound returns true for both transparent huge pages
 * and hugetlbfs pages, so it should only be called when it's known
 * that hugetlbfs pages aren't involved.
 *
 * PageTransCompound 对透明大页面和 Hugetlbfs 页面都返回 true，
 * 因此只有在知道不涉及 Hugetlbfs 页面时才应该调用它。
 */
static inline int PageTransCompound(struct page *page)  /*  */
{
	return PageCompound(page);
}

/*
 * PageTransCompoundMap is the same as PageTransCompound, but it also
 * guarantees the primary MMU has the entire compound page mapped
 * through pmd_trans_huge, which in turn guarantees the secondary MMUs
 * can also map the entire compound page. This allows the secondary
 * MMUs to call get_user_pages() only once for each compound page and
 * to immediately map the entire compound page with a single secondary
 * MMU fault. If there will be a pmd split later, the secondary MMUs
 * will get an update through the MMU notifier invalidation through
 * split_huge_pmd().
 *
 * Unlike PageTransCompound, this is safe to be called only while
 * split_huge_pmd() cannot run from under us, like if protected by the
 * MMU notifier, otherwise it may result in page->_mapcount check false
 * positives.
 *
 * We have to treat page cache THP differently since every subpage of it
 * would get _mapcount inc'ed once it is PMD mapped.  But, it may be PTE
 * mapped in the current process so comparing subpage's _mapcount to
 * compound_mapcount to filter out PTE mapped case.
 */
static inline int PageTransCompoundMap(struct page *page)
{
	struct page *head;

	if (!PageTransCompound(page))
		return 0;

	if (PageAnon(page))
		return atomic_read(&page->_mapcount) < 0;

	head = compound_head(page);
	/* File THP is PMD mapped and not PTE mapped */
	return atomic_read(&page->_mapcount) ==
	       atomic_read(compound_mapcount_ptr(head));
}

/*
 * PageTransTail returns true for both transparent huge pages
 * and hugetlbfs pages, so it should only be called when it's known
 * that hugetlbfs pages aren't involved.
 */
static inline int PageTransTail(struct page *page)
{
	return PageTail(page);
}

/*
 * PageDoubleMap indicates that the compound page is mapped with PTEs as well
 * as PMDs.
 *
 * This is required for optimization of rmap operations for THP: we can postpone
 * per small page mapcount accounting (and its overhead from atomic operations)
 * until the first PMD split.
 *
 * For the page PageDoubleMap means ->_mapcount in all sub-pages is offset up
 * by one. This reference will go away with last compound_mapcount.
 *
 * See also __split_huge_pmd_locked() and page_remove_anon_compound_rmap().
 */
PAGEFLAG(DoubleMap, double_map, PF_SECOND)
	TESTSCFLAG(DoubleMap, double_map, PF_SECOND)
{}
#if __rtoax_gcc_compile_with_E__
    
static __always_inline int PageDoubleMap(struct page *page) {
    return test_bit(PG_double_map, 
        &({ VM_BUG_ON_PGFLAGS(!PageHead(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(&page[1]), &page[1]); &page[1]; }); })->flags); 
}
static __always_inline void SetPageDoubleMap(struct page *page) { 
    set_bit(PG_double_map, 
        &({ VM_BUG_ON_PGFLAGS(!PageHead(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(&page[1]), &page[1]); &page[1]; }); })->flags); 
} 
static __always_inline void ClearPageDoubleMap(struct page *page) { 
    clear_bit(PG_double_map, 
        &({ VM_BUG_ON_PGFLAGS(!PageHead(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(&page[1]), &page[1]); &page[1]; }); })->flags); 
}

static __always_inline int TestSetPageDoubleMap(struct page *page) {
    return test_and_set_bit(PG_double_map,
        &({ VM_BUG_ON_PGFLAGS(!PageHead(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(&page[1]), &page[1]); &page[1]; }); })->flags); 
} 
static __always_inline int TestClearPageDoubleMap(struct page *page) { 
    return test_and_clear_bit(PG_double_map, 
        &({ VM_BUG_ON_PGFLAGS(!PageHead(page), page); ({ VM_BUG_ON_PGFLAGS(PagePoisoned(&page[1]), &page[1]); &page[1]; }); })->flags); 
}
    
#endif //__rtoax_gcc_compile_with_E__

#else

//TESTPAGEFLAG_FALSE(TransHuge)
//TESTPAGEFLAG_FALSE(TransCompound)
//TESTPAGEFLAG_FALSE(TransCompoundMap)
//TESTPAGEFLAG_FALSE(TransTail)
//PAGEFLAG_FALSE(DoubleMap)
//	TESTSCFLAG_FALSE(DoubleMap)

#endif

/*
 * For pages that are never mapped to userspace (and aren't PageSlab),
 * page_type may be used.  Because it is initialised to -1, we invert the
 * sense of the bit, so __SetPageFoo *clears* the bit used for PageFoo, and
 * __ClearPageFoo *sets* the bit used for PageFoo.  We reserve a few high and
 * low bits so that an underflow or overflow of page_mapcount() won't be
 * mistaken for a page type value.
 */

#define PAGE_TYPE_BASE	0xf0000000
/* Reserve		0x0000007f to catch underflows of page_mapcount */
#define PAGE_MAPCOUNT_RESERVE	-128
#define PG_buddy	0x00000080
#define PG_offline	0x00000100
#define PG_kmemcg	0x00000200
#define PG_table	0x00000400
#define PG_guard	0x00000800

#define PageType(page, flag)						\
	((page->page_type & (PAGE_TYPE_BASE | flag)) == PAGE_TYPE_BASE)

static inline int page_has_type(struct page *page)
{
	return (int)page->page_type < PAGE_MAPCOUNT_RESERVE;
}

#define PAGE_TYPE_OPS(uname, lname)					\
static __always_inline int Page##uname(struct page *page)		\
{									\
	return PageType(page, PG_##lname);				\
}									\
static __always_inline void __SetPage##uname(struct page *page)		\
{									\
	VM_BUG_ON_PAGE(!PageType(page, 0), page);			\
	page->page_type &= ~PG_##lname;					\
}									\
static __always_inline void __ClearPage##uname(struct page *page)	\
{									\
	VM_BUG_ON_PAGE(!Page##uname(page), page);			\
	page->page_type |= PG_##lname;					\
}

/*
 * PageBuddy() indicates that the page is free and in the buddy system
 * (see mm/page_alloc.c).
 */
PAGE_TYPE_OPS(Buddy, buddy)

/*
 * PageOffline() indicates that the page is logically offline although the
 * containing section is online. (e.g. inflated in a balloon driver or
 * not onlined when onlining the section).
 * The content of these pages is effectively stale. Such pages should not
 * be touched (read/write/dump/save) except by their owner.
 *
 * If a driver wants to allow to offline unmovable PageOffline() pages without
 * putting them back to the buddy, it can do so via the memory notifier by
 * decrementing the reference count in MEM_GOING_OFFLINE and incrementing the
 * reference count in MEM_CANCEL_OFFLINE. When offlining, the PageOffline()
 * pages (now with a reference count of zero) are treated like free pages,
 * allowing the containing memory block to get offlined. A driver that
 * relies on this feature is aware that re-onlining the memory block will
 * require to re-set the pages PageOffline() and not giving them to the
 * buddy via online_page_callback_t.
 */
PAGE_TYPE_OPS(Offline, offline)

/*
 * If kmemcg is enabled, the buddy allocator will set PageKmemcg() on
 * pages allocated with __GFP_ACCOUNT. It gets cleared on page free.
 */
PAGE_TYPE_OPS(Kmemcg, kmemcg)

/*
 * Marks pages in use as page tables.
 */
PAGE_TYPE_OPS(Table, table)

/*
 * Marks guardpages used with debug_pagealloc.
 */
PAGE_TYPE_OPS(Guard, guard)
    {/* +++ */}
extern bool is_free_buddy_page(struct page *page);
{/* +++ */}
__PAGEFLAG(Isolated, isolated, PF_ANY);

{/* +++ */}

#if __rtoax_gcc_compile_with_E__


static __always_inline int PageBuddy(struct page *page) { 
    return ((page->page_type & (0xf0000000 | 0x00000080)) == 0xf0000000); } 
static __always_inline void __SetPageBuddy(struct page *page) { 
    VM_BUG_ON_PAGE(!((page->page_type & (0xf0000000 | 0)) == 0xf0000000), page); page->page_type &= ~0x00000080; } 
static __always_inline void __ClearPageBuddy(struct page *page) { 
    VM_BUG_ON_PAGE(!PageBuddy(page), page); page->page_type |= 0x00000080; }


static __always_inline int PageOffline(struct page *page) { 
    return ((page->page_type & (0xf0000000 | 0x00000100)) == 0xf0000000); } 
static __always_inline void __SetPageOffline(struct page *page) { 
    VM_BUG_ON_PAGE(!((page->page_type & (0xf0000000 | 0)) == 0xf0000000), page); page->page_type &= ~0x00000100; } 
static __always_inline void __ClearPageOffline(struct page *page) { 
    VM_BUG_ON_PAGE(!PageOffline(page), page); page->page_type |= 0x00000100; }



static __always_inline int PageKmemcg(struct page *page) { 
    return ((page->page_type & (0xf0000000 | 0x00000200)) == 0xf0000000); } 
static __always_inline void __SetPageKmemcg(struct page *page) { 
    VM_BUG_ON_PAGE(!((page->page_type & (0xf0000000 | 0)) == 0xf0000000), page); page->page_type &= ~0x00000200; } 
static __always_inline void __ClearPageKmemcg(struct page *page) { 
    VM_BUG_ON_PAGE(!PageKmemcg(page), page); page->page_type |= 0x00000200; }


static __always_inline int PageTable(struct page *page) { 
    return ((page->page_type & (0xf0000000 | 0x00000400)) == 0xf0000000); } 
static __always_inline void __SetPageTable(struct page *page) { 
    VM_BUG_ON_PAGE(!((page->page_type & (0xf0000000 | 0)) == 0xf0000000), page); page->page_type &= ~0x00000400; } 
static __always_inline void __ClearPageTable(struct page *page) { 
    VM_BUG_ON_PAGE(!PageTable(page), page); page->page_type |= 0x00000400; }


static __always_inline int PageGuard(struct page *page) { 
    return ((page->page_type & (0xf0000000 | 0x00000800)) == 0xf0000000); } 
static __always_inline void __SetPageGuard(struct page *page) { 
    VM_BUG_ON_PAGE(!((page->page_type & (0xf0000000 | 0)) == 0xf0000000), page); page->page_type &= ~0x00000800; } 
static __always_inline void __ClearPageGuard(struct page *page) { 
    VM_BUG_ON_PAGE(!PageGuard(page), page); page->page_type |= 0x00000800; }


static __always_inline int PageIsolated(struct page *page) { 
    return test_bit(PG_isolated, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); } 
static __always_inline void __SetPageIsolated(struct page *page) { 
    __set_bit(PG_isolated, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); } 
static __always_inline void __ClearPageIsolated(struct page *page) { 
    __clear_bit(PG_isolated, &({ VM_BUG_ON_PGFLAGS(PagePoisoned(page), page); page; })->flags); };

#endif //__rtoax_gcc_compile_with_E__


/*
 * If network-based swap is enabled, sl*b must keep track of whether pages
 * were allocated from pfmemalloc reserves.
 */
static inline int PageSlabPfmemalloc(struct page *page)
{
	VM_BUG_ON_PAGE(!PageSlab(page), page);
	return PageActive(page);
}

static inline void SetPageSlabPfmemalloc(struct page *page)
{
	VM_BUG_ON_PAGE(!PageSlab(page), page);
	SetPageActive(page);
}

static inline void __ClearPageSlabPfmemalloc(struct page *page)
{
	VM_BUG_ON_PAGE(!PageSlab(page), page);
	__ClearPageActive(page);
}

static inline void ClearPageSlabPfmemalloc(struct page *page)
{
	VM_BUG_ON_PAGE(!PageSlab(page), page);
	ClearPageActive(page);
}

#ifdef CONFIG_MMU
#define __PG_MLOCKED		(1UL << PG_mlocked)
#else
//#define __PG_MLOCKED		0
#endif

/*
 * Flags checked when a page is freed.  Pages being freed should not have
 * these flags set.  It they are, there is a problem.
 */
#define PAGE_FLAGS_CHECK_AT_FREE				\
	(1UL << PG_lru		| 1UL << PG_locked	|	\
	 1UL << PG_private	| 1UL << PG_private_2	|	\
	 1UL << PG_writeback	| 1UL << PG_reserved	|	\
	 1UL << PG_slab		| 1UL << PG_active 	|	\
	 1UL << PG_unevictable	| __PG_MLOCKED)

/*
 * Flags checked when a page is prepped for return by the page allocator.
 * Pages being prepped should not have these flags set.  It they are set,
 * there has been a kernel bug or struct page corruption.
 *
 * __PG_HWPOISON is exceptional because it needs to be kept beyond page's
 * alloc-free cycle to prevent from reusing the page.
 */
#define PAGE_FLAGS_CHECK_AT_PREP	\
	(((1UL << NR_PAGEFLAGS) - 1) & ~__PG_HWPOISON)

#define PAGE_FLAGS_PRIVATE				\
	(1UL << PG_private | 1UL << PG_private_2)
/**
 * page_has_private - Determine if page has private stuff
 * @page: The page to be checked
 *
 * Determine if a page has private stuff, indicating that release routines
 * should be invoked upon it.
 */
static inline int page_has_private(struct page *page)
{
	return !!(page->flags & PAGE_FLAGS_PRIVATE);
}

#undef PF_ANY
#undef PF_HEAD
#undef PF_ONLY_HEAD
#undef PF_NO_TAIL
#undef PF_NO_COMPOUND
#undef PF_SECOND
#endif /* !__GENERATING_BOUNDS_H */

#endif	/* PAGE_FLAGS_H */
