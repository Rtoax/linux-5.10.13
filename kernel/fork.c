// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/fork.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also entry.S and others).
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/memory.c': 'copy_page_range()'
 */

#include <linux/anon_inodes.h>
#include <linux/slab.h>
#include <linux/sched/autogroup.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/user.h>
#include <linux/sched/numa_balancing.h>
#include <linux/sched/stat.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/cputime.h>
#include <linux/seq_file.h>
#include <linux/rtmutex.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>
#include <linux/personality.h>
#include <linux/mempolicy.h>
#include <linux/sem.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/iocontext.h>
#include <linux/key.h>
#include <linux/binfmts.h>
#include <linux/mman.h>
#include <linux/mmu_notifier.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/vmacache.h>
#include <linux/nsproxy.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/cgroup.h>
#include <linux/security.h>
#include <linux/hugetlb.h>
#include <linux/seccomp.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/jiffies.h>
#include <linux/futex.h>
#include <linux/compat.h>
#include <linux/kthread.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/rcupdate.h>
#include <linux/ptrace.h>
#include <linux/mount.h>
#include <linux/audit.h>
#include <linux/memcontrol.h>
#include <linux/ftrace.h>
#include <linux/proc_fs.h>
#include <linux/profile.h>
#include <linux/rmap.h>
#include <linux/ksm.h>
#include <linux/acct.h>
#include <linux/userfaultfd_k.h>
#include <linux/tsacct_kern.h>
#include <linux/cn_proc.h>
#include <linux/freezer.h>
#include <linux/delayacct.h>
#include <linux/taskstats_kern.h>
#include <linux/random.h>
#include <linux/tty.h>
#include <linux/blkdev.h>
#include <linux/fs_struct.h>
#include <linux/magic.h>
#include <linux/perf_event.h>
#include <linux/posix-timers.h>
#include <linux/user-return-notifier.h>
#include <linux/oom.h>
#include <linux/khugepaged.h>
#include <linux/signalfd.h>
#include <linux/uprobes.h>
#include <linux/aio.h>
#include <linux/compiler.h>
#include <linux/sysctl.h>
#include <linux/kcov.h>
#include <linux/livepatch.h>
#include <linux/thread_info.h>
#include <linux/stackleak.h>
#include <linux/kasan.h>
#include <linux/scs.h>
#include <linux/io_uring.h>

#include <asm/pgalloc.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#include <trace/events/sched.h>

#define CREATE_TRACE_POINTS
#include <trace/events/task.h>

/*
 * Minimum number of threads to boot the kernel
 */
#define MIN_THREADS 20

/*
 * Maximum number of threads
 */
#define MAX_THREADS FUTEX_TID_MASK/* 最大线程数 */

/*
 * Protected counters by write_lock_irq(&tasklist_lock)
 */
unsigned long total_forks;	/* Handle normal Linux uptimes. */
int nr_threads;			/* The idle threads do not count.. */

/**
 *  当前可以拥有的最大进程数
 *  set_max_threads()
 */
static int max_threads;		/* tunable limit on nr_threads */

#define NAMED_ARRAY_INDEX(x)	[x] = __stringify(x)

static const char * const resident_page_types[] = {
	NAMED_ARRAY_INDEX(MM_FILEPAGES),
	NAMED_ARRAY_INDEX(MM_ANONPAGES),
	NAMED_ARRAY_INDEX(MM_SWAPENTS),
	NAMED_ARRAY_INDEX(MM_SHMEMPAGES),
};

DEFINE_PER_CPU(unsigned long, process_counts) = 0;

__cacheline_aligned DEFINE_RWLOCK(tasklist_lock);  /* outer */

#ifdef CONFIG_PROVE_RCU
int lockdep_tasklist_lock_is_held(void)
{
	return lockdep_is_held(&tasklist_lock);
}
EXPORT_SYMBOL_GPL(lockdep_tasklist_lock_is_held);
#endif /* #ifdef CONFIG_PROVE_RCU */

int nr_processes(void)
{
	int cpu;
	int total = 0;

	for_each_possible_cpu(cpu)
		total += per_cpu(process_counts, cpu);

	return total;
}

void __weak arch_release_task_struct(struct task_struct *tsk)
{
}

#ifndef CONFIG_ARCH_TASK_STRUCT_ALLOCATOR
static struct kmem_cache *task_struct_cachep;

static inline struct task_struct *alloc_task_struct_node(int node)  /* 分配内存 */
{
	return kmem_cache_alloc_node(task_struct_cachep, GFP_KERNEL, node);
}

static inline void free_task_struct(struct task_struct *tsk)
{
	kmem_cache_free(task_struct_cachep, tsk);
}
#endif

#ifndef CONFIG_ARCH_THREAD_STACK_ALLOCATOR

/*
 * Allocate pages if THREAD_SIZE is >= PAGE_SIZE, otherwise use a
 * kmemcache based allocator.
 */
# if THREAD_SIZE >= PAGE_SIZE || defined(CONFIG_VMAP_STACK)

#ifdef CONFIG_VMAP_STACK
/*
 * vmalloc() is a bit slow, and calling vfree() enough times will force a TLB
 * flush.  Try to minimize the number of calls by caching stacks.
 */
//#define NR_CACHED_STACKS 2
//static DEFINE_PER_CPU(struct vm_struct *, cached_stacks[NR_CACHED_STACKS]);

//static int free_vm_stack_cache(unsigned int cpu)
//{
//	struct vm_struct **cached_vm_stacks = per_cpu_ptr(cached_stacks, cpu);
//	int i;
//
//	for (i = 0; i < NR_CACHED_STACKS; i++) {
//		struct vm_struct *vm_stack = cached_vm_stacks[i];
//
//		if (!vm_stack)
//			continue;
//
//		vfree(vm_stack->addr);
//		cached_vm_stacks[i] = NULL;
//	}
//
//	return 0;
//}
#endif

/**
 *  为线程 栈分配 物理页
 */
static unsigned long *alloc_thread_stack_node(struct task_struct *tsk, int node)
{
#ifdef CONFIG_VMAP_STACK
//	void *stack;
//	int i;
//
//	for (i = 0; i < NR_CACHED_STACKS; i++) {
//		struct vm_struct *s;
//
//		s = this_cpu_xchg(cached_stacks[i], NULL);
//
//		if (!s)
//			continue;
//
//		/* Clear the KASAN shadow of the stack. */
//		kasan_unpoison_shadow(s->addr, THREAD_SIZE);
//
//		/* Clear stale pointers from reused stack. */
//		memset(s->addr, 0, THREAD_SIZE);
//
//		tsk->stack_vm_area = s;
//		tsk->stack = s->addr;
//		return s->addr;
//	}
//
//	/*
//	 * Allocated stacks are cached and later reused by new threads,
//	 * so memcg accounting is performed manually on assigning/releasing
//	 * stacks to tasks. Drop __GFP_ACCOUNT.
//	 */
//	stack = __vmalloc_node_range(THREAD_SIZE, THREAD_ALIGN,
//				     VMALLOC_START, VMALLOC_END,
//				     THREADINFO_GFP & ~__GFP_ACCOUNT,
//				     PAGE_KERNEL,
//				     0, node, __builtin_return_address(0));
//
//	/*
//	 * We can't call find_vm_area() in interrupt context, and
//	 * free_thread_stack() can be called in interrupt context,
//	 * so cache the vm_struct.
//	 */
//	if (stack) {
//		tsk->stack_vm_area = find_vm_area(stack);
//		tsk->stack = stack;
//	}
//	return stack;
#else
	/**
	 *  为栈分配 page
	 */
	struct page *page = alloc_pages_node(node, THREADINFO_GFP, THREAD_SIZE_ORDER);

	if (likely(page)) {
	    /**
	     *  给栈赋值
	     */
		tsk->stack = kasan_reset_tag(page_address(page)/* 虚拟地址 */);
		return tsk->stack;
	}
	return NULL;
#endif
}

static inline void free_thread_stack(struct task_struct *tsk)
{
#ifdef CONFIG_VMAP_STACK
//	struct vm_struct *vm = task_stack_vm_area(tsk);
//
//	if (vm) {
//		int i;
//
//		for (i = 0; i < THREAD_SIZE / PAGE_SIZE; i++)
//			memcg_kmem_uncharge_page(vm->pages[i], 0);
//
//		for (i = 0; i < NR_CACHED_STACKS; i++) {
//			if (this_cpu_cmpxchg(cached_stacks[i],
//					NULL, tsk->stack_vm_area) != NULL)
//				continue;
//
//			return;
//		}
//
//		vfree_atomic(tsk->stack);
//		return;
//	}
#endif

	__free_pages(virt_to_page(tsk->stack), THREAD_SIZE_ORDER);
}
# else
//static struct kmem_cache *thread_stack_cache;
//
//static unsigned long *alloc_thread_stack_node(struct task_struct *tsk,
//						  int node)
//{
//	unsigned long *stack;
//	stack = kmem_cache_alloc_node(thread_stack_cache, THREADINFO_GFP, node);
//	stack = kasan_reset_tag(stack);
//	tsk->stack = stack;
//	return stack;
//}
//
//static void free_thread_stack(struct task_struct *tsk)
//{
//	kmem_cache_free(thread_stack_cache, tsk->stack);
//}
//
//void thread_stack_cache_init(void)  /*线程栈  */
//{
//	thread_stack_cache = kmem_cache_create_usercopy("thread_stack",
//					THREAD_SIZE, THREAD_SIZE, 0, 0,
//					THREAD_SIZE, NULL);
//	BUG_ON(thread_stack_cache == NULL);
//}
# endif
#endif

/* SLAB cache for signal_struct structures (tsk->signal) */
static struct kmem_cache *signal_cachep;

/* SLAB cache for sighand_struct structures (tsk->sighand) */
struct kmem_cache *sighand_cachep;

/* SLAB cache for files_struct structures (tsk->files) */
struct kmem_cache *files_cachep;

/* SLAB cache for fs_struct structures (tsk->fs) */
struct kmem_cache *fs_cachep;

/* SLAB cache for vm_area_struct structures */
static struct kmem_cache *vm_area_cachep;

/* SLAB cache for mm_struct structures (tsk->mm) */
static struct kmem_cache *mm_cachep;

struct vm_area_struct *vm_area_alloc(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	vma = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (vma)
		vma_init(vma, mm);
	return vma;
}
/**
 * 复制一个 vma 结构
 * 当 拆分一个 vma 时候，将使用这个接口
 */
struct vm_area_struct *vm_area_dup(struct vm_area_struct *orig)
{
	struct vm_area_struct *new = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);/* 分配内存 */

	if (new) {
		/* 并发性检测内容 */
		ASSERT_EXCLUSIVE_WRITER(orig->vm_flags);
		ASSERT_EXCLUSIVE_WRITER(orig->vm_file);
		/*
		 * orig->shared.rb may be modified concurrently, but the clone
		 * will be reinitialized.
		 */
		*new = data_race(*orig);    /* 复制这个数据结构中的内容 */
		INIT_LIST_HEAD(&new->anon_vma_chain);   /* 初始化自己的链表 */
		new->vm_next = new->vm_prev = NULL; /* 链表节点初始化 */
	}
	return new;
}

void vm_area_free(struct vm_area_struct *vma)
{
	kmem_cache_free(vm_area_cachep, vma);
}

/**
 *
 */
static void account_kernel_stack(struct task_struct *tsk, int account)
{
	/**
	 *  栈的虚拟地址
	 */
	void *stack = task_stack_page(tsk); /* tast_struct->stack */

	/**
	 *
	 */
	struct vm_struct *vm = task_stack_vm_area(tsk); /* t->stack_vm_area */

	/* All stack pages are in the same node. */
	if (vm)
		mod_lruvec_page_state(vm->pages[0], NR_KERNEL_STACK_KB, account * (THREAD_SIZE / 1024));
	else
	    /**
	     *
	     */
		mod_lruvec_slab_state(stack, NR_KERNEL_STACK_KB, account * (THREAD_SIZE / 1024));
}

/**
 *
 */
static int memcg_charge_kernel_stack(struct task_struct *tsk)
{
#ifdef CONFIG_VMAP_STACK
	struct vm_struct *vm = task_stack_vm_area(tsk);
	int ret;

	BUILD_BUG_ON(IS_ENABLED(CONFIG_VMAP_STACK) && PAGE_SIZE % 1024 != 0);

	if (vm) {
		int i;

		BUG_ON(vm->nr_pages != THREAD_SIZE / PAGE_SIZE);

		for (i = 0; i < THREAD_SIZE / PAGE_SIZE; i++) {
			/*
			 * If memcg_kmem_charge_page() fails, page->mem_cgroup
			 * pointer is NULL, and memcg_kmem_uncharge_page() in
			 * free_thread_stack() will ignore this page.
			 */
			ret = memcg_kmem_charge_page(vm->pages[i], GFP_KERNEL,
						     0);
			if (ret)
				return ret;
		}
	}
#endif
	return 0;
}

static void release_task_stack(struct task_struct *tsk)
{
	if (WARN_ON(tsk->state != TASK_DEAD))
		return;  /* Better to leak the stack than to free prematurely */

	account_kernel_stack(tsk, -1);
	free_thread_stack(tsk);
	tsk->stack = NULL;
#ifdef CONFIG_VMAP_STACK
	tsk->stack_vm_area = NULL;
#endif
}

#ifdef CONFIG_THREAD_INFO_IN_TASK
void put_task_stack(struct task_struct *tsk)
{
	if (refcount_dec_and_test(&tsk->stack_refcount))
		release_task_stack(tsk);
}
#endif

void free_task(struct task_struct *tsk)
{
	scs_release(tsk);

#ifndef CONFIG_THREAD_INFO_IN_TASK
	/*
	 * The task is finally done with both the stack and thread_info,
	 * so free both.
	 */
	release_task_stack(tsk);
#else
	/*
	 * If the task had a separate stack allocation, it should be gone
	 * by now.
	 */
	WARN_ON_ONCE(refcount_read(&tsk->stack_refcount) != 0);
#endif
	rt_mutex_debug_task_free(tsk);
	ftrace_graph_exit_task(tsk);
	arch_release_task_struct(tsk);
	if (tsk->flags & PF_KTHREAD)
		free_kthread_struct(tsk);
	free_task_struct(tsk);
}
EXPORT_SYMBOL(free_task);

#ifdef CONFIG_MMU
/**
 *
 */
static __latent_entropy int dup_mmap(struct mm_struct *mm,
					struct mm_struct *oldmm)
{
	struct vm_area_struct *mpnt, *tmp, *prev, **pprev;
	struct rb_node **rb_link, *rb_parent;
	int retval;
	unsigned long charge;
	LIST_HEAD(uf);

	uprobe_start_dup_mmap();

	/**
	 *
	 */
	if (mmap_write_lock_killable(oldmm)) {
		retval = -EINTR;
		goto fail_uprobe_end;
	}
	/**
	 *  为空
	 */
	flush_cache_dup_mm(oldmm);

	/**
	 *
	 */
	uprobe_dup_mmap(oldmm, mm);

	/*
	 * Not linked in yet - no deadlock potential:
	 */
	mmap_write_lock_nested(mm, SINGLE_DEPTH_NESTING);

	/* No ordering required: file already has been exposed. */
	RCU_INIT_POINTER(mm->exe_file, get_mm_exe_file(oldmm));

	mm->total_vm = oldmm->total_vm;
	mm->data_vm = oldmm->data_vm;
	mm->exec_vm = oldmm->exec_vm;
	mm->stack_vm = oldmm->stack_vm;

	rb_link = &mm->mm_rb.rb_node;
	rb_parent = NULL;
	pprev = &mm->mmap;

	/**
	 *  同页合并 - 显式的将 进程地址空间添加到 KSM 系统中
	 */
	retval = ksm_fork(mm, oldmm);
	if (retval)
		goto out;

	/**
	 *  大页内存
	 */
	retval = khugepaged_fork(mm, oldmm);
	if (retval)
		goto out;

	prev = NULL;

	/**
	 *  遍历 父进程的 VMA 结构
	 */
	for (mpnt = oldmm->mmap; mpnt; mpnt = mpnt->vm_next) {
		struct file *file;

		if (mpnt->vm_flags & VM_DONTCOPY) {
			vm_stat_account(mm, mpnt->vm_flags, -vma_pages(mpnt));
			continue;
		}
		charge = 0;
		/*
		 * Don't duplicate many vmas if we've been oom-killed (for
		 * example)
		 */
		if (fatal_signal_pending(current)) {
			retval = -EINTR;
			goto out;
		}
		if (mpnt->vm_flags & VM_ACCOUNT) {
			unsigned long len = vma_pages(mpnt);

			if (security_vm_enough_memory_mm(oldmm, len)) /* sic */
				goto fail_nomem;
			charge = len;
		}

	    /* 分配内存与初始化 */
		tmp = vm_area_dup(mpnt);
		if (!tmp)
			goto fail_nomem;

	    /* 策略 */
		retval = vma_dup_policy(mpnt, tmp);
		if (retval)
			goto fail_nomem_policy;

	    /* 赋值 */
		tmp->vm_mm = mm;


		retval = dup_userfaultfd(tmp, &uf);
		if (retval)
			goto fail_nomem_anon_vma_fork;


		if (tmp->vm_flags & VM_WIPEONFORK/* 擦除 */) {
			/*
			 * VM_WIPEONFORK gets a clean slate in the child.
			 * Don't prepare anon_vma until fault since we don't
			 * copy page for current vma.
			 *
			 * 这个标识位 将表明，在 fork 阶段不分配 anon_vma，而是在
			 * 缺页时候在分配.
			 *
			 * 详情见 `  madvise(2) MADV_WIPEONFORK
			 */
			tmp->anon_vma = NULL;

		}
	    /**
	     *  RMAP
	     */
	    else if (anon_vma_fork(tmp, mpnt))
			goto fail_nomem_anon_vma_fork;

	    /**
	     *
	     */
		tmp->vm_flags &= ~(VM_LOCKED | VM_LOCKONFAULT);

	    /**
	     *  这个 vma 是 文件映射
	     */
		file = tmp->vm_file;
		if (file) {
	        /**
	         *  获得这个文件
	         */
			struct inode *inode = file_inode(file);

	        /**
	         *  缓存
	         */
			struct address_space *mapping = file->f_mapping;

	        /**
	         *  引用计数
	         */
			get_file(file);
			if (tmp->vm_flags & VM_DENYWRITE)
				put_write_access(inode);
			i_mmap_lock_write(mapping);
			if (tmp->vm_flags & VM_SHARED)
				mapping_allow_writable(mapping);

	        /**
	         *
	         */
			flush_dcache_mmap_lock(mapping);

			/**
			 *  insert tmp into the share list, just after mpnt
			 */
			vma_interval_tree_insert_after(tmp, mpnt, &mapping->i_mmap);

			flush_dcache_mmap_unlock(mapping);
			i_mmap_unlock_write(mapping);
		}

		/*
		 * Clear hugetlb-related page reserves for children. This only
		 * affects MAP_PRIVATE mappings. Faults generated by the child
		 * are not guaranteed to succeed, even if read-only
		 */
		if (is_vm_hugetlb_page(tmp))
			reset_vma_resv_huge_pages(tmp);

		/*
		 * Link in the new vma and copy the page table entries.
		 */ /* 链表 */
		*pprev = tmp;
		pprev = &tmp->vm_next;
		tmp->vm_prev = prev;
		prev = tmp;

	    /* 红黑树操作 */
		__vma_link_rb(mm, tmp, rb_link, rb_parent);
		/* 往后(右)插入 */
		rb_link = &tmp->vm_rb.rb_right;
		rb_parent = &tmp->vm_rb;

		mm->map_count++;
		if (!(tmp->vm_flags & VM_WIPEONFORK))
	        /**
	         *  复制 父进程的进程地址空间相应页表的核心实现函数
	         */
			retval = copy_page_range(tmp, mpnt);

		if (tmp->vm_ops && tmp->vm_ops->open)
			tmp->vm_ops->open(tmp);

		if (retval)
			goto out;
	}

	/**
	 *
	 */
	/* a new mm has just been created */
	retval = arch_dup_mmap(oldmm, mm);

out:
	mmap_write_unlock(mm);
	flush_tlb_mm(oldmm);
	mmap_write_unlock(oldmm);
	dup_userfaultfd_complete(&uf);
fail_uprobe_end:
	uprobe_end_dup_mmap();

	/**
	 *  正常返回
	 */
	return retval;

fail_nomem_anon_vma_fork:
	mpol_put(vma_policy(tmp));
fail_nomem_policy:
	vm_area_free(tmp);
fail_nomem:
	retval = -ENOMEM;
	vm_unacct_memory(charge);
	goto out;
}

/**
 *  为 新进程分配 pgd
 */
static inline int mm_alloc_pgd(struct mm_struct *mm)
{
	/**
	 *  分配
	 */
	mm->pgd = pgd_alloc(mm);
	if (unlikely(!mm->pgd))
		return -ENOMEM;

	return 0;
}

static inline void mm_free_pgd(struct mm_struct *mm)
{
	pgd_free(mm, mm->pgd);
}
#else

#endif /* CONFIG_MMU */

static void check_mm(struct mm_struct *mm)
{
	int i;

	BUILD_BUG_ON_MSG(ARRAY_SIZE(resident_page_types) != NR_MM_COUNTERS,
			 "Please make sure 'struct resident_page_types[]' is updated as well");

	for (i = 0; i < NR_MM_COUNTERS; i++) {
		long x = atomic_long_read(&mm->rss_stat.count[i]);

		if (unlikely(x))
			pr_alert("BUG: Bad rss-counter state mm:%p type:%s val:%ld\n",
				 mm, resident_page_types[i], x);
	}

	if (mm_pgtables_bytes(mm))
		pr_alert("BUG: non-zero pgtables_bytes on freeing mm: %ld\n",
				mm_pgtables_bytes(mm));

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && !USE_SPLIT_PMD_PTLOCKS
	VM_BUG_ON_MM(mm->pmd_huge_pte, mm);
#endif
}

#define allocate_mm()	(kmem_cache_alloc(mm_cachep, GFP_KERNEL))
#define free_mm(mm)	(kmem_cache_free(mm_cachep, (mm)))

/*
 * Called when the last reference to the mm
 * is dropped: either by a lazy thread or by
 * mmput. Free the page directory and the mm.
 *
 * 如果引用计数 == 0,那么可以释放这个 mm 结构了
 */
void __mmdrop(struct mm_struct *mm)
{
	BUG_ON(mm == &init_mm);
	WARN_ON_ONCE(mm == current->mm);
	WARN_ON_ONCE(mm == current->active_mm);
	mm_free_pgd(mm);
	destroy_context(mm);
	mmu_notifier_subscriptions_destroy(mm);
	check_mm(mm);
	put_user_ns(mm->user_ns);
	free_mm(mm);
}
EXPORT_SYMBOL_GPL(__mmdrop);

static void mmdrop_async_fn(struct work_struct *work)
{
	struct mm_struct *mm;

	mm = container_of(work, struct mm_struct, async_put_work);
	__mmdrop(mm);
}

static void mmdrop_async(struct mm_struct *mm)
{
	if (unlikely(atomic_dec_and_test(&mm->mm_count))) {
		INIT_WORK(&mm->async_put_work, mmdrop_async_fn);
		schedule_work(&mm->async_put_work);
	}
}

static inline void free_signal_struct(struct signal_struct *sig)
{
	taskstats_tgid_free(sig);
	sched_autogroup_exit(sig);
	/*
	 * __mmdrop is not safe to call from softirq context on x86 due to
	 * pgd_dtor so postpone it to the async context
	 */
	if (sig->oom_mm)
		mmdrop_async(sig->oom_mm);
	kmem_cache_free(signal_cachep, sig);
}

static inline void put_signal_struct(struct signal_struct *sig)
{
	if (refcount_dec_and_test(&sig->sigcnt))
		free_signal_struct(sig);
}

void __put_task_struct(struct task_struct *tsk)
{
	WARN_ON(!tsk->exit_state);
	WARN_ON(refcount_read(&tsk->usage));
	WARN_ON(tsk == current);

	io_uring_free(tsk);
	cgroup_free(tsk);
	task_numa_free(tsk, true);
	security_task_free(tsk);
	exit_creds(tsk);
	delayacct_tsk_free(tsk);
	put_signal_struct(tsk->signal);

	if (!profile_handoff_task(tsk))
		free_task(tsk);
}
EXPORT_SYMBOL_GPL(__put_task_struct);

void __init __weak arch_task_cache_init(void) { }
//在老本版内核中，该函数可能为：
//void arch_task_cache_init(void)
//{
////the `task_xstate` which represents [FPU](http://en.wikipedia.org/wiki/Floating-point_unit)
//
//    task_xstate_cachep =
//            kmem_cache_create("task_xstate", xstate_size,
//                              __alignof__(union thread_xstate),
//                              SLAB_PANIC | SLAB_NOTRACK, NULL);
//    setup_xstate_comp();
//}


/*
 * set_max_threads
 *
 *  当前可以拥有的最大进程数
 */
static void set_max_threads(unsigned int max_threads_suggested)
{
	u64 threads;
	unsigned long nr_pages = totalram_pages();

	/*
	 * The number of threads shall be limited such that the thread
	 * structures may only consume a small part of the available memory.
	 */
	if (fls64(nr_pages) + fls64(PAGE_SIZE) > 64)
		threads = MAX_THREADS;
	else
		threads = div64_u64((u64) nr_pages * (u64) PAGE_SIZE,
				    (u64) THREAD_SIZE * 8UL);

	if (threads > max_threads_suggested)
		threads = max_threads_suggested;

	/* 最大线程数 */
	max_threads = clamp_t(u64, threads, MIN_THREADS, MAX_THREADS);
}

#ifdef CONFIG_ARCH_WANTS_DYNAMIC_TASK_STRUCT
/* Initialized by the architecture: */
/**
 *
 */
int __read_mostly arch_task_struct_size ;
#endif

#ifndef CONFIG_ARCH_TASK_STRUCT_ALLOCATOR
/**
 *  task_struct 结构中 user copy 白名单
 */
static void task_struct_whitelist(unsigned long *offset, unsigned long *size)
{
	/**
	 *  x86 和 fpu 在硬件上下文 thread_struct 中的偏移相关
	 */
	/* Fetch thread_struct whitelist for the architecture. */
	arch_thread_struct_whitelist(offset, size);

	/*
	 * Handle zero-sized whitelist or empty thread_struct, otherwise
	 * adjust offset to position of thread_struct in task_struct.
	 */
	if (unlikely(*size == 0))
		*offset = 0;
	else
		*offset += offsetof(struct task_struct, thread);
}
#endif /* CONFIG_ARCH_TASK_STRUCT_ALLOCATOR */

//allocates cache for the `task_struct`
void __init fork_init(void)
{
	int i;
#ifndef CONFIG_ARCH_TASK_STRUCT_ALLOCATOR
#ifndef ARCH_MIN_TASKALIGN
#define ARCH_MIN_TASKALIGN	0
#endif
	int align = max_t(int, L1_CACHE_BYTES, ARCH_MIN_TASKALIGN);
	unsigned long useroffset, usersize;

	/**
	 *  哪部分是 用户 可以 copy的
	 */
	/* create a slab on which task_structs can be allocated */
	task_struct_whitelist(&useroffset, &usersize);

	/**
	 *  创建 task_struct 的 slab  缓存
	 */
	//sudo cat /proc/slabinfo | grep task
	task_struct_cachep = kmem_cache_create_usercopy("task_struct",
	                    			arch_task_struct_size, align,
	                    			SLAB_PANIC|SLAB_ACCOUNT,
	                    			useroffset, usersize, NULL);
#endif

	/* do the arch specific task caches init */
	arch_task_cache_init(); /* x86 为空 */

	/**
	 *  当前可以拥有的最大进程数
	 */
	set_max_threads(MAX_THREADS);   /* 最大线程数 */

	//initialize [signal] handler
	init_task.signal->rlim[RLIMIT_NPROC].rlim_cur = max_threads/2;
	init_task.signal->rlim[RLIMIT_NPROC].rlim_max = max_threads/2;
	init_task.signal->rlim[RLIMIT_SIGPENDING] =
		init_task.signal->rlim[RLIMIT_NPROC];

	for (i = 0; i < UCOUNT_COUNTS; i++) {
		init_user_ns.ucount_max[i] = max_threads/2;
	}

#ifdef CONFIG_VMAP_STACK
	cpuhp_setup_state(CPUHP_BP_PREPARE_DYN, "fork:vm_stack_cache",
			  NULL, free_vm_stack_cache);
#endif

	scs_init();

	lockdep_init_task(&init_task);  /* 死锁检测 */
	uprobes_init(); /* uprobes trace工具 */
}

int __weak arch_dup_task_struct(struct task_struct *dst,
				struct task_struct *src)
{
	*dst = *src;
	return 0;
}

void set_task_stack_end_magic(struct task_struct *tsk)/* 设置越界检测 以检测堆栈溢出 */
{
//+-----------------------+
//|                       |
//|        stack          |
//|_______________________|
//|          |            |
//|__________↓____________|             +--------------------+
//|                       |             |                    |
//|      thread_info      |<----------->|     task_struct    |
//+-----------------------+             +--------------------+

	unsigned long *stackend;

	stackend = end_of_stack(tsk);
	*stackend = STACK_END_MAGIC;	/* for overflow detection 越界检测*/
}

/**
 *  拷贝一个新的 task_struct
 *  为新进程分配一个进程描述符 和 内核栈
 */
static struct task_struct *dup_task_struct(struct task_struct *orig, int node)
{
	struct task_struct *tsk;
	unsigned long *stack;
	struct vm_struct __maybe_unused *stack_vm_area ;
	int err;

	if (node == NUMA_NO_NODE)
		node = tsk_fork_get_node(orig); /* kthreadd 的node */

	/**
	 *  为新进程分配一个进程描述符
	 */
	tsk = alloc_task_struct_node(node); /* kmem_cache_alloc *//* 分配 task_struct 结构 */
	if (!tsk)
		return NULL;

	/**
	 *  为新进程分配一个  内核栈
	 */
	stack = alloc_thread_stack_node(tsk, node); /* task_struct->stack */
	if (!stack)
		goto free_tsk;

	/**
	 *  该函数为空
	 */
	if (memcg_charge_kernel_stack(tsk)) /* =0 */
		goto free_stack;

	stack_vm_area = task_stack_vm_area(tsk);    /* =NULL */

	/**
	 *  赋值 task_struct结构，和 fpu
	 */
	err = arch_dup_task_struct(tsk, orig);  /* 直接赋值 */

	/*
	 * arch_dup_task_struct() clobbers the stack-related fields.  Make
	 * sure they're properly initialized before using any stack-related
	 * functions again.
	 */
	tsk->stack = stack; /* 进程栈 */

#ifdef CONFIG_VMAP_STACK
	tsk->stack_vm_area = stack_vm_area;
#endif

#ifdef CONFIG_THREAD_INFO_IN_TASK
	/**
	 *  栈的引用计数
	 */
	refcount_set(&tsk->stack_refcount, 1);  /* 栈的引用计数 */
#endif

	if (err)
		goto free_stack;

	/**
	 *
	 */
	err = scs_prepare(tsk, node);   /* shadow call stack  */
	if (err)
		goto free_stack;

#ifdef CONFIG_SECCOMP   /* 限制系统调用 */
	/*
	 * We must handle setting up seccomp filters once we're under
	 * the sighand lock in case orig has changed between now and
	 * then. Until then, filter must be NULL to avoid messing up
	 * the usage counts on the error path calling free_task.
	 */
	tsk->seccomp.filter = NULL;
#endif

	/**
	 *  为空
	 */
	setup_thread_stack(tsk, orig);

	/**
	 *  标志位 设置
	 */
	clear_user_return_notifier(tsk);/* 清理 标志位 */
	clear_tsk_need_resched(tsk);    /* 清理 标志位 */
	set_task_stack_end_magic(tsk);  /* 设置栈边界 magic，用于溢出监测 */

/**
 * 这里不是 CONFIG_STACKPROTECTOR_PER_TASK 是因为x86平台此特性兼容的历史原因，
 * 这里欠缺一点优雅
 */
#ifdef CONFIG_STACKPROTECTOR
	/**
	 * 金丝雀，栈保护
	 * fork线程时总是新生成一个随机数作为新线程的canary
	 */
	tsk->stack_canary = get_random_canary();
#endif

	/**
	 *
	 */
	if (orig->cpus_ptr == &orig->cpus_mask) /* CPU亲和性 */
		tsk->cpus_ptr = &tsk->cpus_mask;

	/*
	 * One for the user space visible state that goes away when reaped.
	 * One for the scheduler.
	 */
	refcount_set(&tsk->rcu_users, 2);

	/* One for the rcu users */
	refcount_set(&tsk->usage, 1);

#ifdef CONFIG_BLK_DEV_IO_TRACE
	tsk->btrace_seq = 0;    /* 块设备 IO trace */
#endif

	/**
	 *
	 */
	tsk->splice_pipe = NULL;
	tsk->task_frag.page = NULL;
	tsk->wake_q.next = NULL;

	/**
	 *
	 */
	account_kernel_stack(tsk, 1);

	/**
	 *
	 */
	kcov_task_init(tsk);    /* 覆盖检测 */

#ifdef CONFIG_FAULT_INJECTION
	tsk->fail_nth = 0;
#endif

#ifdef CONFIG_BLK_CGROUP
	tsk->throttle_queue = NULL;
	tsk->use_memdelay = 0;
#endif

#ifdef CONFIG_MEMCG
	tsk->active_memcg = NULL;
#endif
	return tsk;

free_stack:
	free_thread_stack(tsk);
free_tsk:
	free_task_struct(tsk);
	return NULL;
}

__cacheline_aligned_in_smp DEFINE_SPINLOCK(mmlist_lock);

static unsigned long default_dump_filter = MMF_DUMP_FILTER_DEFAULT;

static int __init coredump_filter_setup(char *s)
{
	default_dump_filter =
		(simple_strtoul(s, NULL, 0) << MMF_DUMP_FILTER_SHIFT) &
		MMF_DUMP_FILTER_MASK;
	return 1;
}

__setup("coredump_filter=", coredump_filter_setup);

#include <linux/init_task.h>

static void mm_init_aio(struct mm_struct *mm)
{
#ifdef CONFIG_AIO
	spin_lock_init(&mm->ioctx_lock);
	mm->ioctx_table = NULL;
#endif
}

static __always_inline void mm_clear_owner(struct mm_struct *mm,
					   struct task_struct *p)
{
#ifdef CONFIG_MEMCG
	if (mm->owner == p)
		WRITE_ONCE(mm->owner, NULL);
#endif
}

static void mm_init_owner(struct mm_struct *mm, struct task_struct *p)
{
#ifdef CONFIG_MEMCG
	mm->owner = p;
#endif
}

static void mm_init_uprobes_state(struct mm_struct *mm)
{
#ifdef CONFIG_UPROBES
	mm->uprobes_state.xol_area = NULL;
#endif
}

/**
 *  分配 mm_struct 结构后进行的初始化
 */
static struct mm_struct *mm_init(struct mm_struct *mm, struct task_struct *p,
	struct user_namespace *user_ns)
{
	/**
	 *  初始化一系列的数据结构，字段
	 */
	mm->mmap = NULL;    /* VMA list */
	mm->mm_rb = RB_ROOT;
	mm->vmacache_seqnum = 0;
	atomic_set(&mm->mm_users, 1);
	atomic_set(&mm->mm_count, 1);
	seqcount_init(&mm->write_protect_seq);
	mmap_init_lock(mm);
	INIT_LIST_HEAD(&mm->mmlist);
	mm->core_state = NULL;
	mm_pgtables_bytes_init(mm);
	mm->map_count = 0;
	mm->locked_vm = 0;
	atomic_set(&mm->has_pinned, 0);
	atomic64_set(&mm->pinned_vm, 0);
	memset(&mm->rss_stat, 0, sizeof(mm->rss_stat));
	spin_lock_init(&mm->page_table_lock);
	spin_lock_init(&mm->arg_lock);
	mm_init_cpumask(mm);
	mm_init_aio(mm);
	mm_init_owner(mm, p);   /* mm->owner = p */
	RCU_INIT_POINTER(mm->exe_file, NULL);
	mmu_notifier_subscriptions_init(mm);
	init_tlb_flush_pending(mm);

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && !USE_SPLIT_PMD_PTLOCKS
	mm->pmd_huge_pte = NULL;
#endif
	mm_init_uprobes_state(mm);

	/**
	 *  当前进程(父进程)是 用户态进程
	 */
	if (current->mm) {
		mm->flags = current->mm->flags & MMF_INIT_MASK;
		mm->def_flags = current->mm->def_flags & VM_INIT_DEF_MASK;

	/**
	 *  当前进程(父进程)是 内核态进程
	 */
	} else {
		mm->flags = default_dump_filter;
		mm->def_flags = 0;
	}

	/**
	 *  分配 全局页表
	 */
	if (mm_alloc_pgd(mm))
		goto fail_nopgd;

	/**
	 *
	 */
	if (init_new_context(p, mm))
		goto fail_nocontext;

	/**
	 *  namespace 引用计数
	 */
	mm->user_ns = get_user_ns(user_ns);
	return mm;

fail_nocontext:
	mm_free_pgd(mm);
fail_nopgd:
	free_mm(mm);
	return NULL;
}

/*
 * Allocate and initialize an mm_struct.
 */
struct mm_struct *mm_alloc(void)    /* 分配MM结构 */
{
	struct mm_struct *mm;

	mm = allocate_mm(); /* 申请内存 */
	if (!mm)
		return NULL;

	memset(mm, 0, sizeof(*mm));
	return mm_init(mm, current, current_user_ns());
}

static inline void __mmput(struct mm_struct *mm)
{
	VM_BUG_ON(atomic_read(&mm->mm_users));

	uprobe_clear_state(mm);
	exit_aio(mm);
	ksm_exit(mm);
	khugepaged_exit(mm); /* must run before exit_mmap */
	exit_mmap(mm);
	mm_put_huge_zero_page(mm);
	set_mm_exe_file(mm, NULL);
	if (!list_empty(&mm->mmlist)) {
		spin_lock(&mmlist_lock);
		list_del(&mm->mmlist);
		spin_unlock(&mmlist_lock);
	}
	if (mm->binfmt)
		module_put(mm->binfmt->module);
	mmdrop(mm);
}

/*
 * Decrement the use count and release all resources for an mm.
 */
void mmput(struct mm_struct *mm)
{
	might_sleep();

	if (atomic_dec_and_test(&mm->mm_users))
		__mmput(mm);
}
EXPORT_SYMBOL_GPL(mmput);

#ifdef CONFIG_MMU
static void mmput_async_fn(struct work_struct *work)
{
	struct mm_struct *mm = container_of(work, struct mm_struct,
					    async_put_work);

	__mmput(mm);
}

void mmput_async(struct mm_struct *mm)
{
	if (atomic_dec_and_test(&mm->mm_users)) {
		INIT_WORK(&mm->async_put_work, mmput_async_fn);
		schedule_work(&mm->async_put_work);
	}
}
#endif

/**
 * set_mm_exe_file - change a reference to the mm's executable file
 *
 * This changes mm's executable file (shown as symlink /proc/[pid]/exe).
 *
 * Main users are mmput() and sys_execve(). Callers prevent concurrent
 * invocations: in mmput() nobody alive left, in execve task is single
 * threaded. sys_prctl(PR_SET_MM_MAP/EXE_FILE) also needs to set the
 * mm->exe_file, but does so without using set_mm_exe_file() in order
 * to do avoid the need for any locks.
 */
void set_mm_exe_file(struct mm_struct *mm, struct file *new_exe_file)
{
	struct file *old_exe_file;

	/*
	 * It is safe to dereference the exe_file without RCU as
	 * this function is only called if nobody else can access
	 * this mm -- see comment above for justification.
	 */
	old_exe_file = rcu_dereference_raw(mm->exe_file);

	if (new_exe_file)
		get_file(new_exe_file); /* 引用计数 */
	rcu_assign_pointer(mm->exe_file, new_exe_file); /* 符号链接 /proc/PID/exe */
	if (old_exe_file)
		fput(old_exe_file);
}

/**
 * get_mm_exe_file - acquire a reference to the mm's executable file
 *
 * Returns %NULL if mm has no associated executable file.
 * User must release file via fput().
 */
struct file *get_mm_exe_file(struct mm_struct *mm)
{
	struct file *exe_file;

	rcu_read_lock();
	exe_file = rcu_dereference(mm->exe_file);
	if (exe_file && !get_file_rcu(exe_file))
		exe_file = NULL;
	rcu_read_unlock();
	return exe_file;
}
EXPORT_SYMBOL(get_mm_exe_file);

/**
 * get_task_exe_file - acquire a reference to the task's executable file
 *
 * Returns %NULL if task's mm (if any) has no associated executable file or
 * this is a kernel thread with borrowed mm (see the comment above get_task_mm).
 * User must release file via fput().
 */
struct file *get_task_exe_file(struct task_struct *task)
{
	struct file *exe_file = NULL;
	struct mm_struct *mm;

	task_lock(task);
	mm = task->mm;
	if (mm) {
		if (!(task->flags & PF_KTHREAD))
			exe_file = get_mm_exe_file(mm);
	}
	task_unlock(task);
	return exe_file;
}
EXPORT_SYMBOL(get_task_exe_file);

/**
 * get_task_mm - acquire a reference to the task's mm
 *
 * Returns %NULL if the task has no mm.  Checks PF_KTHREAD (meaning
 * this kernel workthread has transiently adopted a user mm with use_mm,
 * to do its AIO) is not set and if so returns a reference to it, after
 * bumping up the use count.  User must release the mm via mmput()
 * after use.  Typically used by /proc and ptrace.
 */
struct mm_struct *get_task_mm(struct task_struct *task)
{
	struct mm_struct *mm;

	task_lock(task);
	mm = task->mm;
	if (mm) {
		if (task->flags & PF_KTHREAD)
			mm = NULL;
		else
			mmget(mm);
	}
	task_unlock(task);
	return mm;
}
EXPORT_SYMBOL_GPL(get_task_mm);

struct mm_struct *mm_access(struct task_struct *task, unsigned int mode)
{
	struct mm_struct *mm;
	int err;

	err =  down_read_killable(&task->signal->exec_update_lock);
	if (err)
		return ERR_PTR(err);

	mm = get_task_mm(task);
	if (mm && mm != current->mm &&
			!ptrace_may_access(task, mode)) {
		mmput(mm);
		mm = ERR_PTR(-EACCES);
	}
	up_read(&task->signal->exec_update_lock);

	return mm;
}

static void complete_vfork_done(struct task_struct *tsk)
{
	struct completion *vfork;

	task_lock(tsk);
	vfork = tsk->vfork_done;
	if (likely(vfork)) {
		tsk->vfork_done = NULL;
		complete(vfork);
	}
	task_unlock(tsk);
}

static int wait_for_vfork_done(struct task_struct *child,
				struct completion *vfork)
{
	int killed;

	freezer_do_not_count();
	cgroup_enter_frozen();
	killed = wait_for_completion_killable(vfork);   /* vfork 等待，直到 子进程 退出 */
	cgroup_leave_frozen(false);
	freezer_count();

	if (killed) {
		task_lock(child);
		child->vfork_done = NULL;
		task_unlock(child);
	}

	put_task_struct(child);
	return killed;
}

/* Please note the differences between mmput and mm_release.
 * mmput is called whenever we stop holding onto a mm_struct,
 * error success whatever.
 *
 * mm_release is called after a mm_struct has been removed
 * from the current process.
 *
 * This difference is important for error handling, when we
 * only half set up a mm_struct for a new process and need to restore
 * the old one.  Because we mmput the new mm_struct before
 * restoring the old one. . .
 * Eric Biederman 10 January 1998
 */
static void mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	uprobe_free_utask(tsk);

	/* Get rid of any cached register state */
	deactivate_mm(tsk, mm);

	/*
	 * Signal userspace if we're not exiting with a core dump
	 * because we want to leave the value intact for debugging
	 * purposes.
	 */
	if (tsk->clear_child_tid) {
		if (!(tsk->signal->flags & SIGNAL_GROUP_COREDUMP) &&
		    atomic_read(&mm->mm_users) > 1) {
			/*
			 * We don't check the error code - if userspace has
			 * not set up a proper pointer then tough luck.
			 */
			put_user(0, tsk->clear_child_tid);
			do_futex(tsk->clear_child_tid, FUTEX_WAKE,
					1, NULL, NULL, 0, 0);
		}
		tsk->clear_child_tid = NULL;
	}

	/*
	 * All done, finally we can wake up parent and return this mm to him.
	 * Also kthread_stop() uses this completion for synchronization.
	 */
	if (tsk->vfork_done)
		complete_vfork_done(tsk);
}

void exit_mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	futex_exit_release(tsk);
	mm_release(tsk, mm);
}

void exec_mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	futex_exec_release(tsk);
	mm_release(tsk, mm);
}

/**
 * dup_mm() - duplicates an existing mm structure
 * @tsk: the task_struct with which the new mm will be associated.
 * @oldmm: the mm to duplicate.
 *
 * Allocates a new mm structure and duplicates the provided @oldmm structure
 * content into it.
 *
 * Return: the duplicated mm or NULL on failure.
 *
 * 复制 mm 结构
 *
 */ /* dup 一个 mm_struct */
static struct mm_struct *dup_mm(struct task_struct *tsk,
				struct mm_struct *oldmm)
{
	struct mm_struct *mm;
	int err;

	/**
	 *  分配 mm 结构
	 */
	mm = allocate_mm(); /* 分配 */
	if (!mm)
		goto fail_nomem;

	/* 直接拷贝 */
	memcpy(mm, oldmm, sizeof(*mm));

	/**
	 *  初始化
	 */
	if (!mm_init(mm, tsk, mm->user_ns))
		goto fail_nomem;

	/**
	 *  映射部分
	 *
	 */
	err = dup_mmap(mm, oldmm);
	if (err)
		goto free_pt;

	/* 内存统计计数 */
	mm->hiwater_rss = get_mm_rss(mm);
	mm->hiwater_vm = mm->total_vm;

	if (mm->binfmt && !try_module_get(mm->binfmt->module))
		goto free_pt;

	return mm;

free_pt:
	/* don't put binfmt in mmput, we haven't got module yet */
	mm->binfmt = NULL;
	mm_init_owner(mm, NULL);
	mmput(mm);

fail_nomem:
	return NULL;
}

/**
 *  拷贝 mm 结构
 */
static int copy_mm(unsigned long clone_flags, struct task_struct *tsk)
{
	struct mm_struct *mm, *oldmm;
	int retval;

	/**
	 *  初始化子进程 mm
	 */
	tsk->min_flt = tsk->maj_flt = 0;
	tsk->nvcsw = tsk->nivcsw = 0;

#ifdef CONFIG_DETECT_HUNG_TASK
	tsk->last_switch_count = tsk->nvcsw + tsk->nivcsw;
	tsk->last_switch_time = 0;
#endif

	tsk->mm = NULL;
	tsk->active_mm = NULL;

	/*
	 * Are we cloning a kernel thread?
	 *
	 * We need to steal a active VM for that..
	 */
	oldmm = current->mm;
	if (!oldmm)
		return 0;

	/* initialize the new vmacache entries */
	vmacache_flush(tsk);    /* 清零 */

	/**
	 *  vfork(2): clone_flags = CLONE_VFORK | CLONE_VM
	 *
	 *  如果克隆 VM ，直接指向 父进程 VM
	 *
	 *  pthread_create ->>>
	 *  clone(..., flags=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|
	 *                   CLONE_THREAD|
	 *                   CLONE_SYSVSEM|CLONE_SETTLS|CLONE_PARENT_SETTID|
	 *                   CLONE_CHILD_CLEARTID, ...)
	 */
	if (clone_flags & CLONE_VM) {   /* 如果共享 VM 区，直接返回 */

		/**
		 *  引用计数
		 *
		 *  pthread_create 会执行这里 父进程和子进程共享 mm
		 *  然后直接返回
		 */
		mmget(oldmm);
		mm = oldmm;
		goto good_mm;
	}

	retval = -ENOMEM;

	/**
	 *  复制 mm
	 */
	mm = dup_mm(tsk, current->mm);
	if (!mm)
		goto fail_nomem;

good_mm:
	tsk->mm = mm;
	tsk->active_mm = mm;
	return 0;

fail_nomem:
	return retval;
}

/**
 *  复制父进程的 fs_struct 数据结构信息
 */
static int copy_fs(unsigned long clone_flags, struct task_struct *tsk)
{
	struct fs_struct *fs = current->fs;
	if (clone_flags & CLONE_FS) {
		/* tsk->fs is already what we want */
		spin_lock(&fs->lock);
		if (fs->in_exec) {
			spin_unlock(&fs->lock);
			return -EAGAIN;
		}
		fs->users++;
		spin_unlock(&fs->lock);
		return 0;
	}

	/**
	 *  拷贝文件系统
	 */
	tsk->fs = copy_fs_struct(fs);
	if (!tsk->fs)
		return -ENOMEM;
	return 0;
}

/**
 *  复制父进程打开的文件等信息
 */
static int copy_files(unsigned long clone_flags, struct task_struct *tsk)
{
	struct files_struct *oldf, *newf;
	int error = 0;

	/*
	 * A background process may not have any files ...
	 */
	oldf = current->files;
	if (!oldf)
		goto out;

	if (clone_flags & CLONE_FILES) {    /* 如果克隆 FILES */
		atomic_inc(&oldf->count);   /* 引用计数+1 返回 */
		goto out;
	}

	/**
	 *
	 */
	newf = dup_fd(oldf, NR_OPEN_MAX, &error);   /* 赋值 FILES */
	if (!newf)
		goto out;

	tsk->files = newf;
	error = 0;
out:
	return error;
}

/**
 *  pthread_create 并没有这个选项
 */
static int copy_io(unsigned long clone_flags, struct task_struct *tsk)
{
	/**
	 *  块设备
	 *
	 * clone(..., flags=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|
	 *                  CLONE_THREAD|CLONE_SYSVSEM|CLONE_SETTLS|
	 *                  CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID, ...)
	 */
#ifdef CONFIG_BLOCK
	struct io_context *ioc = current->io_context;
	struct io_context *new_ioc;

	if (!ioc)
		return 0;
	/*
	 * Share io context with parent, if CLONE_IO is set
	 */
	if (clone_flags & CLONE_IO) {   /* 克隆 IO 上下文 */
		ioc_task_link(ioc);
		tsk->io_context = ioc;

	/**
	 *
	 */
	} else if (ioprio_valid(ioc->ioprio)) {
		new_ioc = get_task_io_context(tsk, GFP_KERNEL, NUMA_NO_NODE);
		if (unlikely(!new_ioc))
			return -ENOMEM;

		new_ioc->ioprio = ioc->ioprio;
		put_io_context(new_ioc);
	}
#endif
	return 0;
}


/**
 *
 */
static int copy_sighand(unsigned long clone_flags, struct task_struct *tsk)
{
	struct sighand_struct *sig;

	/**
	 *  克隆父进程的 sighand
	 */
	if (clone_flags & CLONE_SIGHAND) {
		refcount_inc(&current->sighand->count); /* 添加引用计数 */
		return 0;
	}

	/**
	 *  分配一个结构
	 */
	sig = kmem_cache_alloc(sighand_cachep, GFP_KERNEL);
	RCU_INIT_POINTER(tsk->sighand, sig);
	if (!sig)
		return -ENOMEM;

	/**
	 *  引用计数 =1
	 */
	refcount_set(&sig->count, 1);
	spin_lock_irq(&current->sighand->siglock);

	/**
	 *  拷贝父进程的结构 给子进程
	 */
	memcpy(sig->action, current->sighand->action, sizeof(sig->action));
	spin_unlock_irq(&current->sighand->siglock);

	/* Reset all signal handler not set to SIG_IGN to SIG_DFL. */
	if (clone_flags & CLONE_CLEAR_SIGHAND)
	    /**
	     *  清理 默认值
	     */
		flush_signal_handlers(tsk, 0);

	return 0;
}

void __cleanup_sighand(struct sighand_struct *sighand)
{
	if (refcount_dec_and_test(&sighand->count)) {
		signalfd_cleanup(sighand);
		/*
		 * sighand_cachep is SLAB_TYPESAFE_BY_RCU so we can free it
		 * without an RCU grace period, see __lock_task_sighand().
		 */
		kmem_cache_free(sighand_cachep, sighand);
	}
}

/*
 * Initialize POSIX timer handling for a thread group.
 */
static void posix_cpu_timers_init_group(struct signal_struct *sig)
{
	struct posix_cputimers *pct = &sig->posix_cputimers;
	unsigned long cpu_limit;

	cpu_limit = READ_ONCE(sig->rlim[RLIMIT_CPU].rlim_cur);
	posix_cputimers_group_init(pct, cpu_limit);
}


/**
 * 信号 - 继承父进程的信号系统
 */
static int copy_signal(unsigned long clone_flags, struct task_struct *tsk)
{
	struct signal_struct *sig;

	if (clone_flags & CLONE_THREAD)
		return 0;

	/**
	 *  分配
	 */
	sig = kmem_cache_zalloc(signal_cachep, GFP_KERNEL);
	tsk->signal = sig;
	if (!sig)
		return -ENOMEM;

	sig->nr_threads = 1;
	atomic_set(&sig->live, 1);
	refcount_set(&sig->sigcnt, 1);

	/* list_add(thread_node, thread_head) without INIT_LIST_HEAD() */
	sig->thread_head = (struct list_head)LIST_HEAD_INIT(tsk->thread_node);
	tsk->thread_node = (struct list_head)LIST_HEAD_INIT(sig->thread_head);

	/**
	 *
	 */
	init_waitqueue_head(&sig->wait_chldexit);
	sig->curr_target = tsk;
	init_sigpending(&sig->shared_pending);
	INIT_HLIST_HEAD(&sig->multiprocess);
	seqlock_init(&sig->stats_lock);
	prev_cputime_init(&sig->prev_cputime);

#ifdef CONFIG_POSIX_TIMERS
	INIT_LIST_HEAD(&sig->posix_timers);
	hrtimer_init(&sig->real_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sig->real_timer.function = it_real_fn;
#endif

	task_lock(current->group_leader);
	memcpy(sig->rlim, current->signal->rlim, sizeof sig->rlim);
	task_unlock(current->group_leader);

	posix_cpu_timers_init_group(sig);

	tty_audit_fork(sig);
	sched_autogroup_fork(sig);

	sig->oom_score_adj = current->signal->oom_score_adj;
	sig->oom_score_adj_min = current->signal->oom_score_adj_min;

	mutex_init(&sig->cred_guard_mutex);
	init_rwsem(&sig->exec_update_lock);

	return 0;
}

/**
 *
 */
static void copy_seccomp(struct task_struct *p)
{
#ifdef CONFIG_SECCOMP
	/*
	 * Must be called with sighand->lock held, which is common to
	 * all threads in the group. Holding cred_guard_mutex is not
	 * needed because this new task is not yet running and cannot
	 * be racing exec.
	 */
	assert_spin_locked(&current->sighand->siglock);

	/* Ref-count the new filter user, and assign it. */
	get_seccomp_filter(current);
	p->seccomp = current->seccomp;

	/*
	 * Explicitly enable no_new_privs here in case it got set
	 * between the task_struct being duplicated and holding the
	 * sighand lock. The seccomp state and nnp must be in sync.
	 */
	if (task_no_new_privs(current))
		task_set_no_new_privs(p);

	/*
	 * If the parent gained a seccomp mode after copying thread
	 * flags and between before we held the sighand lock, we have
	 * to manually enable the seccomp thread flag here.
	 */
	if (p->seccomp.mode != SECCOMP_MODE_DISABLED)
		set_tsk_thread_flag(p, TIF_SECCOMP);
#endif
}

SYSCALL_DEFINE1(set_tid_address, int __user *, tidptr)
{
	current->clear_child_tid = tidptr;

	return task_pid_vnr(current);
}

static void rt_mutex_init_task(struct task_struct *p)
{
	raw_spin_lock_init(&p->pi_lock);
#ifdef CONFIG_RT_MUTEXES
	p->pi_waiters = RB_ROOT_CACHED;
	p->pi_top_task = NULL;
	p->pi_blocked_on = NULL;
#endif
}


/**
 *  把新进程添加到 进程管理 的流程里
 */
static inline void init_task_pid_links(struct task_struct *task)
{
	enum pid_type type;

	for (type = PIDTYPE_PID; type < PIDTYPE_MAX; ++type) {
		INIT_HLIST_NODE(&task->pid_links[type]);
	}
}

/**
 * 初始化 task_struct 的 pid
 */
static inline void
init_task_pid(struct task_struct *task, enum pid_type type, struct pid *pid)
{
	if (type == PIDTYPE_PID)
		task->thread_pid = pid;
	else
		task->signal->pids[type] = pid;
}

static inline void rcu_copy_process(struct task_struct *p)  /* 初始化RCU 字段 */
{
#ifdef CONFIG_PREEMPT_RCU
	p->rcu_read_lock_nesting = 0;
	p->rcu_read_unlock_special.s = 0;
	p->rcu_blocked_node = NULL;
	INIT_LIST_HEAD(&p->rcu_node_entry);
#endif /* #ifdef CONFIG_PREEMPT_RCU */
#ifdef CONFIG_TASKS_RCU
	p->rcu_tasks_holdout = false;
	INIT_LIST_HEAD(&p->rcu_tasks_holdout_list);
	p->rcu_tasks_idle_cpu = -1;
#endif /* #ifdef CONFIG_TASKS_RCU */
#ifdef CONFIG_TASKS_TRACE_RCU
	p->trc_reader_nesting = 0;
	p->trc_reader_special.s = 0;
	INIT_LIST_HEAD(&p->trc_holdout_list);
#endif /* #ifdef CONFIG_TASKS_TRACE_RCU */
}

struct pid *pidfd_pid(const struct file *file)
{
	if (file->f_op == &pidfd_fops)
		return file->private_data;

	return ERR_PTR(-EBADF);
}

static int pidfd_release(struct inode *inode, struct file *file)
{
	struct pid *pid = file->private_data;

	file->private_data = NULL;
	put_pid(pid);
	return 0;
}

#ifdef CONFIG_PROC_FS
/**
 * pidfd_show_fdinfo - print information about a pidfd
 * @m: proc fdinfo file
 * @f: file referencing a pidfd
 *
 * Pid:
 * This function will print the pid that a given pidfd refers to in the
 * pid namespace of the procfs instance.
 * If the pid namespace of the process is not a descendant of the pid
 * namespace of the procfs instance 0 will be shown as its pid. This is
 * similar to calling getppid() on a process whose parent is outside of
 * its pid namespace.
 *
 * NSpid:
 * If pid namespaces are supported then this function will also print
 * the pid of a given pidfd refers to for all descendant pid namespaces
 * starting from the current pid namespace of the instance, i.e. the
 * Pid field and the first entry in the NSpid field will be identical.
 * If the pid namespace of the process is not a descendant of the pid
 * namespace of the procfs instance 0 will be shown as its first NSpid
 * entry and no others will be shown.
 * Note that this differs from the Pid and NSpid fields in
 * /proc/<pid>/status where Pid and NSpid are always shown relative to
 * the  pid namespace of the procfs instance. The difference becomes
 * obvious when sending around a pidfd between pid namespaces from a
 * different branch of the tree, i.e. where no ancestoral relation is
 * present between the pid namespaces:
 * - create two new pid namespaces ns1 and ns2 in the initial pid
 *   namespace (also take care to create new mount namespaces in the
 *   new pid namespace and mount procfs)
 * - create a process with a pidfd in ns1
 * - send pidfd from ns1 to ns2
 * - read /proc/self/fdinfo/<pidfd> and observe that both Pid and NSpid
 *   have exactly one entry, which is 0
 */
static void pidfd_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct pid *pid = f->private_data;
	struct pid_namespace *ns;
	pid_t nr = -1;

	if (likely(pid_has_task(pid, PIDTYPE_PID))) {
		ns = proc_pid_ns(file_inode(m->file)->i_sb);
		nr = pid_nr_ns(pid, ns);
	}

	seq_put_decimal_ll(m, "Pid:\t", nr);

#ifdef CONFIG_PID_NS
	seq_put_decimal_ll(m, "\nNSpid:\t", nr);
	if (nr > 0) {
		int i;

		/* If nr is non-zero it means that 'pid' is valid and that
		 * ns, i.e. the pid namespace associated with the procfs
		 * instance, is in the pid namespace hierarchy of pid.
		 * Start at one below the already printed level.
		 */
		for (i = ns->level + 1; i <= pid->level; i++)
			seq_put_decimal_ll(m, "\t", pid->numbers[i].nr);
	}
#endif
	seq_putc(m, '\n');
}
#endif

/*
 * Poll support for process exit notification.
 */
static __poll_t pidfd_poll(struct file *file, struct poll_table_struct *pts)
{
	struct pid *pid = file->private_data;
	__poll_t poll_flags = 0;

	poll_wait(file, &pid->wait_pidfd, pts);

	/*
	 * Inform pollers only when the whole thread group exits.
	 * If the thread group leader exits before all other threads in the
	 * group, then poll(2) should block, similar to the wait(2) family.
	 */
	if (thread_group_exited(pid))
		poll_flags = EPOLLIN | EPOLLRDNORM;

	return poll_flags;
}

/**
 *
 */
const struct file_operations pidfd_fops = {
	pidfd_fops.release = pidfd_release,
	pidfd_fops.poll = pidfd_poll,
#ifdef CONFIG_PROC_FS
	pidfd_fops.show_fdinfo = pidfd_show_fdinfo,
#endif
};

static void __delayed_free_task(struct rcu_head *rhp)
{
	struct task_struct *tsk = container_of(rhp, struct task_struct, rcu);

	free_task(tsk);
}

static __always_inline void delayed_free_task(struct task_struct *tsk)
{
	if (IS_ENABLED(CONFIG_MEMCG))
		call_rcu(&tsk->rcu, __delayed_free_task);
	else
		free_task(tsk);
}

static void copy_oom_score_adj(u64 clone_flags, struct task_struct *tsk)
{
	/* Skip if kernel thread */
	if (!tsk->mm)
		return;

	/* Skip if spawning a thread or using vfork */
	if ((clone_flags & (CLONE_VM | CLONE_THREAD | CLONE_VFORK)) != CLONE_VM)
		return;

	/* We need to synchronize with __set_oom_adj */
	mutex_lock(&oom_adj_mutex);
	set_bit(MMF_MULTIPROCESS, &tsk->mm->flags);
	/* Update the values in case they were changed after copy_signal */
	tsk->signal->oom_score_adj = current->signal->oom_score_adj;
	tsk->signal->oom_score_adj_min = current->signal->oom_score_adj_min;
	mutex_unlock(&oom_adj_mutex);
}

/*
 * This creates a new process as a copy of the old one,
 * but does not actually start it yet.
 *
 * It copies the registers, and all the appropriate
 * parts of the process environment (as per the clone
 * flags). The actual kick-off is left to the caller.
 *
 * 复制进程，并不运行
 */
static __latent_entropy struct task_struct *copy_process(struct pid *pid,
	        					int trace,
	        					int node,
	        					struct kernel_clone_args *args)
{
	int pidfd = -1, retval;
	struct task_struct *p;
	struct multiprocess_signals delayed;
	struct file *pidfile = NULL;
	u64 clone_flags = args->flags;
	struct nsproxy *nsp = current->nsproxy;

	/*
	 * Don't allow sharing the root directory with processes in a different
	 * namespace
	 * 如果只设置了 NEWNS 和 FS ，返回参数不可用
	 */
	if ((clone_flags & (CLONE_NEWNS|CLONE_FS)) == (CLONE_NEWNS|CLONE_FS))
		return ERR_PTR(-EINVAL);
	/* NEWUSER 和 FS */
	if ((clone_flags & (CLONE_NEWUSER|CLONE_FS)) == (CLONE_NEWUSER|CLONE_FS))
		return ERR_PTR(-EINVAL);

	/*
	 * Thread groups must share signals as well, and detached threads
	 * can only be started up within the thread group.
	 *
	 * 线程组共享 并且 信号处理 不共享，返回错误
	 */
	if ((clone_flags & CLONE_THREAD) && !(clone_flags & CLONE_SIGHAND))
		return ERR_PTR(-EINVAL);

	/*
	 * Shared signal handlers imply shared VM. By way of the above,
	 * thread groups also imply shared VM. Blocking this case allows
	 * for various simplifications in other code.
	 *
	 * 如果 信号处理 共享，但是 内存地址不共享，返回错误
	 */
	if ((clone_flags & CLONE_SIGHAND) && !(clone_flags & CLONE_VM))
		return ERR_PTR(-EINVAL);

	/*
	 * Siblings of global init remain as zombies on exit since they are
	 * not reaped by their parent (swapper). To solve this and to avoid
	 * multi-rooted process trees, prevent global and container-inits
	 * from creating siblings.
	 *
	 * 如果 和当前进程共享父进程，但是 当前进程 不可 kill，返回错误
	 */
	if ((clone_flags & CLONE_PARENT) && current->signal->flags & SIGNAL_UNKILLABLE)
		return ERR_PTR(-EINVAL);

	/*
	 * If the new process will be in a different pid or user namespace
	 * do not allow it to share a thread group with the forking task.
	 *
	 * 如果共享线程组，
	 *      1. 那就不应该有 NEWUSER 和 NEWPID
	 *      2. 同时 当前进程的pid_namespace 应该等于 namespace->pid_namespace
	 * 否则失败
	 */
	if (clone_flags & CLONE_THREAD) {
		if ((clone_flags & (CLONE_NEWUSER | CLONE_NEWPID)) ||
		    (task_active_pid_ns(current) != nsp->pid_ns_for_children))
			return ERR_PTR(-EINVAL);
	}

	/*
	 * If the new process will be in a different time namespace
	 * do not allow it to share VM or a thread group with the forking task.
	 *
	 * 如果共享线程组 或者 共享虚拟地址，那么
	 *      当前进程 的 time_namespace 应该等于 time_ns_for_children
	 */
	if (clone_flags & (CLONE_THREAD | CLONE_VM)) {
		if (nsp->time_ns != nsp->time_ns_for_children)
			return ERR_PTR(-EINVAL);
	}

	/* 如果克隆 FD  */
	if (clone_flags & CLONE_PIDFD) {
		/*
		 * - CLONE_DETACHED is blocked so that we can potentially
		 *   reuse it later for CLONE_PIDFD.
		 * - CLONE_THREAD is blocked until someone really needs it.
		 */
		if (clone_flags & (CLONE_DETACHED | CLONE_THREAD))
			return ERR_PTR(-EINVAL);
	}

	/*
	 * Force any signals received before this point to be delivered
	 * before the fork happens.  Collect up signals sent to multiple
	 * processes that happen during the fork and delay them so that
	 * they appear to happen after the fork.
	 */
	sigemptyset(&delayed.signal);
	INIT_HLIST_NODE(&delayed.node);

	spin_lock_irq(&current->sighand->siglock);/* 保护 task_struct->signal */
	if (!(clone_flags & CLONE_THREAD))  /* 如果不属于一个线程组，需要保存信号 */
		hlist_add_head(&delayed.node, &current->signal->multiprocess);

	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	retval = -ERESTARTNOINTR;
	/**
	 * 检测当前进程是否由挂起的信号，如果有，返回
	 */
	if (signal_pending(current))
		goto fork_out;

	retval = -ENOMEM;

	/**
	 *  拷贝分配一个 PCB
	 *  为新进程分配一个进程描述符 和 内核栈
	 */
	p = dup_task_struct(current, node);
	if (!p)
		goto fork_out;

	/*
	 * This _must_ happen before we call free_task(), i.e. before we jump
	 * to any of the bad_fork_* labels. This is to avoid freeing
	 * p->set_child_tid which is (ab)used as a kthread's data pointer for
	 * kernel threads (PF_KTHREAD).
	 */
	p->set_child_tid = (clone_flags & CLONE_CHILD_SETTID) ? args->child_tid : NULL;
	/*
	 * Clear TID on mm_release()?
	 */
	p->clear_child_tid = (clone_flags & CLONE_CHILD_CLEARTID) ? args->child_tid : NULL;

	/**
	 *
	 */
	ftrace_graph_init_task(p);  /* tracing */

	/**
	 *  互斥锁初始化
	 */
	rt_mutex_init_task(p);

	/* 死锁检测 */
	lockdep_assert_irqs_enabled();

#ifdef CONFIG_PROVE_LOCKING
	DEBUG_LOCKS_WARN_ON(!p->softirqs_enabled);
#endif

	retval = -EAGAIN;
	/**
	 * 用户进程数超限
	 */
	if (atomic_read(&p->real_cred->user->processes) >=
			task_rlimit(p, RLIMIT_NPROC)) {
		if (p->real_cred->user != INIT_USER /* 不是 init 进程 */&&
		    !capable(CAP_SYS_RESOURCE) && !capable(CAP_SYS_ADMIN))
			goto bad_fork_free;
	}

	/* 清理超出位 */
	current->flags &= ~PF_NPROC_EXCEEDED;

	/**
	 *  复制父进程证书
	 */
	retval = copy_creds(p, clone_flags);
	if (retval < 0)
		goto bad_fork_free;

	/*
	 * If multiple threads are within copy_process(), then this check
	 * triggers too late. This doesn't hurt, the check is only there
	 * to stop root fork bombs.
	 */
	retval = -EAGAIN;
	if (data_race(nr_threads >= max_threads))
		goto bad_fork_cleanup_count;

	/**
	 *
	 */
	delayacct_tsk_init(p);	/* Must remain after dup_task_struct() */

	/**
	 *  清理超级用户，工作队列worker，空闲线程标志位
	 */
	p->flags &= ~(PF_SUPERPRIV | PF_WQ_WORKER | PF_IDLE);
	/**
	 * 暂时还不能运行， fork but didn't exec
	 */
	p->flags |= PF_FORKNOEXEC;

	/**
	 *  初始化子进程和 兄弟进程链表
	 */
	INIT_LIST_HEAD(&p->children);
	INIT_LIST_HEAD(&p->sibling);
	rcu_copy_process(p);
	p->vfork_done = NULL;

	/* 初始化spinlock */
	spin_lock_init(&p->alloc_lock);

	/**
	 * 信号挂起链表初始化
	 */
	init_sigpending(&p->pending);

	/**
	 * 时间清零
	 */
	p->utime = p->stime = p->gtime = 0;

#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
	p->utimescaled = p->stimescaled = 0;
#endif

	/**
	 *  为空
	 */
	prev_cputime_init(&p->prev_cputime);

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
	seqcount_init(&p->vtime.seqcount);
	p->vtime.starttime = 0;
	p->vtime.state = VTIME_INACTIVE;
#endif

	/**
	 * @brief io_uring
	 *
	 */
#ifdef CONFIG_IO_URING
	p->io_uring = NULL;
#endif

#if defined(SPLIT_RSS_COUNTING)
	/**
	 *  统计信息清 0
	 */
	memset(&p->rss_stat, 0, sizeof(p->rss_stat));
#endif

	p->default_timer_slack_ns = current->timer_slack_ns;

#ifdef CONFIG_PSI
	p->psi_flags = 0;
#endif

	/**
	 *  IO统计信息清零
	 */
	task_io_accounting_init(&p->ioac);
	/* 清零 */
	acct_clear_integrals(p);

	/**
	 *  定时器初始化
	 */
	posix_cputimers_init(&p->posix_cputimers);

	p->io_context = NULL;
	audit_set_context(p, NULL);

	/**
	 *  cgroup 相关初始化
	 */
	cgroup_fork(p);

#ifdef CONFIG_NUMA
	/**
	 *  内存策略
	 */
	p->mempolicy = mpol_dup(p->mempolicy);
	if (IS_ERR(p->mempolicy)) {
		retval = PTR_ERR(p->mempolicy);
		p->mempolicy = NULL;
		goto bad_fork_cleanup_threadgroup_lock;
	}
#endif

#ifdef CONFIG_CPUSETS
	p->cpuset_mem_spread_rotor = NUMA_NO_NODE;
	p->cpuset_slab_spread_rotor = NUMA_NO_NODE;
	seqcount_spinlock_init(&p->mems_allowed_seq, &p->alloc_lock);
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
	/**
	 *
	 */
	memset(&p->irqtrace, 0, sizeof(p->irqtrace));
	p->irqtrace.hardirq_disable_ip	= _THIS_IP_;
	p->irqtrace.softirq_enable_ip	= _THIS_IP_;
	p->softirqs_enabled		= 1;
	p->softirq_context		= 0;
#endif

	p->pagefault_disabled = 0;

	/**
	 *
	 */
#ifdef CONFIG_LOCKDEP
	lockdep_init_task(p);
#endif

#ifdef CONFIG_DEBUG_MUTEXES
	p->blocked_on = NULL; /* not blocked yet */
#endif
#ifdef CONFIG_BCACHE
	p->sequential_io	= 0;
	p->sequential_io_avg	= 0;
#endif

	/**
	 *  Perform scheduler related setup. Assign this task to a CPU.
	 *  调度 - 初始化与进程调度相关的数据结构
	 */
	retval = sched_fork(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_policy;

	/**
	 *
	 */
	retval = perf_event_init_task(p);
	if (retval)
		goto bad_fork_cleanup_policy;

	/**
	 * 鉴权结构分配
	 */
	retval = audit_alloc(p);
	if (retval)
		goto bad_fork_cleanup_perf;

	/* copy all the process information */

	/**
	 * System V 共享内存
	 */
	shm_init_task(p);

	/**
	 * lsm 安全模块
	 */
	retval = security_task_alloc(p, clone_flags);
	if (retval)
		goto bad_fork_cleanup_audit;

	/**
	 *
	 */
	retval = copy_semundo(clone_flags, p);  /* undo */
	if (retval)
		goto bad_fork_cleanup_security;

	/**
	 *  文件：复制父进程打开的文件等信息
	 */
	retval = copy_files(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_semundo;

	/**
	 *  文件系统： 复制父进程的 fs_struct 数据结构信息
	 */
	retval = copy_fs(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_files;

	/**
	 *  信号处理 sigaction
	 */
	retval = copy_sighand(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_fs;

	/**
	 *  信号 - 复制父进程的信号系统
	 */
	retval = copy_signal(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_sighand;

	/**
	 *  拷贝 MM 结构
	 */
	retval = copy_mm(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_signal;

	/**
	 *  TODO 2021年7月21日16:22:18
	 *
	 * 复制父进程的命名空间
	 */
	retval = copy_namespaces(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_mm;

	/**
	 *  复制与 IO相关 的内容
	 */
	retval = copy_io(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_namespaces;

	/**
	 *  寄存器 - 复制父进程的内核堆信息
	 *
	 *  复制父进程的 pt_regs 结构到子进程，描述寄存器全部信息
	 */
	retval = copy_thread(clone_flags, args->stack, args->stack_size, p, args->tls);
	retval = copy_thread_tls(); /* +++ linux-5.0 */
	if (retval)
		goto bad_fork_cleanup_io;

	/**
	 *
	 */
	stackleak_task_init(p);

	/**
	 * 不是系统第一个静态 pid
	 * 分配一个 PID 结构
	 */
	if (pid != &init_struct_pid) {

		/**
		 * 为进程分配 PID 结构和 pid
		 */
		pid = alloc_pid(p->nsproxy->pid_ns_for_children, args->set_tid, args->set_tid_size);
		if (IS_ERR(pid)) {
			retval = PTR_ERR(pid);
			goto bad_fork_cleanup_thread;
		}
	}

	/*
	 * This has to happen after we've potentially unshared the file
	 * descriptor table (so that the pidfd doesn't leak into the child
	 * if the fd table isn't shared).
	 *
	 * pthread_create -> clone() 中没有 CLONE_PIDFD
	 */
	if (clone_flags & CLONE_PIDFD) {    /* FD */
		retval = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
		if (retval < 0)
			goto bad_fork_free_pid;

		pidfd = retval;

		pidfile = anon_inode_getfile("[pidfd]", &pidfd_fops, pid, O_RDWR | O_CLOEXEC);
		if (IS_ERR(pidfile)) {
			put_unused_fd(pidfd);
			retval = PTR_ERR(pidfile);
			goto bad_fork_free_pid;
		}
		get_pid(pid);	/* held by pidfile now */

		retval = put_user(pidfd, args->pidfd);
		if (retval)
			goto bad_fork_put_pidfd;
	}

#ifdef CONFIG_BLOCK
	p->plug = NULL;
#endif

	/**
	 *
	 */
	futex_init_task(p);

	/*
	 * sigaltstack should be cleared when sharing the same VM
	 */
	if ((clone_flags & (CLONE_VM|CLONE_VFORK)) == CLONE_VM)
		sas_ss_reset(p);

	/*
	 * Syscall tracing and stepping should be turned off in the
	 * child regardless of CLONE_PTRACE.
	 */
	user_disable_single_step(p);
	clear_tsk_thread_flag(p, TIF_SYSCALL_TRACE);
#ifdef TIF_SYSCALL_EMU
	clear_tsk_thread_flag(p, TIF_SYSCALL_EMU);
#endif
	clear_tsk_latency_tracing(p);

	/* ok, now we should be set up.. */
	/**
	 * 设置 group leader 和 TGID
	 *
	 * p->pid/p->tgid 是 pid_t 类型
	 */
	p->pid = pid_nr(pid);
	/**
	 *  子进程归属于父进程线程组
	 */
	if (clone_flags & CLONE_THREAD) {
		p->group_leader = current->group_leader;
		p->tgid = current->tgid;
	}
	/**
	 *  子进程是线程组的领头线程
	 */
	else {
		p->group_leader = p;
		p->tgid = p->pid;
	}

	p->nr_dirtied = 0;
	p->nr_dirtied_pause = 128 >> (PAGE_SHIFT - 10);
	p->dirty_paused_when = 0;

	p->pdeath_signal = 0;
	INIT_LIST_HEAD(&p->thread_group);
	p->task_works = NULL;

#ifdef CONFIG_RETHOOK
	p->rethooks.first = NULL;
#endif

	/*
	 * Ensure that the cgroup subsystem policies allow the new process to be
	 * forked. It should be noted that the new process's css_set can be changed
	 * between here and cgroup_post_fork() if an organisation operation is in
	 * progress.
	 */
	retval = cgroup_can_fork(p, args);
	if (retval)
		goto bad_fork_put_pidfd;

	/*
	 * From this point on we must avoid any synchronous user-space
	 * communication until we take the tasklist-lock. In particular, we do
	 * not want user-space to be able to predict the process start-time by
	 * stalling fork(2) after we recorded the start_time but before it is
	 * visible to the system.
	 */

	p->start_time = ktime_get_ns();
	p->start_boottime = ktime_get_boottime_ns();

	/*
	 * Make it visible to the rest of the system, but dont wake it up yet.
	 * Need tasklist lock for parent etc handling!
	 */
	write_lock_irq(&tasklist_lock);

	/* CLONE_PARENT re-uses the old parent */
	if (clone_flags & (CLONE_PARENT|CLONE_THREAD)) {
		p->real_parent = current->real_parent;
		p->parent_exec_id = current->parent_exec_id;
		if (clone_flags & CLONE_THREAD)
			p->exit_signal = -1;
		else
			p->exit_signal = current->group_leader->exit_signal;
	} else {
		p->real_parent = current;
		p->parent_exec_id = current->self_exec_id;
		p->exit_signal = args->exit_signal;
	}

	klp_copy_process(p);

	spin_lock(&current->sighand->siglock);

	/*
	 * Copy seccomp details explicitly here, in case they were changed
	 * before holding sighand lock.
	 *
	 * 限制 系统调用
	 */
	copy_seccomp(p);

	/**
	 *
	 */
	rseq_fork(p, clone_flags);

	/**
	 * Don't start children in a dying pid namespace
	 *
	 * pid_allocated 初始化将被赋值为 PIDNS_ADDING ， 这是能满足的
	 */
	if (unlikely(!(ns_of_pid(pid)->pid_allocated & PIDNS_ADDING))) {
		retval = -ENOMEM;
		goto bad_fork_cancel_cgroup;
	}

	/* Let kill terminate clone/fork in the middle */
	if (fatal_signal_pending(current)) {
		retval = -EINTR;
		goto bad_fork_cancel_cgroup;
	}

	/**
	 *  past the last point of failure
	 *
	 *  如果 (clone_flags & CLONE_PIDFD) pidfile 将不为空
	 */
	if (pidfile)
		fd_install(pidfd, pidfile);

	/**
	 *  把新进程添加到 进程管理 的流程里
	 */
	init_task_pid_links(p);

	/**
	 *
	 */
	if (likely(p->pid)) {
		ptrace_init_task(p, (clone_flags & CLONE_PTRACE) || trace);

		init_task_pid(p, PIDTYPE_PID, pid);

		/**
		 *  领头进程
		 */
		if (thread_group_leader(p)) {

			/**
			 *  设置 几个 ID
			 */
			init_task_pid(p, PIDTYPE_TGID, pid);
			init_task_pid(p, PIDTYPE_PGID, task_pgrp(current));
			init_task_pid(p, PIDTYPE_SID, task_session(current));

			if (is_child_reaper(pid)) {
				ns_of_pid(pid)->child_reaper = p;
				p->signal->flags |= SIGNAL_UNKILLABLE;
			}
			p->signal->shared_pending.signal = delayed.signal;
			p->signal->tty = tty_kref_get(current->signal->tty);
			/*
			 * Inherit has_child_subreaper flag under the same
			 * tasklist_lock with adding child to the process tree
			 * for propagate_has_child_subreaper optimization.
			 */
			p->signal->has_child_subreaper = p->real_parent->signal->has_child_subreaper ||
							 p->real_parent->signal->is_child_subreaper;
			list_add_tail(&p->sibling, &p->real_parent->children);
			list_add_tail_rcu(&p->tasks, &init_task.tasks);

			/**
			 * 把新进程添加到不同的 哈希表 中
			 */
			attach_pid(p, PIDTYPE_TGID);
			attach_pid(p, PIDTYPE_PGID);
			attach_pid(p, PIDTYPE_SID);

			/**
			 *  递增
			 */
			__this_cpu_inc(process_counts);

		}
	    /**
	     *  不是领头进程
	     */
	    else {
			current->signal->nr_threads++;

			/**
			 *
			 */
			atomic_inc(&current->signal->live);
			refcount_inc(&current->signal->sigcnt);
			task_join_group_stop(p);

			/**
			 *
			 */
			list_add_tail_rcu(&p->thread_group, &p->group_leader->thread_group);
			list_add_tail_rcu(&p->thread_node, &p->signal->thread_head);
		}
		attach_pid(p, PIDTYPE_PID);
		nr_threads++;
	}

	/**
	 *
	 */
	total_forks++;

	/**
	 *
	 */
	hlist_del_init(&delayed.node);

	/**
	 *
	 */
	spin_unlock(&current->sighand->siglock);

	/**
	 *
	 */
	syscall_tracepoint_update(p);
	write_unlock_irq(&tasklist_lock);

	/**
	 *
	 */
	proc_fork_connector(p);

	/**
	 *
	 */
	sched_post_fork(p);

	/**
	 * @brief kernel/cgroup/cgroup.c
	 *
	 * 1. cgroup_fork()
	 * 2. cgroup_can_fork()
	 * 3. cgroup_post_fork()
	 */
	cgroup_post_fork(p, args);

	/**
	 *
	 */
	perf_event_fork(p);

	trace_task_newtask(p, clone_flags);

	uprobe_copy_process(p, clone_flags);

	copy_oom_score_adj(clone_flags, p);

	return p;

bad_fork_cancel_cgroup:
	spin_unlock(&current->sighand->siglock);
	write_unlock_irq(&tasklist_lock);
	cgroup_cancel_fork(p, args);
bad_fork_put_pidfd:
	if (clone_flags & CLONE_PIDFD) {
		fput(pidfile);
		put_unused_fd(pidfd);
	}
bad_fork_free_pid:
	if (pid != &init_struct_pid)
		free_pid(pid);
bad_fork_cleanup_thread:
	exit_thread(p);
bad_fork_cleanup_io:
	if (p->io_context)
		exit_io_context(p);
bad_fork_cleanup_namespaces:
	exit_task_namespaces(p);
bad_fork_cleanup_mm:
	if (p->mm) {
		mm_clear_owner(p->mm, p);
		mmput(p->mm);
	}
bad_fork_cleanup_signal:
	if (!(clone_flags & CLONE_THREAD))
		free_signal_struct(p->signal);
bad_fork_cleanup_sighand:
	__cleanup_sighand(p->sighand);
bad_fork_cleanup_fs:
	exit_fs(p); /* blocking */
bad_fork_cleanup_files:
	exit_files(p); /* blocking */
bad_fork_cleanup_semundo:
	exit_sem(p);
bad_fork_cleanup_security:
	security_task_free(p);
bad_fork_cleanup_audit:
	audit_free(p);
bad_fork_cleanup_perf:
	perf_event_free_task(p);
bad_fork_cleanup_policy:
	lockdep_free_task(p);
#ifdef CONFIG_NUMA
	mpol_put(p->mempolicy);
bad_fork_cleanup_threadgroup_lock:
#endif
	delayacct_tsk_free(p);
bad_fork_cleanup_count:
	atomic_dec(&p->cred->user->processes);
	exit_creds(p);
bad_fork_free:
	p->state = TASK_DEAD;
	put_task_stack(p);
	delayed_free_task(p);
fork_out:
	spin_lock_irq(&current->sighand->siglock);
	hlist_del_init(&delayed.node);
	spin_unlock_irq(&current->sighand->siglock);
	return ERR_PTR(retval);
}

static inline void init_idle_pids(struct task_struct *idle)
{
	enum pid_type type;

	for (type = PIDTYPE_PID; type < PIDTYPE_MAX; ++type) {
		INIT_HLIST_NODE(&idle->pid_links[type]); /* not really needed */
		init_task_pid(idle, type, &init_struct_pid);
	}
}

struct task_struct *fork_idle(int cpu)
{
	struct task_struct *task;
	struct kernel_clone_args args = {
		.flags = CLONE_VM,
	};

	task = copy_process(&init_struct_pid, 0, cpu_to_node(cpu), &args);
	if (!IS_ERR(task)) {
		init_idle_pids(task);
		init_idle(task, cpu);
	}

	return task;
}

struct mm_struct *copy_init_mm(void)
{
	return dup_mm(NULL, &init_mm);
}

/*
 *  Ok, this is the main fork-routine.
 *
 * It copies the process, and if successful kick-starts
 * it and waits for it to finish using the VM if required.
 *
 * args->exit_signal is expected to be checked for sanity by the caller.
 */
pid_t do_fork();    /* 老版本内核为 linux-5.0 */
pid_t _do_fork();   /* 老版本内核为 linux-5.0 */
pid_t kernel_clone(struct kernel_clone_args *args)
{
	u64 clone_flags = args->flags;

	/**
	 *  vfork 父进程等待子进程结束
	 */
	struct completion vfork;
	struct pid *pid;
	struct task_struct *p;
	int trace = 0;
	pid_t nr;

	/*
	 * For legacy clone() calls, CLONE_PIDFD uses the parent_tid argument
	 * to return the pidfd. Hence, CLONE_PIDFD and CLONE_PARENT_SETTID are
	 * mutually exclusive. With clone3() CLONE_PIDFD has grown a separate
	 * field in struct clone_args and it still doesn't make sense to have
	 * them both point at the same memory location. Performing this check
	 * here has the advantage that we don't need to have a separate helper
	 * to check for legacy clone().
	 */
	if ((args->flags & CLONE_PIDFD) &&
	    (args->flags & CLONE_PARENT_SETTID) &&
	    (args->pidfd == args->parent_tid))  /* 检测 */
		return -EINVAL;

	/*
	 * Determine whether and which event to report to ptracer.  When
	 * called from kernel_thread or CLONE_UNTRACED is explicitly
	 * requested, no event is reported; otherwise, report if the event
	 * for the type of forking is enabled.
	 */
	if (!(clone_flags & CLONE_UNTRACED)) {  /* 如果不可追踪 */
		/* 如果是 vfork() */
		if (clone_flags & CLONE_VFORK)
			trace = PTRACE_EVENT_VFORK;
		/* 如果推出signal 不是 SIGCHLD */
		else if (args->exit_signal != SIGCHLD)
			trace = PTRACE_EVENT_CLONE; /* clone() */
		else
			trace = PTRACE_EVENT_FORK;  /* fork() */
		/* 检查标志位，如果没有设置 ptrace 标志位，trace=0*/
		/* 追踪 */
		if (likely(!ptrace_event_enabled(current, trace)))
			trace = 0;  /* 不可追踪 */
	}
	/**
	 * 复制进程，并不运行
	 * trace: 追踪状态
	 */
	p = copy_process(NULL, trace, NUMA_NO_NODE, args);

	/**
	 *
	 */
	add_latent_entropy();

	if (IS_ERR(p))
		return PTR_ERR(p);

	/*
	 * Do this prior waking up the new thread - the thread pointer
	 * might get invalid after that point, if the thread exits quickly.
	 *
	 * tracepoint:sched:sched_process_fork { ... }
	 *
	 * see also bpftrace/tools/pidpersec.bt
	 */
	trace_sched_process_fork(current, p);

	/**
	 * 获取 PID
	 */
	pid = get_task_pid(p, PIDTYPE_PID);
	nr = pid_vnr(pid);

	/**
	 *
	 */
	if (clone_flags & CLONE_PARENT_SETTID)
		put_user(nr, args->parent_tid);

	/**
	 *  vfork() 需要 初始化 ，这在 wait(2) 中是对应的
	 */
	if (clone_flags & CLONE_VFORK) {
		p->vfork_done = &vfork;
		/* VFORK 机制 使用completion完成 */
		init_completion(&vfork);
		get_task_struct(p);
	}

	/**
	 *  唤醒 新进程 ，将进程 添加到运行队列
	 *  这里唤醒新的 task 这就是为什么 fork 返回两次
	 */
	wake_up_new_task(p);

	/* forking complete and child started to run, tell ptracer */
	if (unlikely(trace))
		ptrace_event_pid(trace, pid);

	/* vfork() */
	if (clone_flags & CLONE_VFORK) {
		if (!wait_for_vfork_done(p, &vfork))
			ptrace_event_pid(PTRACE_EVENT_VFORK_DONE, pid);
	}

	put_pid(pid);
	return nr;
}

/*
 * Create a kernel thread.
 */
pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct kernel_clone_args args = {
		args.flags = ((lower_32_bits(flags) | CLONE_VM | CLONE_UNTRACED) & ~CSIGNAL),
		args.exit_signal	= (lower_32_bits(flags) & CSIGNAL),
		args.stack		= (unsigned long)fn,
		args.stack_size	= (unsigned long)arg,
	};

	return kernel_clone(&args);
}

#ifdef __ARCH_WANT_SYS_FORK
/**
 *  fork(2)
 *  fork()系统调用
 */
pid_t fork(void){} /* +++ */
SYSCALL_DEFINE0(fork)
{
#ifdef CONFIG_MMU
	struct kernel_clone_args args = {
		args.exit_signal = SIGCHLD,
	};

	return kernel_clone(&args);
#else
	/* can not support in nommu mode */
	return -EINVAL;
#endif
}
#endif

#ifdef __ARCH_WANT_SYS_VFORK
/**
 *  vfork(2)
 *  vfork 和父进程共享地址空间
 */
pid_t vfork(void);
SYSCALL_DEFINE0(vfork)
{
	struct kernel_clone_args args = {
		/**
		 * CLONE_VFORK | CLONE_VM
		 */
		args.flags		= CLONE_VFORK | CLONE_VM,
		args.exit_signal	= SIGCHLD,
	};

	return kernel_clone(&args);
}
#endif


#ifdef __ARCH_WANT_SYS_CLONE
#ifdef CONFIG_CLONE_BACKWARDS
#elif defined(CONFIG_CLONE_BACKWARDS2)
#elif defined(CONFIG_CLONE_BACKWARDS3)
#else
/**
 *  克隆 clone(2)
 *
 *  pthread_create 为
 *  clone(child_stack=0x7f2c7ec71fb0,
 *          flags=CLONE_VM|
 *                CLONE_FS|
 *                CLONE_FILES|
 *                CLONE_SIGHAND|
 *                CLONE_THREAD|
 *                CLONE_SYSVSEM|
 *                CLONE_SETTLS|
 *                CLONE_PARENT_SETTID|
 *                CLONE_CHILD_CLEARTID,
 *      parent_tidptr=0x7f2c7ec729d0, tls=0x7f2c7ec72700, child_tidptr=0x7f2c7ec729d0)
 */
long clone(unsigned long flags, void *child_stack,
	             void *ptid, void *ctid,
	             struct pt_regs *regs){}//+++
SYSCALL_DEFINE5(clone, unsigned long, clone_flags, unsigned long, newsp,
	            		 int __user *, parent_tidptr,
	            		 int __user *, child_tidptr,
	            		 unsigned long, tls)
#endif
{
	struct kernel_clone_args args = {
		args.flags		= (lower_32_bits(clone_flags) & ~CSIGNAL),
		args.pidfd		= parent_tidptr,
		args.child_tid	= child_tidptr,
		args.parent_tid	= parent_tidptr,
		args.exit_signal	= (lower_32_bits(clone_flags) & CSIGNAL),
		args.stack		= newsp,
		args.tls		= tls,
	};

	return kernel_clone(&args);
}
#endif

#ifdef __ARCH_WANT_SYS_CLONE3

noinline static int copy_clone_args_from_user(struct kernel_clone_args *kargs,
					      struct clone_args __user *uargs,
					      size_t usize)
{
	int err;
	struct clone_args args;
	pid_t *kset_tid = kargs->set_tid;

	BUILD_BUG_ON(offsetofend(struct clone_args, tls) !=
		     CLONE_ARGS_SIZE_VER0);
	BUILD_BUG_ON(offsetofend(struct clone_args, set_tid_size) !=
		     CLONE_ARGS_SIZE_VER1);
	BUILD_BUG_ON(offsetofend(struct clone_args, cgroup) !=
		     CLONE_ARGS_SIZE_VER2);
	BUILD_BUG_ON(sizeof(struct clone_args) != CLONE_ARGS_SIZE_VER2);

	if (unlikely(usize > PAGE_SIZE))
		return -E2BIG;
	if (unlikely(usize < CLONE_ARGS_SIZE_VER0))
		return -EINVAL;

	err = copy_struct_from_user(&args, sizeof(args), uargs, usize);
	if (err)
		return err;

	if (unlikely(args.set_tid_size > MAX_PID_NS_LEVEL))
		return -EINVAL;

	if (unlikely(!args.set_tid && args.set_tid_size > 0))
		return -EINVAL;

	if (unlikely(args.set_tid && args.set_tid_size == 0))
		return -EINVAL;

	/*
	 * Verify that higher 32bits of exit_signal are unset and that
	 * it is a valid signal
	 */
	if (unlikely((args.exit_signal & ~((u64)CSIGNAL)) ||
		     !valid_signal(args.exit_signal)))
		return -EINVAL;

	if ((args.flags & CLONE_INTO_CGROUP) &&
	    (args.cgroup > INT_MAX || usize < CLONE_ARGS_SIZE_VER2))
		return -EINVAL;

	*kargs = (struct kernel_clone_args){
		kargs.flags		= args.flags,
		kargs.pidfd		= u64_to_user_ptr(args.pidfd),
		kargs.child_tid	= u64_to_user_ptr(args.child_tid),
		kargs.parent_tid	= u64_to_user_ptr(args.parent_tid),
		kargs.exit_signal	= args.exit_signal,
		kargs.stack		= args.stack,
		kargs.stack_size	= args.stack_size,
		kargs.tls		= args.tls,
		kargs.set_tid_size	= args.set_tid_size,
		kargs.cgroup		= args.cgroup,
	};

	if (args.set_tid &&
		copy_from_user(kset_tid, u64_to_user_ptr(args.set_tid),
			(kargs->set_tid_size * sizeof(pid_t))))
		return -EFAULT;

	kargs->set_tid = kset_tid;

	return 0;
}

/**
 * clone3_stack_valid - check and prepare stack
 * @kargs: kernel clone args
 *
 * Verify that the stack arguments userspace gave us are sane.
 * In addition, set the stack direction for userspace since it's easy for us to
 * determine.
 */
static inline bool clone3_stack_valid(struct kernel_clone_args *kargs)
{
	if (kargs->stack == 0) {
		if (kargs->stack_size > 0)
			return false;
	} else {
		if (kargs->stack_size == 0)
			return false;

		if (!access_ok((void __user *)kargs->stack, kargs->stack_size))
			return false;

#if !defined(CONFIG_STACK_GROWSUP) && !defined(CONFIG_IA64)
		kargs->stack += kargs->stack_size;
#endif
	}

	return true;
}

static bool clone3_args_valid(struct kernel_clone_args *kargs)
{
	/* Verify that no unknown flags are passed along. */
	if (kargs->flags &
	    ~(CLONE_LEGACY_FLAGS | CLONE_CLEAR_SIGHAND | CLONE_INTO_CGROUP))
		return false;

	/*
	 * - make the CLONE_DETACHED bit reuseable for clone3
	 * - make the CSIGNAL bits reuseable for clone3
	 */
	if (kargs->flags & (CLONE_DETACHED | CSIGNAL))
		return false;

	if ((kargs->flags & (CLONE_SIGHAND | CLONE_CLEAR_SIGHAND)) ==
	    (CLONE_SIGHAND | CLONE_CLEAR_SIGHAND))
		return false;

	if ((kargs->flags & (CLONE_THREAD | CLONE_PARENT)) &&
	    kargs->exit_signal)
		return false;

	if (!clone3_stack_valid(kargs))
		return false;

	return true;
}

/**
 * clone3 - create a new process with specific properties
 * @uargs: argument structure
 * @size:  size of @uargs
 *
 * clone3() is the extensible successor to clone()/clone2().
 * It takes a struct as argument that is versioned by its size.
 *
 * Return: On success, a positive PID for the child process.
 *         On error, a negative errno number.
 */
SYSCALL_DEFINE2(clone3, struct clone_args __user *, uargs, size_t, size)
{
	int err;

	struct kernel_clone_args kargs;
	pid_t set_tid[MAX_PID_NS_LEVEL];

	kargs.set_tid = set_tid;

	err = copy_clone_args_from_user(&kargs, uargs, size);
	if (err)
		return err;

	if (!clone3_args_valid(&kargs))
		return -EINVAL;

	return kernel_clone(&kargs);
}
#endif

void walk_process_tree(struct task_struct *top, proc_visitor visitor, void *data)
{
	struct task_struct *leader, *parent, *child;
	int res;

	read_lock(&tasklist_lock);
	leader = top = top->group_leader;
down:
	for_each_thread(leader, parent) {
		list_for_each_entry(child, &parent->children, sibling) {
			res = visitor(child, data);
			if (res) {
				if (res < 0)
					goto out;
				leader = child;
				goto down;
			}
up:
			;
		}
	}

	if (leader != top) {
		child = leader;
		parent = child->real_parent;
		leader = parent->group_leader;
		goto up;
	}
out:
	read_unlock(&tasklist_lock);
}

#ifndef ARCH_MIN_MMSTRUCT_ALIGN
#define ARCH_MIN_MMSTRUCT_ALIGN 0
#endif

static void sighand_ctor(void *data)
{
	struct sighand_struct *sighand = data;

	spin_lock_init(&sighand->siglock);
	init_waitqueue_head(&sighand->signalfd_wqh);
}

void __init proc_caches_init(void)  /* /proc/slabinfo 中可查到的  */
{
	unsigned int mm_size;

	sighand_cachep = kmem_cache_create("sighand_cache",
			sizeof(struct sighand_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_TYPESAFE_BY_RCU|
			SLAB_ACCOUNT, sighand_ctor);
	signal_cachep = kmem_cache_create("signal_cache",
			sizeof(struct signal_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			NULL);
	files_cachep = kmem_cache_create("files_cache",
			sizeof(struct files_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			NULL);
	fs_cachep = kmem_cache_create("fs_cache",
			sizeof(struct fs_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			NULL);

	/*
	 * The mm_cpumask is located at the end of mm_struct, and is
	 * dynamically sized based on the maximum CPU number this system
	 * can have, taking hotplug into account (nr_cpu_ids).
	 */
	mm_size = sizeof(struct mm_struct) + cpumask_size();

	mm_cachep = kmem_cache_create_usercopy("mm_struct",
			mm_size, ARCH_MIN_MMSTRUCT_ALIGN,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			offsetof(struct mm_struct, saved_auxv),
			sizeof_field(struct mm_struct, saved_auxv),
			NULL);
	vm_area_cachep = KMEM_CACHE(vm_area_struct, SLAB_PANIC|SLAB_ACCOUNT);

	//
	mmap_init();    /* 初始化percpu计数器 for VM 和 region 记录  slabs */

	//initializes `SLAB` for namespaces
	nsproxy_cache_init();   /* namesapce proxy 缓存分配 */
}

/*
 * Check constraints on flags passed to the unshare system call.
 */
static int check_unshare_flags(unsigned long unshare_flags)
{
	if (unshare_flags & ~(CLONE_THREAD|CLONE_FS|CLONE_NEWNS|CLONE_SIGHAND|
				CLONE_VM|CLONE_FILES|CLONE_SYSVSEM|
				CLONE_NEWUTS|CLONE_NEWIPC|CLONE_NEWNET|
				CLONE_NEWUSER|CLONE_NEWPID|CLONE_NEWCGROUP|
				CLONE_NEWTIME))
		return -EINVAL;
	/*
	 * Not implemented, but pretend it works if there is nothing
	 * to unshare.  Note that unsharing the address space or the
	 * signal handlers also need to unshare the signal queues (aka
	 * CLONE_THREAD).
	 */
	if (unshare_flags & (CLONE_THREAD | CLONE_SIGHAND | CLONE_VM)) {
		if (!thread_group_empty(current))
			return -EINVAL;
	}
	if (unshare_flags & (CLONE_SIGHAND | CLONE_VM)) {
		if (refcount_read(&current->sighand->count) > 1)
			return -EINVAL;
	}
	if (unshare_flags & CLONE_VM) {
		if (!current_is_single_threaded())
			return -EINVAL;
	}

	return 0;
}

/*
 * Unshare the filesystem structure if it is being shared
 */
static int unshare_fs(unsigned long unshare_flags, struct fs_struct **new_fsp)
{
	struct fs_struct *fs = current->fs;

	if (!(unshare_flags & CLONE_FS) || !fs)
		return 0;

	/* don't need lock here; in the worst case we'll do useless copy */
	if (fs->users == 1)
		return 0;

	*new_fsp = copy_fs_struct(fs);
	if (!*new_fsp)
		return -ENOMEM;

	return 0;
}

/*
 * Unshare file descriptor table if it is being shared
 */
int unshare_fd(unsigned long unshare_flags, unsigned int max_fds,
	       struct files_struct **new_fdp)
{
	struct files_struct *fd = current->files;
	int error = 0;

	if ((unshare_flags & CLONE_FILES) &&
	    (fd && atomic_read(&fd->count) > 1)) {
		*new_fdp = dup_fd(fd, max_fds, &error);
		if (!*new_fdp)
			return error;
	}

	return 0;
}

/*
 * unshare allows a process to 'unshare' part of the process
 * context which was originally shared using clone.  copy_*
 * functions used by kernel_clone() cannot be used here directly
 * because they modify an inactive task_struct that is being
 * constructed. Here we are modifying the current, active,
 * task_struct.
 *
 *  unshare(2)
 *  将当前进程和所在的Namespace分离，并加入到一个新的Namespace中
 *  相对于setns()系统调用来说，unshare()不用关联之前存在的Namespace，
 *  只需要指定需要分离的Namespace就行，该调用会自动创建一个新的Namespace
 */
int ksys_unshare(unsigned long unshare_flags)
{
	struct fs_struct *fs, *new_fs = NULL;
	struct files_struct *fd, *new_fd = NULL;
	struct cred *new_cred = NULL;
	struct nsproxy *new_nsproxy = NULL;
	int do_sysvsem = 0;
	int err;

	/*
	 * If unsharing a user namespace must also unshare the thread group
	 * and unshare the filesystem root and working directories.
	 */
	if (unshare_flags & CLONE_NEWUSER)
		unshare_flags |= CLONE_THREAD | CLONE_FS;
	/*
	 * If unsharing vm, must also unshare signal handlers.
	 */
	if (unshare_flags & CLONE_VM)
		unshare_flags |= CLONE_SIGHAND;
	/*
	 * If unsharing a signal handlers, must also unshare the signal queues.
	 */
	if (unshare_flags & CLONE_SIGHAND)
		unshare_flags |= CLONE_THREAD;
	/*
	 * If unsharing namespace, must also unshare filesystem information.
	 */
	if (unshare_flags & CLONE_NEWNS)
		unshare_flags |= CLONE_FS;

	/**
	 *
	 */
	err = check_unshare_flags(unshare_flags);
	if (err)
		goto bad_unshare_out;
	/*
	 * CLONE_NEWIPC must also detach from the undolist: after switching
	 * to a new ipc namespace, the semaphore arrays from the old
	 * namespace are unreachable.
	 */
	if (unshare_flags & (CLONE_NEWIPC|CLONE_SYSVSEM))
		do_sysvsem = 1;

	/**
	 *
	 */
	err = unshare_fs(unshare_flags, &new_fs);
	if (err)
		goto bad_unshare_out;
	/**
	 *
	 */
	err = unshare_fd(unshare_flags, NR_OPEN_MAX, &new_fd);
	if (err)
		goto bad_unshare_cleanup_fs;
	/**
	 *
	 */
	err = unshare_userns(unshare_flags, &new_cred);
	if (err)
		goto bad_unshare_cleanup_fd;
	/**
	 *
	 */
	err = unshare_nsproxy_namespaces(unshare_flags, &new_nsproxy,
					 new_cred, new_fs);
	if (err)
		goto bad_unshare_cleanup_cred;

	if (new_fs || new_fd || do_sysvsem || new_cred || new_nsproxy) {
		if (do_sysvsem) {
			/*
			 * CLONE_SYSVSEM is equivalent to sys_exit().
			 */
			exit_sem(current);
		}
		if (unshare_flags & CLONE_NEWIPC) {
			/* Orphan segments in old ns (see sem above). */
			exit_shm(current);
			shm_init_task(current);
		}

		if (new_nsproxy)
			switch_task_namespaces(current, new_nsproxy);

		task_lock(current);

		if (new_fs) {
			fs = current->fs;
			spin_lock(&fs->lock);
			current->fs = new_fs;
			if (--fs->users)
				new_fs = NULL;
			else
				new_fs = fs;
			spin_unlock(&fs->lock);
		}

		if (new_fd) {
			fd = current->files;
			current->files = new_fd;
			new_fd = fd;
		}

		task_unlock(current);

		if (new_cred) {
			/* Install the new user namespace */
			commit_creds(new_cred);
			new_cred = NULL;
		}
	}

	perf_event_namespaces(current);

bad_unshare_cleanup_cred:
	if (new_cred)
		put_cred(new_cred);
bad_unshare_cleanup_fd:
	if (new_fd)
		put_files_struct(new_fd);

bad_unshare_cleanup_fs:
	if (new_fs)
		free_fs_struct(new_fs);

bad_unshare_out:
	return err;
}

/**
 *  unshare(2)
 *  将当前进程和所在的Namespace分离，并加入到一个新的Namespace中
 *  相对于setns()系统调用来说，unshare()不用关联之前存在的Namespace，
 *  只需要指定需要分离的Namespace就行，该调用会自动创建一个新的Namespace
 */
int unshare(int unshare_flags){}//+++++
SYSCALL_DEFINE1(unshare, unsigned long, unshare_flags)
{
	return ksys_unshare(unshare_flags);
}

/*
 *	Helper to unshare the files of the current task.
 *	We don't want to expose copy_files internals to
 *	the exec layer of the kernel.
 */

int unshare_files(struct files_struct **displaced)
{
	struct task_struct *task = current;
	struct files_struct *copy = NULL;
	int error;

	error = unshare_fd(CLONE_FILES, NR_OPEN_MAX, &copy);
	if (error || !copy) {
		*displaced = NULL;
		return error;
	}
	*displaced = task->files;
	task_lock(task);
	task->files = copy;
	task_unlock(task);
	return 0;
}

int sysctl_max_threads(struct ctl_table *table, int write,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table t;
	int ret;
	int threads = max_threads;
	int min = 1;
	int max = MAX_THREADS;

	t = *table;
	t.data = &threads;
	t.extra1 = &min;
	t.extra2 = &max;

	ret = proc_dointvec_minmax(&t, write, buffer, lenp, ppos);
	if (ret || !write)
		return ret;

	max_threads = threads;

	return 0;
}
