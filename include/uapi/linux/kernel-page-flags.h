/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPILINUX_KERNEL_PAGE_FLAGS_H
#define _UAPILINUX_KERNEL_PAGE_FLAGS_H

/*
 * Stable page flag bits exported to user space
 */

/**
 * @brief https://www.kernel.org/doc/html/latest/admin-guide/mm/pagemap.html
 *
 */

/**
 * @brief The page is being locked for exclusive access, e.g. by undergoing read/write IO.
 *
 */
#define KPF_LOCKED		0
/**
 * @brief IO error occurred.
 *
 */
#define KPF_ERROR		1
/**
 * @brief The page has been referenced since last LRU list enqueue/requeue.
 * 自从上次 LRU 列表加入队列/请求以来，页面就被引用了。
 */
#define KPF_REFERENCED		2
/**
 * @brief The page has up-to-date data. ie. for file backed page: (in-memory data revision >= on-disk one)
 *
 */
#define KPF_UPTODATE		3
/**
 * @brief The page has been written to, hence contains new data. i.e.
 * for file backed page: (in-memory data revision > on-disk one)
 *
 */
#define KPF_DIRTY		4
/**
 * @brief The page is in one of the LRU lists.
 *
 */
#define KPF_LRU			5
/**
 * @brief The page is in the active LRU list.
 *
 */
#define KPF_ACTIVE		6
/**
 * @brief The page is managed by the SLAB/SLOB/SLUB/SLQB kernel memory allocator.
 * When compound page is used, SLUB/SLQB will only set this flag on the head page;
 * SLOB will not flag it at all.
 *
 */
#define KPF_SLAB		7
/**
 * @brief The page is being synced to disk.
 *
 */
#define KPF_WRITEBACK		8
/**
 * @brief The page will be reclaimed soon after its pageout IO completed.
 *
 */
#define KPF_RECLAIM		9
/**
 * @brief A free memory block managed by the buddy system allocator. The buddy system
 * organizes free memory in blocks of various orders. An order N block has 2^N physically
 * contiguous pages, with the BUDDY flag set for and _only_ for the first page.
 *
 */
#define KPF_BUDDY		10

/* 11-20: new additions in 2.6.31 */

/**
 * @brief A memory mapped page.
 *
 */
#define KPF_MMAP		11
/**
 * @brief A memory mapped page that is not part of a file.
 *
 */
#define KPF_ANON		12
/**
 * @brief The page is mapped to swap space, i.e. has an associated swap entry.
 *
 */
#define KPF_SWAPCACHE		13
/**
 * @brief The page is backed by swap/RAM.
 *
 */
#define KPF_SWAPBACKED		14
/**
 * @brief A compound page with order N consists of 2^N physically contiguous pages.
 * A compound page with order 2 takes the form of “HTTT”, where H donates its head
 * page and T donates its tail page(s). The major consumers of compound pages are
 * hugeTLB pages (Documentation/admin-guide/mm/hugetlbpage.rst), the SLUB etc. memory
 * allocators and various device drivers. However in this interface, only huge/giga
 * pages are made visible to end users.
 *
 */
#define KPF_COMPOUND_HEAD	15
/**
 * @brief A compound page tail (see description above).
 *
 */
#define KPF_COMPOUND_TAIL	16
/**
 * @brief This is an integral part of a HugeTLB page.
 *
 */
#define KPF_HUGE		17
/**
 * @brief The page is in the unevictable (non-)LRU list It is somehow pinned and not
 * a candidate for LRU page reclaims, e.g. ramfs pages, shmctl(SHM_LOCK) and mlock() memory segments.
 *
 */
#define KPF_UNEVICTABLE		18
/**
 * @brief Hardware detected memory corruption on this page: don’t touch the data!
 * 硬件检测到此页面内存损坏: 请勿访问数据！
 */
#define KPF_HWPOISON		19
/**
 * @brief No page frame exists at the requested address.
 *
 */
#define KPF_NOPAGE		20

/**
 * @brief Identical memory pages dynamically shared between one or more processes.
 *
 */
#define KPF_KSM			21
/**
 * @brief Contiguous pages which construct transparent hugepages.
 *
 */
#define KPF_THP			22
/**
 * @brief The page is logically offline.
 *
 */
#define KPF_OFFLINE		23
/**
 * @brief Zero page for pfn_zero or huge_zero page.
 *
 */
#define KPF_ZERO_PAGE		24
/**
 * @brief The page has not been accessed since it was marked idle
 * (see Documentation/admin-guide/mm/idle_page_tracking.rst).
 *
 * Note that this flag may be stale in case the page was accessed via a PTE.
 * To make sure the flag is up-to-date one has to read /sys/kernel/mm/page_idle/bitmap first.
 *
 * 该页面被标记为 idle 后就没有被访问过(请参阅 Documentation/admin-guide/mm/idle_page_tracking.rst)。
 * 请注意，如果通过 PTE 访问页面，该标志可能会过时。
 * 要确保标志是最新的，必须首先读取 /sys/kernel/mm/page_idle/bitmap。
 */
#define KPF_IDLE		25
/**
 * @brief The page is in use as a page table.
 *
 */
#define KPF_PGTABLE		26

#endif /* _UAPILINUX_KERNEL_PAGE_FLAGS_H */
