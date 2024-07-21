/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/mm_types_task.h>

#include <linux/auxvec.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/uprobes.h>
#include <linux/page-flags-layout.h>
#include <linux/workqueue.h>
#include <linux/seqlock.h>

#include <asm/mmu.h>

#ifndef AT_VECTOR_SIZE_ARCH
#define AT_VECTOR_SIZE_ARCH 0
#endif
#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))


struct address_space;
struct mem_cgroup;

/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page, though if it is a pagecache page, rmap structures can tell us
 * who is mapping it.
 *
 * If you allocate the page using alloc_pages(), you can use some of the
 * space in struct page for your own purposes.  The five words in the main
 * union are available, except for bit 0 of the first word which must be
 * kept clear.  Many users use this word to store a pointer to an object
 * which is guaranteed to be aligned.  If you use the same storage as
 * page->mapping, you must restore it to NULL before freeing the page.
 *
 * If your page will not be mapped to userspace, you can also use the four
 * bytes in the mapcount union, but you must call page_mapcount_reset()
 * before freeing it.
 *
 * If you want to use the refcount field, it must be used in such a way
 * that other CPUs temporarily incrementing and then decrementing the
 * refcount does not cause problems.  On receiving the page from
 * alloc_pages(), the refcount will be positive.
 *
 * If you allocate pages of order > 0, you can use some of the fields
 * in each subpage, but you may need to restore some of their values
 * afterwards.
 *
 * SLUB uses cmpxchg_double() to atomically update its freelist and
 * counters.  That requires that freelist & counters be adjacent and
 * double-word aligned.  We align all struct pages to double-word
 * boundaries, and ensure that 'freelist' is aligned within the
 * struct.
 */
#ifdef CONFIG_HAVE_ALIGNED_STRUCT_PAGE
#define _struct_page_alignment	__aligned(2 * sizeof(unsigned long))
#else
//#define _struct_page_alignment
#endif

/**
 * @brief 物理页
 *
 */
struct page {
    /**
     *  +----------+---------+----------+--------+----------+
     *  |  section |   node  |   zone   |  ...   |   flag   |
     *  +----------+---------+----------+--------+----------+
     *
     *  struct page->flags
     *
     * 63    62 61  60 59             44 43                                               0
     *  +------+------+-----------------+-------------------------------------------------+
     *  | node | zone |    LAST_CPUPID  |                   flags                         |
     *  +------+------+-----------------+-------------------------------------------------+
     *
     *  整体布局如下，但是可能和上面的不太一样，不同的配置，所占位数有差异
     * Page flags: | [SECTION] | [NODE] | ZONE | [LAST_CPUPID] | ... | FLAGS |
	 *
	 * 见 `set_page_links()`
	 * flags -> enum pageflags
     */
	unsigned long flags;		/* Atomic flags, some possibly
					 * updated asynchronously */
	/*
	 * Five words (20/40 bytes) are available in this union.
	 * WARNING: bit 0 of the first word is used for PageTail(). That
	 * means the other users of this union MUST NOT use the bit to
	 * avoid collision and false-positive PageTail().
	 */
	union {
		/**
		 * @brief Page cache and anonymous pages
		 *
		 * 页缓存 和 匿名页(管理匿名页面/文件映射页面)
		 */
		struct {
			/**
			 * @lru: Pageout list, eg. active_list protected by
			 * pgdat->lru_lock.  Sometimes used as a generic list
			 * by the page owner.
			 *
			 * 链表头(不一定是链表头，可能是链表节点)，主要有3个用途：
             * a: page处于伙伴系统中时，用于链接相同阶的伙伴（只使用伙伴中的第一个page的lru即可达到目的）。
             * b: page属于slab时，page->lru.next指向page驻留的的缓存的管理结构，page->lru.prec指向保存该page的slab的管理结构。
             * c: page被用户态使用或被当做页缓存使用时，用于将该page连入zone中相应的lru链表，供内存回收时使用。
             * d: page回收时,见`reclaim_pages()`
             *
             * a. `zone->free_area->free_list` 为链表头的链表,见`get_page_from_free_area()`
			 */
			struct list_head lru;   /* 串入 zone->freelist *//* struct lruvec->lists[lru] */

			/* See page-flags.h for PAGE_MAPPING_FLAGS */
            /**
             *  页面指向的地址空间,一个指针，两个用途
             *  -----------------------------------------------
             *  1. 文件映射页面，`struct address_space`
             *  2. 匿名映射页面，`struct anon_vma`. 见`PageAnon()`,`PAGE_MAPPING_ANON`
             *  3. 交换高速缓存页面，`swapper_spaces`
             *  4. KSM页面对应 `struct stable_node`结构
             *
             * 因为 `struct address_space` 为 8bytes 对齐，所以可将 mapping 成员的低两位用作：
             *
             * bit[0] 页面是否为 匿名页面，见`PageAnon()`,`PAGE_MAPPING_ANON`
             * bit[1] 页面是否为 非 LRU 页面
             * 若bit[0-1]均未置位,表明这是一个 KSM 页面
             *
             * page_rmapping(): 清除 低2位
             * page_mapping():  返回 page->mapping 成员指向的地址空间
             * page_mapped():   是否映射到用户 PTE
             *
             * ================================================
             * 如果 mapping = 0，说明该page属于交换缓存（swap cache）；`page_mapping`返回NULL的情况
             *                  当需要使用地址空间时会指定交换分区的地址空间swapper_space。
             * 如果 mapping != 0，
             *      bit[0] = 0，说明该page属于页缓存或文件映射，mapping指向文件的地址空间address_space。
             *      bit[0] != 0，说明该page为匿名映射，mapping指向struct anon_vma对象。
             *
             * 通过mapping恢复anon_vma的方法：
			 *  anon_vma = (struct anon_vma *)(mapping - PAGE_MAPPING_ANON)。
             */
			struct address_space *mapping;

            /**
             *  在映射的虚拟空间（vma_area）内的偏移；
             *
             *  一个文件可能只映射一部分，假设映射了1M的空间，
             *  index指的是在1M空间内的偏移，而不是在整个文件内的偏移。
             *
             *  如果多个VMA的虚拟页面同时映射了同一个匿名页面，那么 index 值为多少?
             *
             *  1. 对于父子进程同时映射了一个 匿名页面， index 相同，见
             *      rmap_walk_anon()
             *        vma_address()
             *          __vma_address()
             *            page_to_pgoff()
             *
             *  2. 对于KSM页面，由内容相同的两个匿名页面合并而成，他们可以使不相干的进程的VMA
             *      也可以是 父子进程的 VMA
             *      对于 KSM页面来说， index 等于 第一次 映射该页面 的 VMA 中的偏移量
             */
			pgoff_t index;		/* Our offset within mapping. */

			/**
			 * @private: Mapping-private opaque data.
			 * Usually used for buffer_heads if PagePrivate.
			 * Used for swp_entry_t if PageSwapCache.
			 * Indicates order in the buddy system if PageBuddy.
			 */
			unsigned long private;
		};

		/**
		 * @brief  page_pool used by netstack 页池
		 *
		 */
		struct {
			/**
			 * @dma_addr: might require a 64-bit value even on
			 * 32-bit architectures.
			 */
			dma_addr_t dma_addr;
		};
		/**
		 * @brief slab, slob and slub 被slab使用
		 *
		 */
		struct {
			union {
                /**
                 *  slab_list 是 full/partial/empty 链表头的节点
                 */
				struct list_head slab_list;
				struct {	/* Partial pages(slub-16 bytes) */
					struct page *next;
#ifdef CONFIG_64BIT
					int pages;	/* Nr of pages left */
					int pobjects;	/* Approximate count */
#else
					short int pages;
					short int pobjects;
#endif
				};
			};

            struct kmem_cache *slab_cache; /* not slob */
			/* Double-word boundary */
            /* 管理区 */
			void *freelist;		/* first free object */

			union {
				void *s_mem;	/* slab: first object 第一个 slab 对应OBJ 的起始地址 */
				unsigned long counters;		/* SLUB */
				struct {			/* SLUB */
					unsigned inuse:16;
					unsigned objects:15;
					unsigned frozen:1;
				};
			};
		};

        /* TODO */
		struct {	/* Tail pages of compound(复合) page */
			unsigned long compound_head;	/* Bit zero is set */

			/* First tail page only */
			unsigned char compound_dtor;
			unsigned char compound_order;
			atomic_t compound_mapcount;
			unsigned int compound_nr; /* 1 << compound_order */
		};

		struct {	/* Second tail page of compound page */
			unsigned long _compound_pad_1;	/* compound_head */
			atomic_t hpage_pinned_refcount;
			/* For both global and memcg */
			struct list_head deferred_list;
		};

		struct {	/* Page table pages 页表使用的Page(管理页表) */
			unsigned long _pt_pad_1;	/* compound_head */
			pgtable_t pmd_huge_pte; /* protected by page->ptl */
			unsigned long _pt_pad_2;	/* mapping */
			union {
				struct mm_struct *pt_mm; /* x86 pgds only */
				atomic_t pt_frag_refcount; /* powerpc */
			};

            /**
             *  页表自旋锁
             */
#if ALLOC_SPLIT_PTLOCKS
			spinlock_t *ptl;
#else
			spinlock_t ptl;
#endif
		};

		struct {	/* ZONE_DEVICE pages ZONE设备Page */
			/** @pgmap: Points to the hosting device page map. */
			struct dev_pagemap *pgmap;
			void *zone_device_data;
			/*
			 * ZONE_DEVICE private pages are counted as being
			 * mapped so the next 3 words hold the mapping, index,
			 * and private fields from the source anonymous or
			 * page cache page while the page is migrated to device
			 * private memory.
			 * ZONE_DEVICE MEMORY_DEVICE_FS_DAX pages also
			 * use the mapping, index, and private fields when
			 * pmem backed DAX files are mapped.
			 */
		};

		/** @rcu_head: You can use this to free a page by RCU. */
		struct rcu_head rcu_head;
	};

	union {		/* This union is 4 bytes in size. */
		/*
		 * If the page can be mapped to userspace, encodes the number
		 * of times this page is referenced by a page table.
		 *
		 * 被页表映射的次数(映射了多少个PTE)，也就是说该page同时被多少个进程共享。
		 * _mapcount 主要用于 RMAP 中
		 *
		 * =-1 标识没有PTE 映射到页面
		 * =0  标识只有父进程映射到页面
		 * >0  标识除了父进程外还有其他进程映射到这个页面
		 *
		 * 见`page_dup_rmap()`
		 *
		 * 下面的解释是比较古老的解释：
		 * ==========================================
		 * 初始值为-1，如果只被一个进程的页表映射了，该值为0 。
		 * 如果该page处于伙伴系统中，该值为`PAGE_BUDDY_MAPCOUNT_VALUE`（-128），
		 * 内核通过判断该值是否为`PAGE_BUDDY_MAPCOUNT_VALUE`来确定该page是否属于伙伴系统。
         * 注意区分_count和_mapcount，_mapcount表示的是映射次数，而_count表示的是使用次数；
         * 被映射了不一定在使用，但要使用必须先映射。
         *
         *  通常情况下，page_count(page) == page_mapcount(page)
         *          即   page->_refcount = page->_mapcount + 1
		 */
		atomic_t _mapcount;

		/*
		 * If the page is neither PageSlab nor mappable to userspace,
		 * the value stored here may help determine what this page
		 * is used for.  See page-flags.h for a list of page types
		 * which are currently stored here.
		 */
		unsigned int page_type;

        /**
         *  slab 分配器中活跃的对象的数量
         *
         *  =0: 标识没有活跃对象，可以销毁这个slab 分配器
         */
		unsigned int active;		/* SLAB */
		int units;			/* SLOB */
	};

	/**
	 *  Usage count. *DO NOT USE DIRECTLY*. See page_ref.h
	 *
	 *  内核中引用该页面的次数
	 *  =0 该页面 为空闲页面或即将要被释放的页面
	 *  >0 该页面 已经被分配，且内核正在使用，暂时不会被释放
	 *
	 *  操作 _refcount 的函数 `get_page()`,`put_page()`
     *
     *  通常情况下，page_count(page) == page_mapcount(page)
     *          即   page->_refcount = page->_mapcount + 1
     *
     *  _refcount 有以下四种来源：
     *  =============================================
     *  1. 页面高速缓存在 radix tree 上， KSM 不考虑 页面高速缓存的情况
     *  2. 被用户态 PTE 引用， _refcount 和 _mapcount 都会增加计数
     *  3. page->private 数据也会增加 _refcount，对于匿名页面，需要判断他是否在交换缓存中
     *  4. 内核操作某些页面时会增加 _refcount, 如 follow_page(),get_user_pages_fast()
	 */
	atomic_t _refcount;

#ifdef CONFIG_MEMCG
	union {
		struct mem_cgroup *mem_cgroup;
		struct obj_cgroup **obj_cgroups;
	};
#endif

	/*
	 * On machines where all RAM is mapped into kernel address space,
	 * we can simply calculate the virtual address. On machines with
	 * highmem some memory is mapped into kernel virtual memory
	 * dynamically, so we need a place to store that address.
	 * Note that this field could be 16 bits on x86 ... ;)
	 *
	 * Architectures with slow multiplication can define
	 * WANT_PAGE_VIRTUAL in asm/page.h
	 */
#if defined(WANT_PAGE_VIRTUAL)
	/* page 的虚拟地址 */
	void *virtual;
    /* Kernel virtual address (NULL if not kmapped, ie. highmem) */
#endif /* WANT_PAGE_VIRTUAL */

#ifdef LAST_CPUPID_NOT_IN_PAGE_FLAGS
	int _last_cpupid;
#endif
} _struct_page_alignment;

static inline atomic_t *compound_mapcount_ptr(struct page *page)
{
	return &page[1].compound_mapcount;
}

static inline atomic_t *compound_pincount_ptr(struct page *page)
{
	return &page[2].hpage_pinned_refcount;
}

/*
 * Used for sizing the vmemmap region on some architectures
 */
#define STRUCT_PAGE_MAX_SHIFT	(order_base_2(sizeof(struct page)))

#define PAGE_FRAG_CACHE_MAX_SIZE	__ALIGN_MASK(32768, ~PAGE_MASK)
#define PAGE_FRAG_CACHE_MAX_ORDER	get_order(PAGE_FRAG_CACHE_MAX_SIZE)

#define page_private(page)		((page)->private)

/**
 *
 */
static inline void set_page_private(struct page *page, unsigned long private)
{
	page->private = private;
}

struct page_frag_cache {
	void * va;
#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
	__u16 offset;
	__u16 size;
#else
	__u32 offset;
#endif
	/* we maintain a pagecount bias, so that we dont dirty cache line
	 * containing page->_refcount every time we allocate a fragment.
	 */
	unsigned int		pagecnt_bias;
	bool pfmemalloc;
};

typedef unsigned long vm_flags_t;

/*
 * A region containing a mapping of a non-memory backed file under "NOMMU"(NO MMU 情况)
 * conditions.  These are held in a global tree and are pinned by the VMAs that
 * map parts of them.
 */
struct vm_region {
	struct rb_node	vm_rb;		/* link in global region tree */
	vm_flags_t	vm_flags;	/* VMA vm_flags */
	unsigned long	vm_start;	/* start address of region */
	unsigned long	vm_end;		/* region initialised to here */
	unsigned long	vm_top;		/* region allocated to here */
	unsigned long	vm_pgoff;	/* the offset in vm_file corresponding to vm_start */
	struct file	*vm_file;	/* the backing file or NULL */

	int		vm_usage;	/* region usage count (access under nommu_region_sem) */
	bool		vm_icache_flushed : 1; /* true if the icache has been flushed for
						* this region */
};

#ifdef CONFIG_USERFAULTFD
#define NULL_VM_UFFD_CTX ((struct vm_userfaultfd_ctx) { NULL, })
struct vm_userfaultfd_ctx {
	struct userfaultfd_ctx *ctx;
};
#else /* CONFIG_USERFAULTFD */

#endif /* CONFIG_USERFAULTFD */

/*
 * This struct describes a virtual memory area. There is one of these
 * per VM-area/task. A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
struct vm_area_struct { /* VMA */
	/* The first cache line has the info for VMA tree walking. */

	/* 这两个变量是页对齐的 */
	unsigned long vm_start;		/* Our start address within vm_mm. */
	unsigned long vm_end;		/* The first byte after our end address
					   within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next, *vm_prev;   /* 双向链表 */

	/* mm_struct.mm_rb */
	struct rb_node vm_rb;   /* 在 mm_struct 为根的节点 */

	/*
	 * Largest free memory gap(差距) in bytes to the left of this VMA.
	 * Either between this VMA and vma->vm_prev, or between one of the
	 * VMAs below us in the VMA rbtree and its ->vm_prev. This helps
	 * get_unmapped_area find a free area of the right size.
	 *
	 * 此VMA左侧的最大可用内存间隙（差异），以字节为单位。 在此VMA和
	 * vma-> vm_prev之间，或在VMA rbtree中我们下面的VMA之一与其-> vm_prev之间。
	 * 这有助于`get_unmapped_area`找到合适大小的空闲区域。
	 */
	unsigned long rb_subtree_gap;

	/* Second cache line starts here. */

	struct mm_struct *vm_mm;	/* The address space we belong to. */

	/*
	 * Access permissions of this VMA.
	 * See vmf_insert_mixed_prot() for discussion.
	 */
	pgprot_t vm_page_prot;      /* 权限 */
	unsigned long vm_flags;		/* Flags, see mm.h. */

	/*
	 * For areas with an address space and backing store,
	 * linkage into the address_space->i_mmap interval tree.
	 */
	struct {
		struct rb_node rb;
		unsigned long rb_subtree_last;
	} shared;

	/*
	 * A file's MAP_PRIVATE vma can be in both i_mmap tree and anon_vma
	 * list, after a COW of one of the file pages.	A MAP_SHARED vma
	 * can only be in the i_mmap tree.  An anonymous MAP_PRIVATE, stack
	 * or brk vma (with NULL file) can only be in an anon_vma list.
	 *
	 * 它是一个链表头，节点为:
	 *  anon_vma_chain->same_vma
	 */
	struct list_head anon_vma_chain; /* Serialized by mmap_lock & page_table_lock */

	/**
	 *  用于 RMAP ，指向 AV(struct anon_vma)结构
	 *
	 *
	 */
	struct anon_vma *anon_vma;	/* Serialized by page_table_lock */

	/**
	 * VMA 操作
	 *
	 *  Function pointers to deal with this struct.
	 *  匿名页面该项 为 NULL
	 *
	 * 简单统计一下
	 * fs/zonefs/super.c:   zonefs_file_vm_ops
	 * fs/ubifs/file.c:   ubifs_file_vm_ops
	 * fs/vboxsf/file.c:   vboxsf_file_vm_ops
	 * fs/9p/vfs_file.c:   v9fs_file_vm_ops
	 * fs/9p/vfs_file.c:   v9fs_mmap_file_vm_ops
	 * fs/9p/vfs_file.c:   v9fs_file_vm_ops
	 * fs/9p/vfs_file.c:   v9fs_mmap_file_vm_ops
	 * fs/ext4/file.c:   ext4_dax_vm_ops
	 * fs/ext4/file.c:   ext4_file_vm_ops
	 * fs/nilfs2/file.c:   nilfs_file_vm_ops
	 * fs/cifs/file.c:   cifs_file_vm_ops
	 * fs/gfs2/file.c:   gfs2_vm_ops
	 * fs/ceph/addr.c:   ceph_vmops
	 * fs/nfs/file.c:   nfs_file_vm_ops
	 * fs/nfs/file.c:   nfs_file_vm_ops
	 * fs/fuse/dax.c:   fuse_dax_vm_ops
	 * fs/fuse/file.c:   fuse_file_vm_ops
	 * fs/orangefs/file.c:   orangefs_file_vm_ops
	 * fs/afs/file.c:   afs_vm_ops
	 * fs/f2fs/file.c:   f2fs_file_vm_ops
	 * fs/aio.c:   aio_ring_vm_ops
	 * fs/btrfs/file.c:   btrfs_file_vm_ops
	 * fs/xfs/xfs_file.c:   xfs_file_vm_ops
	 * fs/proc/vmcore.c:   vmcore_mmap_ops
	 * fs/ocfs2/mmap.c:   ocfs2_file_vm_ops
	 * fs/kernfs/file.c:   kernfs_vm_ops
	 * fs/coda/file.c:   vm_ops
	 * fs/ext2/file.c:   ext2_dax_vm_ops
	 * mm/filemap.c:  generic_file_vm_ops
	 * mm/mmap.c:   special_mapping_vmops
	 * mm/mmap.c:   legacy_special_mapping_vmops
	 * mm/shmem.c:   shmem_vm_ops
	 * mm/hugetlb.c:  hugetlb_vm_ops
	 * arch/x86/kernel/cpu/resctrl/pseudo_lock.c:   pseudo_mmap_ops
	 * arch/x86/entry/vsyscall/vsyscall_64.c:   gate_vma_ops
	 * kernel/bpf/syscall.c:   bpf_map_default_vmops
	 * kernel/events/core.c:   perf_mmap_vmops
	 * kernel/relay.c:   relay_file_mmap_ops
	 * ipc/shm.c:   shm_vm_ops
	 * net/packet/af_packet.c:   packet_mmap_ops
	 * net/ipv4/tcp.c:   tcp_vm_ops
	 * drivers/char/agp/alpha-agp.c:   alpha_core_agp_vm_ops
	 * drivers/char/mspec.c:   mspec_vm_ops
	 * drivers/char/mem.c:   mmap_mem_ops
	 * drivers/vfio/pci/vfio_pci_nvlink2.c:   vfio_pci_nvgpu_mmap_vmops
	 * drivers/vfio/pci/vfio_pci.c:   vfio_pci_mmap_ops
	 * include/media/videobuf2-memops.h:   vb2_common_vm_ops
	 * include/linux/hugetlb.h:   hugetlb_vm_ops
	 * include/linux/ramfs.h:   generic_file_vm_ops
	 * include/drm/drm_gem_cma_helper.h:   drm_gem_cma_vm_ops
	 * security/selinux/selinuxfs.c:   sel_mmap_policy_ops
	 *
	 */
	const struct vm_operations_struct *vm_ops;

	/* Information about our backing store: */
	/**
	 *
	 */
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE units */

	/**
	 *
	 */
	struct file * vm_file;		/* File we map to (can be NULL). */

	void * vm_private_data;		/* was vm_pte (shared mem) */

#ifdef CONFIG_SWAP
	atomic_long_t swap_readahead_info;
#endif
#ifndef CONFIG_MMU
	struct vm_region *vm_region;	/* NOMMU mapping region */
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *vm_policy;	/* NUMA policy for the VMA */
#endif
	/**
	 * @brief userfaultfd(2) 上下文
	 *
	 */
	struct vm_userfaultfd_ctx vm_userfaultfd_ctx;
} __randomize_layout;

struct core_thread {    /* coredump 线程链表 */
	struct task_struct *task;
	struct core_thread *next;
};

struct core_state { /* coredump 支持 */
	atomic_t nr_threads;
	struct core_thread dumper;
	struct completion startup;
};

struct kioctx_table;

/**
 *  `mm_struct` 包含许多不同的与进程地址空间有关的字段，像内核代码/数据段的起始和结束地址，
 *              `brk` 的起始和结束，内存区域的数量，内存区域列表等
 */
struct mm_struct {  /* 进程虚拟地址空间 */
	struct {
		/* VMA 链表头 */
		struct vm_area_struct *mmap;		/* list of VMAs, 将所有的 VMA 串联成串 */

		/* 节点为 vm_area_struct.vm_rb */
		struct rb_root mm_rb;               /* `mm_rb` 是虚拟内存区域的红黑树结构 */

		u64 vmacache_seqnum;                   /* per-thread vmacache */
#ifdef CONFIG_MMU
		/**
		 *  arch_get_unmapped_area(...), 对应 mmap
		 *
		 * arch_pick_mmap_layout():
		 *  arch_get_unmapped_area()
		 *  arch_get_unmapped_area_topdown()
		 */
		unsigned long (*get_unmapped_area) (struct file *filp,
				unsigned long addr, unsigned long len,
				unsigned long pgoff, unsigned long flags);
#endif
		/**
		 *  与经典布局不同的是：使用固定值限制栈的最大长度。由于栈是有界的，
		 *  因此安置内存映射的区域可以在栈末端的下方立即开始。这时mmap区是自顶向下扩展的。
		 *  由于堆仍然位于虚拟地址空间中较低的区域并向上增长，因此mmap区域和堆可以相对扩展，
		 *  直至耗尽虚拟地址空间中剩余的区域。
		 *
		 *  https://rtoax.blog.csdn.net/article/details/118602363
		 */
		unsigned long mmap_base;	/* base of mmap area 虚拟地址空间中用于内存映射的起始地址 */
		unsigned long mmap_legacy_base;	/* base of mmap area in bottom-up allocations */

#ifdef CONFIG_HAVE_ARCH_COMPAT_MMAP_BASES
		/* Base adresses for compatible mmap() */
		unsigned long mmap_compat_base;
		unsigned long mmap_compat_legacy_base;
#endif
		unsigned long task_size;	/* size of task vm space */

		/**
		 *  虚拟地址  的上限
		 */
		unsigned long highest_vm_end;	/* highest vma end address */

		/**
		 *  x86 - CR3
		 *  Arm64 - TTBR0
		 */
		pgd_t * pgd;    /* `pgd` 是全局页目录的指针 */

#ifdef CONFIG_MEMBARRIER
		/**
		 * @membarrier_state: Flags controlling membarrier behavior.
		 *
		 * This field is close to @pgd to hopefully fit in the same
		 * cache-line, which needs to be touched by switch_mm().
		 */
		atomic_t membarrier_state;
#endif

		/**
		 * @mm_users: The number of users including userspace.
		 *
		 * Use mmget()/mmget_not_zero()/mmput() to modify. When this
		 * drops to 0 (i.e. when the task exits and there are no other
		 * temporary reference holders), we also release a reference on
		 * @mm_count (which may then free the &struct mm_struct if
		 * @mm_count also drops to 0).
		 */
		atomic_t mm_users;/* 用户空间使用的用户数 ,`mm_user` 是使用该内存空间的进程数目 */

		/**
		 * @mm_count: The number of references to &struct mm_struct
		 * (@mm_users count as 1).
		 *
		 * Use mmgrab()/mmdrop() to modify. When this drops to 0, the
		 * &struct mm_struct is freed.
		 */
		atomic_t mm_count;/* 引用这个内存 结构的 次数,`mm_count` 是主引用计数 */

		/**
		 * @has_pinned: Whether this mm has pinned any pages.  This can
		 * be either replaced in the future by @pinned_vm when it
		 * becomes stable, or grow into a counter on its own. We're
		 * aggresive on this bit now - even if the pinned pages were
		 * unpinned later on, we'll still keep this bit set for the
		 * lifecycle of this mm just for simplicity.
		 */
		atomic_t has_pinned;

		/**
		 * @write_protect_seq: Locked when any thread is write
		 * protecting pages mapped by this mm to enforce a later COW,
		 * for instance during page table copying for fork().
		 */
		seqcount_t write_protect_seq;/* 当有线程通过这个 mm 写被保护的 pages，强制后期 COW */

#ifdef CONFIG_MMU
		atomic_long_t pgtables_bytes;	/* PTE page table pages */
#endif
		int map_count;			/* number of VMAs */

		/**
		 *  用于保护进程页表
		 */
		spinlock_t page_table_lock; /* Protects page tables and some
					     * counters 保护页表和一些计数器
					     */
		/**
		 *  在 linux-5.0 中为 struct rw_semaphore mmap_sem;
		 *  用户保护进程地址空间
		 *
		 *  brk, mmap, mprotect, mremap, msync  都会使用 down_write(&mm->mmap_sem)
		 */
		struct rw_semaphore mmap_lock;  /* 读写锁,内存区域信号量 */

		struct list_head mmlist; /* List of maybe swapped mm's.	These 可能被 swap 的 mm
					  * are globally strung together off
					  * init_mm.mmlist, and are protected
					  * by mmlist_lock
					  */


		unsigned long hiwater_rss; /* High-watermark of RSS usage */
		unsigned long hiwater_vm;  /* High-water virtual memory usage */

		unsigned long total_vm;	   /* Total pages mapped 一共映射的pages*/
		unsigned long locked_vm;   /* Pages that have PG_mlocked set */
		atomic64_t    pinned_vm;   /* Refcount permanently increased */
		unsigned long data_vm;	   /* VM_WRITE & ~VM_SHARED & ~VM_STACK */
		unsigned long exec_vm;	   /* VM_EXEC & ~VM_WRITE & ~VM_STACK */
		unsigned long stack_vm;	   /* VM_STACK */
		unsigned long def_flags;

		spinlock_t arg_lock; /* protect the below fields 保护下面的参数*/

		/**
		 * 这些变量在 load_elf_binary() 中被赋值
		 * 部分是从 CRIU(CONFIG_CHECKPOINT_RESTORE) 开始引入的
		 *  https://criu.org/Upstream_kernel_commits
		 *    https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=028ee4be34
		 */
		unsigned long start_code, end_code, start_data, end_data;   /* brk() */
		unsigned long start_brk, brk, start_stack;
		unsigned long arg_start, arg_end, env_start, env_end;

		/**
		 *  辅助向量
		 *  /proc/PID/auxv
		 */
		unsigned long saved_auxv[AT_VECTOR_SIZE]; /* for /proc/PID/auxv */

		/*
		 * Special counters, in some configurations protected by the
		 * page_table_lock, in other configurations by being atomic.
		 *
		 * 用于记录进程的内存使用情况
		 */
		struct mm_rss_stat rss_stat;

		/**
		 *  在 set_binfmt() 中赋值
		 */
		struct linux_binfmt *binfmt;    /* ELF 对应`elf_format` */

		/* Architecture-specific MM context */
		mm_context_t context;   /* MMU 上下文 */

		unsigned long flags; /* Must use atomic bitops to access */

		struct core_state *core_state; /* coredumping support */

#ifdef CONFIG_AIO
		spinlock_t			ioctx_lock;
		struct kioctx_table __rcu	*ioctx_table;
#endif
#ifdef CONFIG_MEMCG
		/*
		 * "owner" points to a task that is regarded as the canonical
		 * user/owner of this mm. All of the following must be true in
		 * order for it to be changed:
		 *
		 * current == mm->owner
		 * current->mm != mm
		 * new_owner->mm == mm
		 * new_owner->alloc_lock is held
		 */
		struct task_struct __rcu *owner;
#endif
		struct user_namespace *user_ns;/* name space 资源的隔离，名字的隔离， cgroup 做资源的限制*/

		/**
		 * store ref to file /proc/<pid>/exe symlink points to
		 * 符号链接
		 *
		 * 在 加载 ELF 文件时，会通过
		 *  load_elf_binary()
		 *   ->begin_new_exec()
		 *    ->set_mm_exe_file()
		 */
		struct file __rcu *exe_file;

#ifdef CONFIG_MMU_NOTIFIER
		/**
		 *  MMU 通知
		 */
		struct mmu_notifier_subscriptions *notifier_subscriptions;
#endif

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && !USE_SPLIT_PMD_PTLOCKS
		pgtable_t pmd_huge_pte; /* protected by page_table_lock */
#endif

#ifdef CONFIG_NUMA_BALANCING
		/*
		 * numa_next_scan is the next time that the PTEs will be marked
		 * pte_numa. NUMA hinting faults will gather statistics and
		 * migrate pages to new nodes if necessary.
		 */
		unsigned long numa_next_scan;

		/* Restart point for scanning and setting pte_numa */
		unsigned long numa_scan_offset;

		/* numa_scan_seq prevents two threads setting pte_numa */
		int numa_scan_seq;
#endif
		/*
		 * An operation with batched TLB flushing is going on. Anything
		 * that can move process memory needs to flush the TLB when
		 * moving a PROT_NONE or PROT_NUMA mapped page.
		 */
		atomic_t tlb_flush_pending;
#ifdef CONFIG_ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH
		/* See flush_tlb_batched_pending() */
		bool tlb_flush_batched;
#endif
		struct uprobes_state uprobes_state;
#ifdef CONFIG_HUGETLB_PAGE
		atomic_long_t hugetlb_usage;
#endif
		struct work_struct async_put_work;

#ifdef CONFIG_IOMMU_SUPPORT
		u32 pasid;
#endif
	}/* __randomize_layout ---*/;

	/*
	 * The mm_cpumask needs to be at the end of mm_struct, because it
	 * is dynamically sized based on nr_cpu_ids.
	 */
	unsigned long cpu_bitmap[];
};

extern struct mm_struct init_mm;

/* Pointer magic because the dynamic array size confuses some compilers. */
//sets the [cpumask] pointer to the memory descriptor `cpumask`
static inline void mm_init_cpumask(struct mm_struct *mm)
{
	unsigned long cpu_bitmap = (unsigned long)mm;

	cpu_bitmap += offsetof(struct mm_struct, cpu_bitmap);
	cpumask_clear((struct cpumask *)cpu_bitmap);
}

/* Future-safe accessor for struct mm_struct's cpu_vm_mask. */
static inline cpumask_t *mm_cpumask(struct mm_struct *mm)
{
	return (struct cpumask *)&mm->cpu_bitmap;
}

struct mmu_gather;
extern void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm,
				unsigned long start, unsigned long end);
extern void tlb_finish_mmu(struct mmu_gather *tlb,
				unsigned long start, unsigned long end);

static inline void init_tlb_flush_pending(struct mm_struct *mm)
{
	atomic_set(&mm->tlb_flush_pending, 0);
}

static inline void inc_tlb_flush_pending(struct mm_struct *mm)
{
	atomic_inc(&mm->tlb_flush_pending);
	/*
	 * The only time this value is relevant is when there are indeed pages
	 * to flush. And we'll only flush pages after changing them, which
	 * requires the PTL.
	 *
	 * So the ordering here is:
	 *
	 *	atomic_inc(&mm->tlb_flush_pending);
	 *	spin_lock(&ptl);
	 *	...
	 *	set_pte_at();
	 *	spin_unlock(&ptl);
	 *
	 *				spin_lock(&ptl)
	 *				mm_tlb_flush_pending();
	 *				....
	 *				spin_unlock(&ptl);
	 *
	 *	flush_tlb_range();
	 *	atomic_dec(&mm->tlb_flush_pending);
	 *
	 * Where the increment if constrained by the PTL unlock, it thus
	 * ensures that the increment is visible if the PTE modification is
	 * visible. After all, if there is no PTE modification, nobody cares
	 * about TLB flushes either.
	 *
	 * This very much relies on users (mm_tlb_flush_pending() and
	 * mm_tlb_flush_nested()) only caring about _specific_ PTEs (and
	 * therefore specific PTLs), because with SPLIT_PTE_PTLOCKS and RCpc
	 * locks (PPC) the unlock of one doesn't order against the lock of
	 * another PTL.
	 *
	 * The decrement is ordered by the flush_tlb_range(), such that
	 * mm_tlb_flush_pending() will not return false unless all flushes have
	 * completed.
	 */
}

static inline void dec_tlb_flush_pending(struct mm_struct *mm)
{
	/*
	 * See inc_tlb_flush_pending().
	 *
	 * This cannot be smp_mb__before_atomic() because smp_mb() simply does
	 * not order against TLB invalidate completion, which is what we need.
	 *
	 * Therefore we must rely on tlb_flush_*() to guarantee order.
	 */
	atomic_dec(&mm->tlb_flush_pending);
}

static inline bool mm_tlb_flush_pending(struct mm_struct *mm)
{
	/*
	 * Must be called after having acquired the PTL; orders against that
	 * PTLs release and therefore ensures that if we observe the modified
	 * PTE we must also observe the increment from inc_tlb_flush_pending().
	 *
	 * That is, it only guarantees to return true if there is a flush
	 * pending for _this_ PTL.
	 */
	return atomic_read(&mm->tlb_flush_pending);
}

static inline bool mm_tlb_flush_nested(struct mm_struct *mm)
{
	/*
	 * Similar to mm_tlb_flush_pending(), we must have acquired the PTL
	 * for which there is a TLB flush pending in order to guarantee
	 * we've seen both that PTE modification and the increment.
	 *
	 * (no requirement on actually still holding the PTL, that is irrelevant)
	 */
	return atomic_read(&mm->tlb_flush_pending) > 1;
}

struct vm_fault;

/**
 * typedef vm_fault_t - Return type for page fault handlers.
 *
 * Page fault handlers return a bitmask of %VM_FAULT values.
 */
typedef __bitwise unsigned int vm_fault_t;

/**
 * enum vm_fault_reason - Page fault handlers return a bitmask of
 * these values to tell the core VM what happened when handling the
 * fault. Used to decide whether a process gets delivered SIGBUS or
 * just gets major/minor fault counters bumped up.
 *
 * @VM_FAULT_OOM:		Out Of Memory
 * @VM_FAULT_SIGBUS:		Bad access
 * @VM_FAULT_MAJOR:		Page read from storage
 * @VM_FAULT_WRITE:		Special case for get_user_pages
 * @VM_FAULT_HWPOISON:		Hit poisoned small page
 * @VM_FAULT_HWPOISON_LARGE:	Hit poisoned large page. Index encoded
 *				in upper bits
 * @VM_FAULT_SIGSEGV:		segmentation fault
 * @VM_FAULT_NOPAGE:		->fault installed the pte, not return page
 * @VM_FAULT_LOCKED:		->fault locked the returned page
 * @VM_FAULT_RETRY:		->fault blocked, must retry
 * @VM_FAULT_FALLBACK:		huge page fault failed, fall back to small
 * @VM_FAULT_DONE_COW:		->fault has fully handled COW
 * @VM_FAULT_NEEDDSYNC:		->fault did not modify page tables and needs
 *				fsync() to complete (for synchronous page faults
 *				in DAX)
 * @VM_FAULT_HINDEX_MASK:	mask HINDEX value
 *
 * 处理 缺页异常过程中 返回的 转状态信息
 */
enum vm_fault_reason {
	VM_FAULT_OOM            = (__force vm_fault_t)0x000001,
	VM_FAULT_SIGBUS         = (__force vm_fault_t)0x000002,
	VM_FAULT_MAJOR          = (__force vm_fault_t)0x000004, /* 主缺页: 从交换分区中加载 page */
	VM_FAULT_WRITE          = (__force vm_fault_t)0x000008,
	VM_FAULT_HWPOISON       = (__force vm_fault_t)0x000010,
	VM_FAULT_HWPOISON_LARGE = (__force vm_fault_t)0x000020,
	VM_FAULT_SIGSEGV        = (__force vm_fault_t)0x000040, /* 段错误 */
	VM_FAULT_NOPAGE         = (__force vm_fault_t)0x000100,
	VM_FAULT_LOCKED         = (__force vm_fault_t)0x000200,
	VM_FAULT_RETRY          = (__force vm_fault_t)0x000400,
	VM_FAULT_FALLBACK       = (__force vm_fault_t)0x000800,
	VM_FAULT_DONE_COW       = (__force vm_fault_t)0x001000,
	VM_FAULT_NEEDDSYNC      = (__force vm_fault_t)0x002000,
	VM_FAULT_HINDEX_MASK    = (__force vm_fault_t)0x0f0000,
};

/* Encode hstate index for a hwpoisoned large page */
#define VM_FAULT_SET_HINDEX(x) ((__force vm_fault_t)((x) << 16))
#define VM_FAULT_GET_HINDEX(x) (((__force unsigned int)(x) >> 16) & 0xf)

/* 缺页异常 - 错误 */
#define VM_FAULT_ERROR (VM_FAULT_OOM | VM_FAULT_SIGBUS |	\
			VM_FAULT_SIGSEGV | VM_FAULT_HWPOISON |	\
			VM_FAULT_HWPOISON_LARGE | VM_FAULT_FALLBACK)

#define VM_FAULT_RESULT_TRACE \
	{ VM_FAULT_OOM,                 "OOM" },	\
	{ VM_FAULT_SIGBUS,              "SIGBUS" },	\
	{ VM_FAULT_MAJOR,               "MAJOR" },	\
	{ VM_FAULT_WRITE,               "WRITE" },	\
	{ VM_FAULT_HWPOISON,            "HWPOISON" },	\
	{ VM_FAULT_HWPOISON_LARGE,      "HWPOISON_LARGE" },	\
	{ VM_FAULT_SIGSEGV,             "SIGSEGV" },	\
	{ VM_FAULT_NOPAGE,              "NOPAGE" },	\
	{ VM_FAULT_LOCKED,              "LOCKED" },	\
	{ VM_FAULT_RETRY,               "RETRY" },	\
	{ VM_FAULT_FALLBACK,            "FALLBACK" },	\
	{ VM_FAULT_DONE_COW,            "DONE_COW" },	\
	{ VM_FAULT_NEEDDSYNC,           "NEEDDSYNC" }

/**
 * vDSO
 */
struct vm_special_mapping {
	const char *name;	/* The name, e.g. "[vdso]". */

	/*
	 * If .fault is not provided, this points to a
	 * NULL-terminated array of pages that back the special mapping.
	 *
	 * This must not be NULL unless .fault is provided.
	 */
	struct page **pages;

	/*
	 * If non-NULL, then this is called to resolve page faults
	 * on the special mapping.  If used, .pages is not checked.
	 */
	vm_fault_t (*fault)(const struct vm_special_mapping *sm,
				struct vm_area_struct *vma,
				struct vm_fault *vmf);

	int (*mremap)(const struct vm_special_mapping *sm,
		     struct vm_area_struct *new_vma);
};

enum tlb_flush_reason {
	TLB_FLUSH_ON_TASK_SWITCH,
	TLB_REMOTE_SHOOTDOWN,
	TLB_LOCAL_SHOOTDOWN,
	TLB_LOCAL_MM_SHOOTDOWN,
	TLB_REMOTE_SEND_IPI,
	NR_TLB_FLUSH_REASONS,
};

 /*
  * A swap entry has to fit into a "unsigned long", as the entry is hidden
  * in the "index" field of the swapper address space.
  *
  * swapper address space 的 index
  */
typedef struct {
	unsigned long val;
} swp_entry_t;

#endif /* _LINUX_MM_TYPES_H */
