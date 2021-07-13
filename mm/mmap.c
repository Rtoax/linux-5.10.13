// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/mmap.c
 *
 * Written by obz.
 *
 * Address space accounting code	<alan@lxorguk.ukuu.org.uk>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/mm.h>
#include <linux/vmacache.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/capability.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/hugetlb.h>
#include <linux/shmem_fs.h>
#include <linux/profile.h>
#include <linux/export.h>
#include <linux/mount.h>
#include <linux/mempolicy.h>
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <linux/mmdebug.h>
#include <linux/perf_event.h>
#include <linux/audit.h>
#include <linux/khugepaged.h>
#include <linux/uprobes.h>
#include <linux/rbtree_augmented.h>
#include <linux/notifier.h>
#include <linux/memory.h>
#include <linux/printk.h>
#include <linux/userfaultfd_k.h>
#include <linux/moduleparam.h>
#include <linux/pkeys.h>
#include <linux/oom.h>
#include <linux/sched/mm.h>

#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>

#define CREATE_TRACE_POINTS
#include <trace/events/mmap.h>

#include "internal.h"

#ifndef arch_mmap_check
#define arch_mmap_check(addr, len, flags)	(0)
#endif

#ifdef CONFIG_HAVE_ARCH_MMAP_RND_BITS
const int mmap_rnd_bits_min = CONFIG_ARCH_MMAP_RND_BITS_MIN;
const int mmap_rnd_bits_max = CONFIG_ARCH_MMAP_RND_BITS_MAX;
int __read_mostly mmap_rnd_bits  = CONFIG_ARCH_MMAP_RND_BITS;
#endif
#ifdef CONFIG_HAVE_ARCH_MMAP_RND_COMPAT_BITS
const int mmap_rnd_compat_bits_min = CONFIG_ARCH_MMAP_RND_COMPAT_BITS_MIN;
const int mmap_rnd_compat_bits_max = CONFIG_ARCH_MMAP_RND_COMPAT_BITS_MAX;
int __read_mostly mmap_rnd_compat_bits  = CONFIG_ARCH_MMAP_RND_COMPAT_BITS;
#endif

static bool ignore_rlimit_data;
core_param(ignore_rlimit_data, ignore_rlimit_data, bool, 0644);

static void unmap_region(struct mm_struct *mm,
		struct vm_area_struct *vma, struct vm_area_struct *prev,
		unsigned long start, unsigned long end);

/* description of effects of mapping type and prot in current implementation.
 * this is due to the limited x86 page protection hardware.  The expected
 * behavior is in parens:
 *
 * map_type	prot
 *		PROT_NONE	PROT_READ	PROT_WRITE	PROT_EXEC
 * MAP_SHARED	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (yes) yes	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *
 * MAP_PRIVATE	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (copy) copy	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *
 * 把 vm_flags 转化成 PTE 硬件标志位, 见`vm_get_page_prot()`
 */
pgprot_t __ro_after_init protection_map[16]  = {    /* VMA 权限 */
	__P000, 
    __P001, /* VM_READ */
    __P010, /* VM_WRITE */
    __P011, /* VM_READ|VM_WRITE */
    __P100, /* VM_EXEC */
    __P101, /* VM_EXEC|VM_READ */
    __P110,
    __P111,
	__S000,
	__S001,
	__S010,
	__S011,
	__S100, 
	__S101,
	__S110,
	__S111  /* VM_READ|VM_WRITE|VM_EXEC */
};

#ifndef CONFIG_ARCH_HAS_FILTER_PGPROT
static inline pgprot_t arch_filter_pgprot(pgprot_t prot)
{
	return prot;
}
#endif

/**
 * 把 vm_flags 转化成 PTE 硬件标志位
 */
pgprot_t vm_get_page_prot(unsigned long vm_flags)   /*  */
{
	pgprot_t ret = __pgprot(pgprot_val(protection_map[vm_flags & (VM_READ|VM_WRITE|VM_EXEC|VM_SHARED)] ) |
            			    pgprot_val(arch_vm_get_page_prot(vm_flags)));

	return arch_filter_pgprot(ret);
}
EXPORT_SYMBOL(vm_get_page_prot);

static pgprot_t vm_pgprot_modify(pgprot_t oldprot, unsigned long vm_flags)
{
	return pgprot_modify(oldprot, vm_get_page_prot(vm_flags));
}

/* Update vma->vm_page_prot to reflect vma->vm_flags. */
void vma_set_page_prot(struct vm_area_struct *vma)
{
	unsigned long vm_flags = vma->vm_flags;
	pgprot_t vm_page_prot;

	vm_page_prot = vm_pgprot_modify(vma->vm_page_prot, vm_flags);
	if (vma_wants_writenotify(vma, vm_page_prot)) {
		vm_flags &= ~VM_SHARED;
		vm_page_prot = vm_pgprot_modify(vm_page_prot, vm_flags);
	}
	/* remove_protection_ptes reads vma->vm_page_prot without mmap_lock */
	WRITE_ONCE(vma->vm_page_prot, vm_page_prot);
}

/*
 * Requires inode->i_mapping->i_mmap_rwsem
 */
static void __remove_shared_vm_struct(struct vm_area_struct *vma,
		struct file *file, struct address_space *mapping)
{
	if (vma->vm_flags & VM_DENYWRITE)
		allow_write_access(file);
	if (vma->vm_flags & VM_SHARED)
		mapping_unmap_writable(mapping);

	flush_dcache_mmap_lock(mapping);
	vma_interval_tree_remove(vma, &mapping->i_mmap);
	flush_dcache_mmap_unlock(mapping);
}

/*
 * Unlink a file-based vm structure from its interval tree, to hide
 * vma from rmap and vmtruncate before freeing its page tables.
 */
void unlink_file_vma(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;

	if (file) {
		struct address_space *mapping = file->f_mapping;
		i_mmap_lock_write(mapping);
		__remove_shared_vm_struct(vma, file, mapping);
		i_mmap_unlock_write(mapping);
	}
}

/*
 * Close a vm structure and free it, returning the next.
 */
static struct vm_area_struct *remove_vma(struct vm_area_struct *vma)    /*  */
{
	struct vm_area_struct *next = vma->vm_next;

	might_sleep();
	if (vma->vm_ops && vma->vm_ops->close)
		vma->vm_ops->close(vma);
	if (vma->vm_file)
		fput(vma->vm_file);
	mpol_put(vma_policy(vma));
	vm_area_free(vma);
	return next;
}

static int do_brk_flags(unsigned long addr, unsigned long request, unsigned long flags,
		struct list_head *uf);
SYSCALL_DEFINE1(brk, unsigned long, brk)    /* int brk(void *addr);  void *sbrk(intptr_t increment); */
{
	unsigned long retval;
	unsigned long newbrk, oldbrk, origbrk;
	struct mm_struct *mm = current->mm; /* 获取当前进程的 mm_struct 结构 */
	struct vm_area_struct *next;    /* 下一个 vma */
	unsigned long min_brk;
	bool populate;
	bool downgraded = false;
	LIST_HEAD(uf);

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	origbrk = mm->brk;  /* 原始的堆顶 */

#ifdef CONFIG_COMPAT_BRK
	/*
	 * CONFIG_COMPAT_BRK can still be overridden by setting
	 * randomize_va_space to 2, which will still cause mm->start_brk
	 * to be arbitrarily shifted
	 */
//	if (current->brk_randomized)
//		min_brk = mm->start_brk;
//	else
//		min_brk = mm->end_data;
#else
	min_brk = mm->start_brk;    /* brk 开始的地方 */
#endif
	if (brk < min_brk)
		goto out;

	/*
	 * Check against rlimit here. If this check is done later after the test
	 * of oldbrk with newbrk then it can escape the test and let the data
	 * segment grow beyond its set limit the in case where the limit is
	 * not page aligned -Ram Gupta
	 *//* 检测资源限制 
	 
    +-------+ brk
    |       |
    |       |   堆
    |  heap |
    +-------+ mm->start_brk
    |       |
    |  ...  |
    |       |
    +-------+ mm->end_data
    |       |     
    |  data |   数据段
    |       |
    +-------+ mm->start_data

	 */
	if (check_data_rlimit(rlimit(RLIMIT_DATA), brk, mm->start_brk,
			      mm->end_data, mm->start_data))
		goto out;

	newbrk = PAGE_ALIGN(brk);       /* 新的 brk ：页对齐，申请大小对齐 page */
	oldbrk = PAGE_ALIGN(mm->brk);   /* 旧的 brk */
	if (oldbrk == newbrk) { /* brk 位置没有发生变化 */
		mm->brk = brk;
		goto success;
	}

	/*
	 * Always allow shrinking brk.
	 * __do_munmap() may downgrade mmap_lock to read.
	 *
	 * 对应 free 操作
	 */
	if (brk <= mm->brk) {   /* brk < 原有 brk -> 这是一个 free 操作 */
		int ret;

		/*
		 * mm->brk must to be protected by write mmap_lock so update it
		 * before downgrading mmap_lock. When __do_munmap() fails,
		 * mm->brk will be restored from origbrk.
		 */
		mm->brk = brk;  /* 保存 */

        /*
        +-------+ oldbrk ~ mm->brk 上次的位置
        |       |
        |       |
        |       | newbrk ~ mm->brk 约等于，页对齐
        |       |
        |       |
        +-------+ mm->start_brk
        */
		ret = __do_munmap(mm, newbrk, oldbrk-newbrk, &uf, true);    /* do munmap */
		if (ret < 0) {
			mm->brk = origbrk;  /* unmap 失败使用原来的brk 位置 */
			goto out;
		} else if (ret == 1) {
			downgraded = true;
		}
		goto success;
	}
    /* 新的 brk 位置高于 旧的 brk 位置 */
    /*
                  newbrk ~ mm->brk 约等于，页对齐
        
        +-------+ oldbrk ~ mm->brk 上次的位置
        |       |
        |       |
        |       | 
        |       |
        |       |
        +-------+ mm->start_brk
    */
	/* Check against existing mmap mappings. vm_start <= oldbrk */
	next = find_vma(mm, oldbrk);
	if (next && newbrk + PAGE_SIZE > vm_start_gap(next))
		goto out;

	/* Ok, looks good - let it rip. */
	if (do_brk_flags(oldbrk, newbrk-oldbrk, 0, &uf) < 0)    /* 如果 brk 比原来 高，分配新的vma */
		goto out;
	mm->brk = brk;  /* 更新brk位置 */

success:
    /* 如果新页并且锁定标志设置 */
	populate = newbrk > oldbrk && (mm->def_flags & VM_LOCKED) != 0;
	if (downgraded)
		mmap_read_unlock(mm);
	else
		mmap_write_unlock(mm);
	userfaultfd_unmap_complete(mm, &uf);    /*  */
	if (populate)   /*  */
		mm_populate(oldbrk, newbrk - oldbrk);   /* TODO */
	return brk;

out:
	retval = origbrk;
	mmap_write_unlock(mm);
	return retval;
}

static inline unsigned long vma_compute_gap(struct vm_area_struct *vma)
{
	unsigned long gap, prev_end;

	/*
	 * Note: in the rare case of a VM_GROWSDOWN above a VM_GROWSUP, we
	 * allow two stack_guard_gaps between them here, and when choosing
	 * an unmapped area; whereas when expanding we only require one.
	 * That's a little inconsistent, but keeps the code here simpler.
	 */
	gap = vm_start_gap(vma);
	if (vma->vm_prev) {
		prev_end = vm_end_gap(vma->vm_prev);
		if (gap > prev_end)
			gap -= prev_end;
		else
			gap = 0;
	}
	return gap;
}

#ifdef CONFIG_DEBUG_VM_RB
static unsigned long vma_compute_subtree_gap(struct vm_area_struct *vma)
{
	unsigned long max = vma_compute_gap(vma), subtree_gap;
	if (vma->vm_rb.rb_left) {
		subtree_gap = rb_entry(vma->vm_rb.rb_left,
				struct vm_area_struct, vm_rb)->rb_subtree_gap;
		if (subtree_gap > max)
			max = subtree_gap;
	}
	if (vma->vm_rb.rb_right) {
		subtree_gap = rb_entry(vma->vm_rb.rb_right,
				struct vm_area_struct, vm_rb)->rb_subtree_gap;
		if (subtree_gap > max)
			max = subtree_gap;
	}
	return max;
}

static int browse_rb(struct mm_struct *mm)
{
	struct rb_root *root = &mm->mm_rb;
	int i = 0, j, bug = 0;
	struct rb_node *nd, *pn = NULL;
	unsigned long prev = 0, pend = 0;

	for (nd = rb_first(root); nd; nd = rb_next(nd)) {
		struct vm_area_struct *vma;
		vma = rb_entry(nd, struct vm_area_struct, vm_rb);
		if (vma->vm_start < prev) {
			pr_emerg("vm_start %lx < prev %lx\n",
				  vma->vm_start, prev);
			bug = 1;
		}
		if (vma->vm_start < pend) {
			pr_emerg("vm_start %lx < pend %lx\n",
				  vma->vm_start, pend);
			bug = 1;
		}
		if (vma->vm_start > vma->vm_end) {
			pr_emerg("vm_start %lx > vm_end %lx\n",
				  vma->vm_start, vma->vm_end);
			bug = 1;
		}
		spin_lock(&mm->page_table_lock);
		if (vma->rb_subtree_gap != vma_compute_subtree_gap(vma)) {
			pr_emerg("free gap %lx, correct %lx\n",
			       vma->rb_subtree_gap,
			       vma_compute_subtree_gap(vma));
			bug = 1;
		}
		spin_unlock(&mm->page_table_lock);
		i++;
		pn = nd;
		prev = vma->vm_start;
		pend = vma->vm_end;
	}
	j = 0;
	for (nd = pn; nd; nd = rb_prev(nd))
		j++;
	if (i != j) {
		pr_emerg("backwards %d, forwards %d\n", j, i);
		bug = 1;
	}
	return bug ? -1 : i;
}

static void validate_mm_rb(struct rb_root *root, struct vm_area_struct *ignore)
{
	struct rb_node *nd;

	for (nd = rb_first(root); nd; nd = rb_next(nd)) {
		struct vm_area_struct *vma;
		vma = rb_entry(nd, struct vm_area_struct, vm_rb);
		VM_BUG_ON_VMA(vma != ignore &&
			vma->rb_subtree_gap != vma_compute_subtree_gap(vma),
			vma);
	}
}

static void validate_mm(struct mm_struct *mm)   /*  */
{
	int bug = 0;
	int i = 0;
	unsigned long highest_address = 0;
	struct vm_area_struct *vma = mm->mmap;

	while (vma) {
		struct anon_vma *anon_vma = vma->anon_vma;
		struct anon_vma_chain *avc;

		if (anon_vma) {
			anon_vma_lock_read(anon_vma);
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma) {
				anon_vma_interval_tree_verify(avc); }
			anon_vma_unlock_read(anon_vma);
		}

		highest_address = vm_end_gap(vma);
		vma = vma->vm_next;
		i++;
	}
	if (i != mm->map_count) {
		pr_emerg("map_count %d vm_next %d\n", mm->map_count, i);
		bug = 1;
	}
	if (highest_address != mm->highest_vm_end) {
		pr_emerg("mm->highest_vm_end %lx, found %lx\n",
			  mm->highest_vm_end, highest_address);
		bug = 1;
	}
	i = browse_rb(mm);
	if (i != mm->map_count) {
		if (i != -1)
			pr_emerg("map_count %d rb %d\n", mm->map_count, i);
		bug = 1;
	}
	VM_BUG_ON_MM(bug, mm);
}
#else
/*  */
#endif

RB_DECLARE_CALLBACKS_MAX(static, vma_gap_callbacks,
			 struct vm_area_struct, vm_rb,
			 unsigned long, rb_subtree_gap, vma_compute_gap){}/* 我加的 {} */

#if __RTOAX__________RB_DECLARE_CALLBACKS_MAX
/*
 * Template for declaring augmented rbtree callbacks (generic case)
 *
 * static:    'static' or empty
 * vma_gap_callbacks:      name of the rb_augment_callbacks structure
 * struct vm_area_struct:    struct type of the tree nodes
 * vm_rb:     name of struct rb_node field within struct vm_area_struct
 * rb_subtree_gap: name of field within struct vm_area_struct holding data for subtree
 * vma_compute_gap:   name of function that recomputes the rb_subtree_gap data
 */

// #define RB_DECLARE_CALLBACKS(static, vma_gap_callbacks,				
			     // struct vm_area_struct, vm_rb, rb_subtree_gap, vma_compute_gap)	
static inline void							
vma_gap_callbacks_propagate(struct rb_node *rb, struct rb_node *stop)		
{									
	while (rb != stop) {						
		struct vm_area_struct *node = rb_entry(rb, struct vm_area_struct, vm_rb);	
		if (vma_compute_gap(node, true))				
			break;						
		rb = rb_parent(&node->vm_rb);				
	}								
}									
static inline void							
vma_gap_callbacks_copy(struct rb_node *rb_old, struct rb_node *rb_new)		
{									
	struct vm_area_struct *old = rb_entry(rb_old, struct vm_area_struct, vm_rb);		
	struct vm_area_struct *new = rb_entry(rb_new, struct vm_area_struct, vm_rb);		
	new->rb_subtree_gap = old->rb_subtree_gap;				
}									
static void								
vma_gap_callbacks_rotate(struct rb_node *rb_old, struct rb_node *rb_new)	
{									
	struct vm_area_struct *old = rb_entry(rb_old, struct vm_area_struct, vm_rb);		
	struct vm_area_struct *new = rb_entry(rb_new, struct vm_area_struct, vm_rb);		
	new->rb_subtree_gap = old->rb_subtree_gap;				
	vma_compute_gap(old, false);						
}									
static const struct rb_augment_callbacks vma_gap_callbacks = {			
	.propagate = vma_gap_callbacks_propagate,				
	.copy = vma_gap_callbacks_copy,					
	.rotate = vma_gap_callbacks_rotate					
};

/*
 * Template for declaring augmented rbtree callbacks,
 * computing rb_subtree_gap scalar as max(vma_compute_gap(node)) for all subtree nodes.
 *
 * static:    'static' or empty
 * vma_gap_callbacks:      name of the rb_augment_callbacks structure
 * struct vm_area_struct:    struct type of the tree nodes
 * vm_rb:     name of struct rb_node field within struct vm_area_struct
 * unsigned long:      type of the rb_subtree_gap field
 * rb_subtree_gap: name of unsigned long field within struct vm_area_struct holding data for subtree
 * vma_compute_gap:   name of function that returns the per-node unsigned long scalar
 */

// #define RB_DECLARE_CALLBACKS_MAX(static, vma_gap_callbacks, struct vm_area_struct, vm_rb,	      
				 // unsigned long, rb_subtree_gap, vma_compute_gap)	      
static inline bool vma_gap_callbacks_compute_max(struct vm_area_struct *node, bool exit)	      
{									      
	struct vm_area_struct *child;						      
	unsigned long max = vma_compute_gap(node);					      
	if (node->vm_rb.rb_left) {					      
		child = rb_entry(node->vm_rb.rb_left, struct vm_area_struct, vm_rb);   
		if (child->rb_subtree_gap > max)				      
			max = child->rb_subtree_gap;			      
	}								      
	if (node->vm_rb.rb_right) {					      
		child = rb_entry(node->vm_rb.rb_right, struct vm_area_struct, vm_rb);  
		if (child->rb_subtree_gap > max)				      
			max = child->rb_subtree_gap;			      
	}								      
	if (exit && node->rb_subtree_gap == max)				      
		return true;						      
	node->rb_subtree_gap = max;					      
	return false;							      
}									      
// RB_DECLARE_CALLBACKS(static, vma_gap_callbacks,					      
		     // struct vm_area_struct, vm_rb, rb_subtree_gap, vma_gap_callbacks_compute_max)


#endif //__RTOAX__________RB_DECLARE_CALLBACKS_MAX

/*
 * Update augmented rbtree rb_subtree_gap values after vma->vm_start or
 * vma->vm_prev->vm_end values changed, without modifying the vma's position
 * in the rbtree.
 */
static void vma_gap_update(struct vm_area_struct *vma)
{
	/*
	 * As it turns out, RB_DECLARE_CALLBACKS_MAX() already created
	 * a callback function that does exactly what we want.
	 */
	vma_gap_callbacks_propagate(&vma->vm_rb, NULL);
}

static inline void vma_rb_insert(struct vm_area_struct *vma,
				 struct rb_root *root)
{
	/* All rb_subtree_gap values must be consistent prior to insertion */
	validate_mm_rb(root, NULL);

	rb_insert_augmented(&vma->vm_rb, root, &vma_gap_callbacks);
}

static void __vma_rb_erase(struct vm_area_struct *vma, struct rb_root *root)
{
	/*
	 * Note rb_erase_augmented is a fairly large inline function,
	 * so make sure we instantiate it only once with our desired
	 * augmented rbtree callbacks.
	 */
	rb_erase_augmented(&vma->vm_rb, root, &vma_gap_callbacks);
}

static __always_inline void vma_rb_erase_ignore(struct vm_area_struct *vma,
						struct rb_root *root,
						struct vm_area_struct *ignore)
{
	/*
	 * All rb_subtree_gap values must be consistent prior to erase,
	 * with the possible exception of
	 *
	 * a. the "next" vma being erased if next->vm_start was reduced in
	 *    __vma_adjust() -> __vma_unlink()
	 * b. the vma being erased in detach_vmas_to_be_unmapped() ->
	 *    vma_rb_erase()
	 */
	validate_mm_rb(root, ignore);

	__vma_rb_erase(vma, root);
}

static __always_inline void vma_rb_erase(struct vm_area_struct *vma,
					 struct rb_root *root)
{
	vma_rb_erase_ignore(vma, root, vma);
}

/*
 * vma has some anon_vma assigned, and is already inserted on that
 * anon_vma's interval trees.
 *
 * Before updating the vma's vm_start / vm_end / vm_pgoff fields, the
 * vma must be removed from the anon_vma's interval trees using
 * anon_vma_interval_tree_pre_update_vma().
 *
 * After the update, the vma will be reinserted using
 * anon_vma_interval_tree_post_update_vma().
 *
 * The entire update must be protected by exclusive mmap_lock and by
 * the root anon_vma's mutex.
 */
static inline void
anon_vma_interval_tree_pre_update_vma(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc;

	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
		anon_vma_interval_tree_remove(avc, &avc->anon_vma->rb_root);
}

static inline void
anon_vma_interval_tree_post_update_vma(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc;

	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
		anon_vma_interval_tree_insert(avc, &avc->anon_vma->rb_root);
}

/**
 * 
 */
static int find_vma_links(struct mm_struct *mm, unsigned long addr,
		unsigned long end, struct vm_area_struct **pprev,
		struct rb_node ***rb_link, struct rb_node **rb_parent)
{
	struct rb_node **__rb_link, *__rb_parent, *rb_prev;

    /**
     *  进程虚拟地址空间 红黑树的根 进行遍历 
     */
	__rb_link = &mm->mm_rb.rb_node; 
	rb_prev = __rb_parent = NULL;

    /*  */
	while (*__rb_link) {
		struct vm_area_struct *vma_tmp;

        /* 首先 保存当前节点 到 parent */
		__rb_parent = *__rb_link;

        /* 获取当前节点的 VMA */
		vma_tmp = rb_entry(__rb_parent, struct vm_area_struct, vm_rb);

        /* vma->vm_end 大于 addr */
		if (vma_tmp->vm_end > addr) {
            
			/**
			 *  Fail if an existing vma overlaps the area 如果现存vma覆盖了这个area
			 *
			 *  end +----+
			 *      |    |
			 *      |    | 
			 *      |    |   
			 *      |    |  <---- vm_end
			 *      |    |
			 * addr +----+
			 *
			 *              <---- vm_start
			 *
			 */
			if (vma_tmp->vm_start < end)    
				return -ENOMEM; /* 这个 start~start+len 已经存在 vma */
            /* 
                    vm_start-vm_end(s-e)
                          [7-8)
                            o
                           / \
                          /   \
                         /     \
                        /       \
                       /         \
                    [3-4)       [11-12)
                     / \         / \
                    /   \       /   \
                   /     \     /     \
                [1-2)  [5-6) [9-10) [13-14)
                      ^^^
            */
            /* 左 子树 */
			__rb_link = &__rb_parent->rb_left;
            
		} else {    /* vma_tmp->vm_end <= addr */

			/**
			 *
			 *  end +----+
			 *      |    |
			 *      |    | 
			 *      |    |   
			 *      |    |  
			 *      |    |
			 * addr +----+  <---- vm_end
			 *              <---- vm_end
			 *              ...
			 *              <---- vm_start
			 *
			 */

            /* 记录当前 节点 */
			rb_prev = __rb_parent;

            /* 右 子树 */
			__rb_link = &__rb_parent->rb_right;
		}
	}

	*pprev = NULL;
	if (rb_prev)
		*pprev = rb_entry(rb_prev, struct vm_area_struct, vm_rb);
	*rb_link = __rb_link;
	*rb_parent = __rb_parent;
    
	return 0;
}

/*
 * vma_next() - Get the next VMA.
 * @mm: The mm_struct.
 * @vma: The current vma.
 *
 * If @vma is NULL, return the first vma in the mm.
 *
 * Returns: The next VMA after @vma.
 */
static inline struct vm_area_struct *vma_next(struct mm_struct *mm,
					 struct vm_area_struct *vma)
{
	if (!vma)
		return mm->mmap;

	return vma->vm_next;
}

/*
 * munmap_vma_range() - munmap VMAs that overlap a range.
 * @mm: The mm struct
 * @start: The start of the range.
 * @len: The length of the range.
 * @pprev: pointer to the pointer that will be set to previous vm_area_struct
 * @rb_link: the rb_node
 * @rb_parent: the parent rb_node
 *
 * Find all the vm_area_struct that overlap from @start to
 * @end and munmap them.  Set @pprev to the previous vm_area_struct.
 *
 * Returns: -ENOMEM on munmap failure or 0 on success.
 */
static inline int
munmap_vma_range(struct mm_struct *mm, unsigned long start, unsigned long len,
		 struct vm_area_struct **pprev, struct rb_node ***link,
		 struct rb_node **parent, struct list_head *uf)
{
    /* 查找 vma，如果返回 -ENOMEM ，进行 do_munmap */
    /**
     *  如果新地址和旧的vma有覆盖的情况：
     *  把覆盖的地址范围的vma分割出来，先释放掉
     *
     *  如果 根据 start 找到了 VMA，则返回 -ENOMEM, 这时，调用 do_munmap()
     */
	while (find_vma_links(mm, start, start + len, pprev, link, parent)) {
        /**
         *  do_munmap 成功，返回 0，继续进入 while 循环 
         */
		if (do_munmap(mm, start, len, uf)) {
			return -ENOMEM;
        }
    }
	return 0;
}

/*
 * 计算区间内 的总共 页数
 * 
 */
static unsigned long count_vma_pages_range(struct mm_struct *mm,
		unsigned long addr, unsigned long end)/*  */
{
	unsigned long nr_pages = 0;
	struct vm_area_struct *vma;

	/* Find first overlaping mapping */
	vma = find_vma_intersection(mm, addr, end);
	if (!vma)
		return 0;

    /* 计算页数 */
	nr_pages = (min(end, vma->vm_end) -
		max(addr, vma->vm_start)) >> PAGE_SHIFT;

	/* Iterate over the rest of the overlaps */
    /* 轮询属于这个地址范围内的 vma */
	for (vma = vma->vm_next; vma; vma = vma->vm_next) {
		unsigned long overlap_len;

		if (vma->vm_start > end)
			break;

		overlap_len = min(end, vma->vm_end) - vma->vm_start;
		nr_pages += overlap_len >> PAGE_SHIFT;
	}

	return nr_pages;
}

void __vma_link_rb(struct mm_struct *mm, struct vm_area_struct *vma,
		struct rb_node **rb_link, struct rb_node *rb_parent)
{
	/* Update tracking information for the gap following the new vma. */
	if (vma->vm_next)
		vma_gap_update(vma->vm_next);
	else
		mm->highest_vm_end = vm_end_gap(vma);

	/*
	 * vma->vm_prev wasn't known when we followed the rbtree to find the
	 * correct insertion point for that vma. As a result, we could not
	 * update the vma vm_rb parents rb_subtree_gap values on the way down.
	 * So, we first insert the vma with a zero rb_subtree_gap value
	 * (to be consistent with what we did on the way down), and then
	 * immediately update the gap to the correct value. Finally we
	 * rebalance the rbtree after all augmented values have been set.
	 */
	rb_link_node(&vma->vm_rb, rb_parent, rb_link);
	vma->rb_subtree_gap = 0;
	vma_gap_update(vma);
	vma_rb_insert(vma, &mm->mm_rb);
}

static void __vma_link_file(struct vm_area_struct *vma) /*  */
{
	struct file *file;

	file = vma->vm_file;
	if (file) { /* 文件映射，更新 缓存 */
		struct address_space *mapping = file->f_mapping;

		if (vma->vm_flags & VM_DENYWRITE)
			put_write_access(file_inode(file));
		if (vma->vm_flags & VM_SHARED)
			mapping_allow_writable(mapping);

		flush_dcache_mmap_lock(mapping);
		vma_interval_tree_insert(vma, &mapping->i_mmap);
		flush_dcache_mmap_unlock(mapping);
	}
}

static void
__vma_link(struct mm_struct *mm, struct vm_area_struct *vma,
	struct vm_area_struct *prev, struct rb_node **rb_link,
	struct rb_node *rb_parent)
{
	__vma_link_list(mm, vma, prev); /* 添加至链表 */
	__vma_link_rb(mm, vma, rb_link, rb_parent); /* 添加至红黑树 */
}

static void vma_link(struct mm_struct *mm, struct vm_area_struct *vma,
			struct vm_area_struct *prev, struct rb_node **rb_link,
			struct rb_node *rb_parent)
{
	struct address_space *mapping = NULL;

	if (vma->vm_file) { /* 文件映射 */
		mapping = vma->vm_file->f_mapping;
		i_mmap_lock_write(mapping);
	}

    /* 将新的vma插入到vma红黑树和vma链表中，并且更新树上的各种参数 */
	__vma_link(mm, vma, prev, rb_link, rb_parent);  /* 添加至链表和红黑树 */

    /* 将vma插入到文件的file->f_mapping->i_mmap缓存树中 */
	__vma_link_file(vma);   /* 文件映射的话，更新缓存, 添加到基数树 */

	if (mapping)
		i_mmap_unlock_write(mapping);

	mm->map_count++;    /* 映射计数++ */
	validate_mm(mm);    /*  */
}

/*
 * Helper for vma_adjust() in the split_vma insert case: insert a vma into the
 * mm's list and rbtree.  It has already been inserted into the interval tree.
 */
static void __insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vma)
{
	struct vm_area_struct *prev;
	struct rb_node **rb_link, *rb_parent;

	if (find_vma_links(mm, vma->vm_start, vma->vm_end,
			   &prev, &rb_link, &rb_parent))
		BUG();
	__vma_link(mm, vma, prev, rb_link, rb_parent);
	mm->map_count++;
}

static __always_inline void __vma_unlink(struct mm_struct *mm,
						struct vm_area_struct *vma,
						struct vm_area_struct *ignore)
{
	vma_rb_erase_ignore(vma, &mm->mm_rb, ignore);
	__vma_unlink_list(mm, vma);
	/* Kill the cache */
	vmacache_invalidate(mm);
}

/*
 * We cannot adjust vm_start, vm_end, vm_pgoff fields of a vma that
 * is already present in an i_mmap tree without adjusting the tree.
 * The following helper function should be used when such adjustments
 * are necessary.  The "insert" vma (if any) is to be inserted
 * before we drop the necessary locks.
 *
 * 如果不调整树，则无法调整i_mmap树中已经存在的vma的vm_start，vm_end，vm_pgoff字段。 
 * 当需要进行此类调整时，应使用以下帮助器功能。 在插入必要的锁之前，将插入“插入” vma（如果有）。
 */ /*  */
int __vma_adjust(struct vm_area_struct *vma, unsigned long start,
	unsigned long end, pgoff_t pgoff, struct vm_area_struct *insert,
	struct vm_area_struct *expand)
{
    /*
                                +-------+
                                |       |
                                |       |
                                |       |
                                | next  |
                                |       |
                                |       |
                                +-------+
                                
    +-------+--- end            orig_vma
    |       |                   +-------+               +-------+
    |       | len               |  vma  |               |       |
    |       |--- start -------->|       |               | insert|
    |       |                   |       |               |       |
    |       |                   |       | <-- end ----> +-------+
    +-------+ mm->start_brk     |       |
                                |       |
                                +-------+ start

    */
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *next = vma->vm_next, *orig_vma = vma;
	struct address_space *mapping = NULL;
	struct rb_root_cached *root = NULL;
	struct anon_vma *anon_vma = NULL;
	struct file *file = vma->vm_file;
	bool start_changed = false, end_changed = false;
	long adjust_next = 0;
	int remove_next = 0;

	if (next && !insert) {
		struct vm_area_struct *exporter = NULL, *importer = NULL;

		if (end >= next->vm_end) {
			/*
			 * vma expands, overlapping all the next, and
			 * perhaps the one after too (mprotect case 6).
			 * The only other cases that gets here are
			 * case 1, case 7 and case 8.
			 */
			if (next == expand) {
				/*
				 * The only case where we don't expand "vma"
				 * and we expand "next" instead is case 8.
				 */
				VM_WARN_ON(end != next->vm_end);
				/*
				 * remove_next == 3 means we're
				 * removing "vma" and that to do so we
				 * swapped "vma" and "next".
				 */
				remove_next = 3;
				VM_WARN_ON(file != next->vm_file);
				swap(vma, next);
			} else {
				VM_WARN_ON(expand != vma);
				/*
				 * case 1, 6, 7, remove_next == 2 is case 6,
				 * remove_next == 1 is case 1 or 7.
				 */
				remove_next = 1 + (end > next->vm_end);
				VM_WARN_ON(remove_next == 2 &&
					   end != next->vm_next->vm_end);
				/* trim end to next, for case 6 first pass */
				end = next->vm_end;
			}

			exporter = next;
			importer = vma;

			/*
			 * If next doesn't have anon_vma, import from vma after
			 * next, if the vma overlaps with it.
			 */
			if (remove_next == 2 && !next->anon_vma)
				exporter = next->vm_next;

		} else if (end > next->vm_start) {
			/*
			 * vma expands, overlapping part of the next:
			 * mprotect case 5 shifting the boundary up.
			 */
			adjust_next = (end - next->vm_start);
			exporter = next;
			importer = vma;
			VM_WARN_ON(expand != importer);
		} else if (end < vma->vm_end) {
			/*
			 * vma shrinks, and !insert tells it's not
			 * split_vma inserting another: so it must be
			 * mprotect case 4 shifting the boundary down.
			 */
			adjust_next = -(vma->vm_end - end);
			exporter = vma;
			importer = next;
			VM_WARN_ON(expand != importer);
		}

		/*
		 * Easily overlooked: when mprotect shifts the boundary,
		 * make sure the expanding vma has anon_vma set if the
		 * shrinking vma had, to cover any anon pages imported.
		 */
		if (exporter && exporter->anon_vma && !importer->anon_vma) {
			int error;

			importer->anon_vma = exporter->anon_vma;
			error = anon_vma_clone(importer, exporter);
			if (error)
				return error;
		}
	}
again:
	vma_adjust_trans_huge(orig_vma, start, end, adjust_next);

	if (file) {
		mapping = file->f_mapping;
		root = &mapping->i_mmap;
		uprobe_munmap(vma, vma->vm_start, vma->vm_end);

		if (adjust_next)
			uprobe_munmap(next, next->vm_start, next->vm_end);

		i_mmap_lock_write(mapping);
		if (insert) {
			/*
			 * Put into interval tree now, so instantiated pages
			 * are visible to arm/parisc __flush_dcache_page
			 * throughout; but we cannot insert into address
			 * space until vma start or end is updated.
			 */
			__vma_link_file(insert);
		}
	}

	anon_vma = vma->anon_vma;
	if (!anon_vma && adjust_next)
		anon_vma = next->anon_vma;
	if (anon_vma) {
		VM_WARN_ON(adjust_next && next->anon_vma &&
			   anon_vma != next->anon_vma);
		anon_vma_lock_write(anon_vma);
		anon_vma_interval_tree_pre_update_vma(vma);
		if (adjust_next)
			anon_vma_interval_tree_pre_update_vma(next);
	}

	if (file) {
		flush_dcache_mmap_lock(mapping);
		vma_interval_tree_remove(vma, root);
		if (adjust_next)
			vma_interval_tree_remove(next, root);
	}
    /*
     +-------+
     |  vma  |
     |       | 
     |       |
     |       | <-- end
     |       |
     |       |
     +-------+ start
 
     >>>> 变为
                
     +-------+ <-- vma->vm_end
     |  vma  |
     |       |
     +-------+ <-- vma->vm_start

     */
	if (start != vma->vm_start) {
		vma->vm_start = start;
		start_changed = true;
	}
	if (end != vma->vm_end) {
		vma->vm_end = end;
		end_changed = true;
	}
	vma->vm_pgoff = pgoff;
	if (adjust_next) {
		next->vm_start += adjust_next;
		next->vm_pgoff += adjust_next >> PAGE_SHIFT;
	}

	if (file) {
		if (adjust_next)
			vma_interval_tree_insert(next, root);
		vma_interval_tree_insert(vma, root);
		flush_dcache_mmap_unlock(mapping);
	}

	if (remove_next) {
		/*
		 * vma_merge has merged next into vma, and needs
		 * us to remove next before dropping the locks.
		 */
		if (remove_next != 3)
			__vma_unlink(mm, next, next);
		else
			/*
			 * vma is not before next if they've been
			 * swapped.
			 *
			 * pre-swap() next->vm_start was reduced so
			 * tell validate_mm_rb to ignore pre-swap()
			 * "next" (which is stored in post-swap()
			 * "vma").
			 */
			__vma_unlink(mm, next, vma);
		if (file)
			__remove_shared_vm_struct(next, file, mapping);
	} else if (insert) {
		/*
		 * split_vma has split insert from vma, and needs
		 * us to insert it before dropping the locks
		 * (it may either follow vma or precede it).
		 */
		__insert_vm_struct(mm, insert);
	} else {
		if (start_changed)
			vma_gap_update(vma);
		if (end_changed) {
			if (!next)
				mm->highest_vm_end = vm_end_gap(vma);
			else if (!adjust_next)
				vma_gap_update(next);
		}
	}

	if (anon_vma) {
		anon_vma_interval_tree_post_update_vma(vma);
		if (adjust_next)
			anon_vma_interval_tree_post_update_vma(next);
		anon_vma_unlock_write(anon_vma);
	}

	if (file) {
		i_mmap_unlock_write(mapping);
		uprobe_mmap(vma);

		if (adjust_next)
			uprobe_mmap(next);
	}

	if (remove_next) {
		if (file) {
			uprobe_munmap(next, next->vm_start, next->vm_end);
			fput(file);
		}
		if (next->anon_vma)
			anon_vma_merge(vma, next);
		mm->map_count--;
		mpol_put(vma_policy(next));
		vm_area_free(next);
		/*
		 * In mprotect's case 6 (see comments on vma_merge),
		 * we must remove another next too. It would clutter
		 * up the code too much to do both in one go.
		 */
		if (remove_next != 3) {
			/*
			 * If "next" was removed and vma->vm_end was
			 * expanded (up) over it, in turn
			 * "next->vm_prev->vm_end" changed and the
			 * "vma->vm_next" gap must be updated.
			 */
			next = vma->vm_next;
		} else {
			/*
			 * For the scope of the comment "next" and
			 * "vma" considered pre-swap(): if "vma" was
			 * removed, next->vm_start was expanded (down)
			 * over it and the "next" gap must be updated.
			 * Because of the swap() the post-swap() "vma"
			 * actually points to pre-swap() "next"
			 * (post-swap() "next" as opposed is now a
			 * dangling pointer).
			 */
			next = vma;
		}
		if (remove_next == 2) {
			remove_next = 1;
			end = next->vm_end;
			goto again;
		}
		else if (next)
			vma_gap_update(next);
		else {
			/*
			 * If remove_next == 2 we obviously can't
			 * reach this path.
			 *
			 * If remove_next == 3 we can't reach this
			 * path because pre-swap() next is always not
			 * NULL. pre-swap() "next" is not being
			 * removed and its next->vm_end is not altered
			 * (and furthermore "end" already matches
			 * next->vm_end in remove_next == 3).
			 *
			 * We reach this only in the remove_next == 1
			 * case if the "next" vma that was removed was
			 * the highest vma of the mm. However in such
			 * case next->vm_end == "end" and the extended
			 * "vma" has vma->vm_end == next->vm_end so
			 * mm->highest_vm_end doesn't need any update
			 * in remove_next == 1 case.
			 */
			VM_WARN_ON(mm->highest_vm_end != vm_end_gap(vma));
		}
	}
	if (insert && file)
		uprobe_mmap(insert);

	validate_mm(mm);

	return 0;
}

/*
 * If the vma has a ->close operation then the driver probably needs to release
 * per-vma resources, so we don't attempt to merge those.
 */
static inline int is_mergeable_vma(struct vm_area_struct *vma,
				struct file *file, unsigned long vm_flags,
				struct vm_userfaultfd_ctx vm_userfaultfd_ctx)
{
	/*
	 * VM_SOFTDIRTY should not prevent from VMA merging, if we
	 * match the flags but dirty bit -- the caller should mark
	 * merged VMA as dirty. If dirty bit won't be excluded from
	 * comparison, we increase pressure on the memory system forcing
	 * the kernel to generate new VMAs when old one could be
	 * extended instead.
	 */
	if ((vma->vm_flags ^ vm_flags) & ~VM_SOFTDIRTY)
		return 0;
	if (vma->vm_file != file)
		return 0;
	if (vma->vm_ops && vma->vm_ops->close)
		return 0;
	if (!is_mergeable_vm_userfaultfd_ctx(vma, vm_userfaultfd_ctx))
		return 0;
	return 1;
}

static inline int is_mergeable_anon_vma(struct anon_vma *anon_vma1,
					struct anon_vma *anon_vma2,
					struct vm_area_struct *vma)
{
	/*
	 * The list_is_singular() test is to avoid merging VMA cloned from
	 * parents. This can improve scalability caused by anon_vma lock.
	 */
	if ((!anon_vma1 || !anon_vma2) && (!vma ||
		list_is_singular(&vma->anon_vma_chain)))
		return 1;
	return anon_vma1 == anon_vma2;
}

/*
 * Return true if we can merge this (vm_flags,anon_vma,file,vm_pgoff)
 * in front of (at a lower virtual address and file offset than) the vma.
 *
 * We cannot merge two vmas if they have differently assigned (non-NULL)
 * anon_vmas, nor if same anon_vma is assigned but offsets incompatible.
 *
 * We don't check here for the merged mmap wrapping around the end of pagecache
 * indices (16TB on ia32) because do_mmap() does not permit mmap's which
 * wrap, nor mmaps which cover the final page at index -1UL.
 */
static int
can_vma_merge_before(struct vm_area_struct *vma, unsigned long vm_flags,
		     struct anon_vma *anon_vma, struct file *file,
		     pgoff_t vm_pgoff,
		     struct vm_userfaultfd_ctx vm_userfaultfd_ctx)
{
	if (is_mergeable_vma(vma, file, vm_flags, vm_userfaultfd_ctx) &&
	    is_mergeable_anon_vma(anon_vma, vma->anon_vma, vma)) {
		if (vma->vm_pgoff == vm_pgoff)
			return 1;
	}
	return 0;
}

/*
 * Return true if we can merge this (vm_flags,anon_vma,file,vm_pgoff)
 * beyond (at a higher virtual address and file offset than) the vma.
 *
 * We cannot merge two vmas if they have differently assigned (non-NULL)
 * anon_vmas, nor if same anon_vma is assigned but offsets incompatible.
 */
static int
can_vma_merge_after(struct vm_area_struct *vma, unsigned long vm_flags,
		    struct anon_vma *anon_vma, struct file *file,
		    pgoff_t vm_pgoff,
		    struct vm_userfaultfd_ctx vm_userfaultfd_ctx)
{
	if (is_mergeable_vma(vma, file, vm_flags, vm_userfaultfd_ctx) &&
	    is_mergeable_anon_vma(anon_vma, vma->anon_vma, vma)) {
		pgoff_t vm_pglen;
		vm_pglen = vma_pages(vma);
		if (vma->vm_pgoff + vm_pglen == vm_pgoff)
			return 1;
	}
	return 0;
}

/*
 * Given a mapping request (addr,end,vm_flags,file,pgoff), figure out
 * whether that can be merged with its predecessor or its successor.
 * Or both (it neatly fills a hole).
 *
 * In most cases - when called for mmap, brk or mremap - [addr,end) is
 * certain not to be mapped by the time vma_merge is called; but when
 * called for mprotect, it is certain to be already mapped (either at
 * an offset within prev, or at the start of next), and the flags of
 * this area are about to be changed to vm_flags - and the no-change
 * case has already been eliminated.
 *
 * int mprotect(void *addr, size_t len, int prot);
 * 
 * The following mprotect cases have to be considered, where AAAA is
 * the area passed down from mprotect_fixup, never extending beyond one
 * vma, PPPPPP is the prev vma specified, and NNNNNN the next vma after:
 *
 *     AAAA             AAAA                   AAAA
 *    PPPPPPNNNNNN    PPPPPPNNNNNN       PPPPPPNNNNNN
 *    cannot merge    might become       might become
 *                    PPNNNNNNNNNN       PPPPPPPPPPNN
 *    mmap, brk or    case 4 below       case 5 below
 *    mremap move:
 *                        AAAA               AAAA
 *                    PPPP    NNNN       PPPPNNNNXXXX
 *                    might become       might become
 *                    PPPPPPPPPPPP 1 or  PPPPPPPPPPPP 6 or
 *                    PPPPPPPPNNNN 2 or  PPPPPPPPXXXX 7 or
 *                    PPPPNNNNNNNN 3     PPPPXXXXXXXX 8
 *
 * It is important for case 8 that the vma NNNN overlapping the
 * region AAAA is never going to extended over XXXX. Instead XXXX must
 * be extended in region AAAA and NNNN must be removed. This way in
 * all cases where vma_merge succeeds, the moment vma_adjust drops the
 * rmap_locks, the properties of the merged vma will be already
 * correct for the whole merged range. Some of those properties like
 * vm_page_prot/vm_flags may be accessed by rmap_walks and they must
 * be correct for the whole merged range immediately after the
 * rmap_locks are released. Otherwise if XXXX would be removed and
 * NNNN would be extended over the XXXX range, remove_migration_ptes
 * or other rmap walkers (if working on addresses beyond the "end"
 * parameter) may establish ptes with the wrong permissions of NNNN
 * instead of the right permissions of XXXX.
 */ /* vma 合并 */
struct vm_area_struct *vma_merge(struct mm_struct *mm,
			struct vm_area_struct *prev, unsigned long addr,
			unsigned long end, unsigned long vm_flags,
			struct anon_vma *anon_vma, struct file *file,
			pgoff_t pgoff, struct mempolicy *policy,
			struct vm_userfaultfd_ctx vm_userfaultfd_ctx)
{
	pgoff_t pglen = (end - addr) >> PAGE_SHIFT;
	struct vm_area_struct *area, *next;
	int err;

	/*
	 * We later require that vma->vm_flags == vm_flags,
	 * so this tests vma->vm_flags & VM_SPECIAL, too.
	 */
	if (vm_flags & VM_SPECIAL)
		return NULL;

	next = vma_next(mm, prev);
	area = next;
	if (area && area->vm_end == end)		/* cases 6, 7, 8 */
		next = next->vm_next;

	/* verify some invariant that must be enforced by the caller */
	VM_WARN_ON(prev && addr <= prev->vm_start);
	VM_WARN_ON(area && end > area->vm_end);
	VM_WARN_ON(addr >= end);

	/*
	 * Can it merge with the predecessor?
	 */
	if (prev && prev->vm_end == addr &&
			mpol_equal(vma_policy(prev), policy) &&
			can_vma_merge_after(prev, vm_flags,
					    anon_vma, file, pgoff,
					    vm_userfaultfd_ctx)) {
		/*
		 * OK, it can.  Can we now merge in the successor as well?
		 */
		if (next && end == next->vm_start &&
				mpol_equal(policy, vma_policy(next)) &&
				can_vma_merge_before(next, vm_flags,
						     anon_vma, file,
						     pgoff+pglen,
						     vm_userfaultfd_ctx) &&
				is_mergeable_anon_vma(prev->anon_vma,
						      next->anon_vma, NULL)) {
							/* cases 1, 6 */
			err = __vma_adjust(prev, prev->vm_start,
					 next->vm_end, prev->vm_pgoff, NULL,
					 prev);
		} else					/* cases 2, 5, 7 */
			err = __vma_adjust(prev, prev->vm_start,
					 end, prev->vm_pgoff, NULL, prev);
		if (err)
			return NULL;
		khugepaged_enter_vma_merge(prev, vm_flags);
		return prev;
	}

	/*
	 * Can this new request be merged in front of next?
	 */
	if (next && end == next->vm_start &&
			mpol_equal(policy, vma_policy(next)) &&
			can_vma_merge_before(next, vm_flags,
					     anon_vma, file, pgoff+pglen,
					     vm_userfaultfd_ctx)) {
		if (prev && addr < prev->vm_end)	/* case 4 */
			err = __vma_adjust(prev, prev->vm_start,
					 addr, prev->vm_pgoff, NULL, next);
		else {					/* cases 3, 8 */
			err = __vma_adjust(area, addr, next->vm_end,
					 next->vm_pgoff - pglen, NULL, next);
			/*
			 * In case 3 area is already equal to next and
			 * this is a noop, but in case 8 "area" has
			 * been removed and next was expanded over it.
			 */
			area = next;
		}
		if (err)
			return NULL;
		khugepaged_enter_vma_merge(area, vm_flags);
		return area;
	}

	return NULL;
}

/*
 * Rough compatibility check to quickly see if it's even worth looking
 * at sharing an anon_vma.
 *
 * They need to have the same vm_file, and the flags can only differ
 * in things that mprotect may change.
 *
 * NOTE! The fact that we share an anon_vma doesn't _have_ to mean that
 * we can merge the two vma's. For example, we refuse to merge a vma if
 * there is a vm_ops->close() function, because that indicates that the
 * driver is doing some kind of reference counting. But that doesn't
 * really matter for the anon_vma sharing case.
 *
 * 能复用的条件比较苛刻，如：
 *  VMA 必须相邻；
 *  VMA 内的 policy 相同
 *  相同的 vm_file
 */
static int anon_vma_compatible(struct vm_area_struct *a, struct vm_area_struct *b)
{
	return a->vm_end == b->vm_start &&  /* 起始地址相同 */
		mpol_equal(vma_policy(a), vma_policy(b)) && /* 策略相同 */
		a->vm_file == b->vm_file &&     /* 文件相同 */
		!((a->vm_flags ^ b->vm_flags) & ~(VM_ACCESS_FLAGS | VM_SOFTDIRTY)) &&       /*  */
		b->vm_pgoff == a->vm_pgoff + ((b->vm_start - a->vm_start) >> PAGE_SHIFT);   /* page offset */
}

/*
 * Do some basic sanity checking to see if we can re-use the anon_vma
 * from 'old'. The 'a'/'b' vma's are in VM order - one of them will be
 * the same as 'old', the other will be the new one that is trying
 * to share the anon_vma.
 *
 * NOTE! This runs with mm_sem held for reading, so it is possible that
 * the anon_vma of 'old' is concurrently in the process of being set up
 * by another page fault trying to merge _that_. But that's ok: if it
 * is being set up, that automatically means that it will be a singleton
 * acceptable for merging, so we can do all of this optimistically. But
 * we do that READ_ONCE() to make sure that we never re-load the pointer.
 *
 * IOW: that the "list_is_singular()" test on the anon_vma_chain only
 * matters for the 'stable anon_vma' case (ie the thing we want to avoid
 * is to return an anon_vma that is "complex" due to having gone through
 * a fork).
 *
 * We also make sure that the two vma's are compatible (adjacent,
 * and with the same memory policies). That's all stable, even with just
 * a read lock on the mm_sem.
 *
 * 能复用的条件比较苛刻，如：
 *  VMA 必须相邻；
 *  VMA 内的 policy 相同
 *  相同的 vm_file
 */
static struct anon_vma *reusable_anon_vma(struct vm_area_struct *old, struct vm_area_struct *a, struct vm_area_struct *b)
{
	if (anon_vma_compatible(a, b)) {
		struct anon_vma *anon_vma = READ_ONCE(old->anon_vma);

		if (anon_vma && list_is_singular(&old->anon_vma_chain))
			return anon_vma;
	}
	return NULL;
}

/*
 * find_mergeable_anon_vma is used by anon_vma_prepare, to check
 * neighbouring vmas for a suitable anon_vma, before it goes off
 * to allocate a new anon_vma.  It checks because a repetitive
 * sequence of mprotects and faults may otherwise lead to distinct
 * anon_vmas being allocated, preventing vma merge in subsequent
 * mprotect.
 *
 * 是否可以复用当前 VMA 的 near_vma 和 prev_vma 的 anon_vma 结构。
 *
 * 能复用的条件比较苛刻，如：
 *  VMA 必须相邻；
 *  VMA 内的 policy 相同
 *  相同的 vm_file
 */
struct anon_vma *find_mergeable_anon_vma(struct vm_area_struct *vma)    /*  */
{
	struct anon_vma *anon_vma = NULL;

	/* Try next first. */
	if (vma->vm_next) {
		anon_vma = reusable_anon_vma(vma->vm_next, vma, vma->vm_next);
		if (anon_vma)
			return anon_vma;
	}

	/* Try prev next. */
	if (vma->vm_prev)
		anon_vma = reusable_anon_vma(vma->vm_prev, vma->vm_prev, vma);

	/*
	 * We might reach here with anon_vma == NULL if we can't find
	 * any reusable anon_vma.
	 * There's no absolute need to look only at touching neighbours:
	 * we could search further afield for "compatible" anon_vmas.
	 * But it would probably just be a waste of time searching,
	 * or lead to too many vmas hanging off the same anon_vma.
	 * We're trying to allow mprotect remerging later on,
	 * not trying to minimize memory used for anon_vmas.
	 */
	return anon_vma;
}

/*
 * If a hint addr is less than mmap_min_addr change hint to be as
 * low as possible but still greater than mmap_min_addr
 */
static inline unsigned long round_hint_to_min(unsigned long hint)
{
	hint &= PAGE_MASK;

    /**
     * 如果地址不为空，并且 地址小于最小映射地址
     */
	if (((void *)hint != NULL) && (hint < mmap_min_addr))
		return PAGE_ALIGN(mmap_min_addr);
	return hint;
}

static inline int mlock_future_check(struct mm_struct *mm,
				     unsigned long flags,
				     unsigned long len)
{
	unsigned long locked, lock_limit;

	/*  mlock MCL_FUTURE? */
	if (flags & VM_LOCKED) {
		locked = len >> PAGE_SHIFT;
		locked += mm->locked_vm;
		lock_limit = rlimit(RLIMIT_MEMLOCK);
		lock_limit >>= PAGE_SHIFT;
		if (locked > lock_limit && !capable(CAP_IPC_LOCK))
			return -EAGAIN;
	}
	return 0;
}

static inline u64 file_mmap_size_max(struct file *file, struct inode *inode)
{
	if (S_ISREG(inode->i_mode))
		return MAX_LFS_FILESIZE;

	if (S_ISBLK(inode->i_mode))
		return MAX_LFS_FILESIZE;

	if (S_ISSOCK(inode->i_mode))
		return MAX_LFS_FILESIZE;

	/* Special "we do even unsigned file positions" case */
	if (file->f_mode & FMODE_UNSIGNED_OFFSET)
		return 0;

	/* Yes, random drivers might want more. But I'm tired of buggy drivers */
	return ULONG_MAX;
}

/* 指定的page offset 和len，需要在文件的合法长度内 */
static inline bool file_mmap_ok(struct file *file, struct inode *inode,
				unsigned long pgoff, unsigned long len)
{
	u64 maxsize = file_mmap_size_max(file, inode);

	if (maxsize && len > maxsize)
		return false;
	maxsize -= len;
	if (pgoff > maxsize >> PAGE_SHIFT)
		return false;
	return true;
}

/*
 * The caller must write-lock current->mm->mmap_lock.
 *  
 *  file : 文件映射打开的 文件，可能为 NULL
 *  addr: 用户传入的 虚拟地址，可能为 0
 */ /* mmap 映射 */
unsigned long do_mmap(struct file *file, unsigned long addr,
			unsigned long len, unsigned long prot,
			unsigned long flags, unsigned long pgoff,
			unsigned long *populate, struct list_head *uf/* userfaultfd?? */)
{
	struct mm_struct *mm = current->mm; /* 当前进程 mm 结构 */
	vm_flags_t vm_flags;
	int pkey = 0;

	*populate = 0;

    /* 长度合法 */
	if (!len)
		return -EINVAL;

	/*
	 * Does the application expect PROT_READ to imply PROT_EXEC?
	 *
	 * (the exception is when the underlying filesystem is noexec
	 *  mounted, in which case we dont add PROT_EXEC.)
	 * 
	 * PROT_EXEC  Pages may be executed.
     * PROT_READ  Pages may be read.
     * PROT_WRITE Pages may be written.
     * PROT_NONE  Pages may not be accessed.
	 */
	/*  */
	if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
		if (!(file && path_noexec(&file->f_path)))
			prot |= PROT_EXEC;

	/* force arch specific MAP_FIXED handling in get_unmapped_area */
	if (flags & MAP_FIXED_NOREPLACE)
		flags |= MAP_FIXED;

    /**
     * MAP_FIXED 将覆盖已映射的地址空间
     */
	if (!(flags & MAP_FIXED))
		addr = round_hint_to_min(addr);

	/* Careful about overflows.. 页对齐，最少映射一页
        给长度按page取整 */
	len = PAGE_ALIGN(len);
	if (!len)
		return -ENOMEM;

	/* offset overflow? len溢出 
	  判断page offset + 长度，是否已经溢出 */
	if ((pgoff + (len >> PAGE_SHIFT/* 12 */)) < pgoff)  /* 页偏移+长度 < pgoff,表明 len 溢出 */
		return -EOVERFLOW;

	/* Too many mappings? 
      判断本进程mmap的区段个数已经超标 */
	if (mm->map_count > sysctl_max_map_count)   /* 太多的map 数量 */
		return -ENOMEM;

	/**
	 *  这是 MMAP 的核心函数，下面还有个 `mmap_region()`
	 *
	 * Obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 *
	 * 从当前进程的用户地址空间中找出一块符合要求的空闲空间，给新的vma。
	 * 获取地址空间未被映射的区域，从本进程的线性地址红黑树中分配一块空白地址
	 */
	addr = get_unmapped_area(file, addr, len, pgoff, flags);
	if (IS_ERR_VALUE(addr))
		return addr;

    /**
     *  不能覆盖已经存在的地址空间 
     */
	if (flags & MAP_FIXED_NOREPLACE) {
        /* 查找最小的VMA，满足addr < vma->vm_end */
		struct vm_area_struct *vma = find_vma(mm, addr);

        /* 
        已存在
        +-------+ vma->vm_end
        |       |
        |       |
        |       | <---- addr + len
        | vma   |
        |       | addr
        |       |
        +-------+ vma->vm_start
        */
		if (vma && vma->vm_start < addr + len)
			return -EEXIST;
	}
    
    /**
     *  如果prot只指定了 PROT_EXEC(可执行的)
     *  TODO 2021年7月9日
     */
	if (prot == PROT_EXEC) {
		pkey = execute_only_pkey(mm);
		if (pkey < 0)
			pkey = 0;
	}

	/**
	 * Do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 *
	 * 计算权限 TODO 2021年7月9日
	 */
	vm_flags = calc_vm_prot_bits(prot, pkey) | calc_vm_flag_bits(flags) |
			mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

    //MAP_LOCKED：不会被swap出去
	if (flags & MAP_LOCKED)
		if (!can_do_mlock())
			return -EPERM;

    /* 如果指定了内存lock标志，但是lock的长度超标，出错返回 */
	if (mlock_future_check(mm, vm_flags, len))
		return -EAGAIN;

    /**
     *  文件映射 
     *
     * 文件内存映射的一系列判断和处理
     */
	if (file) { 
        /* 获取inode */
		struct inode *inode = file_inode(file); /* file -> inode */
		unsigned long flags_mask;

        /* 指定的page offset和len，需要在文件的合法长度内 */
		if (!file_mmap_ok(file, inode, pgoff, len))
			return -EOVERFLOW;

        /* 本文件支持的mask，和mmap()传递下来的flags进行判断 */
		flags_mask = LEGACY_MAP_MASK | file->f_op->mmap_supported_flags;

        /* 映射类型：私有，共享 */
		switch (flags & MAP_TYPE) {
        /* 共享映射 */
		case MAP_SHARED:    
			/*
			 * Force use of MAP_SHARED_VALIDATE with non-legacy
			 * flags. E.g. MAP_SYNC is dangerous to use with
			 * MAP_SHARED as you don't know which consistency model
			 * you will get. We silently ignore unsupported flags
			 * with MAP_SHARED to preserve backward compatibility.
			 */
			flags &= LEGACY_MAP_MASK;
			fallthrough;

        /* 共享&校验映射 */
		case MAP_SHARED_VALIDATE:
			if (flags & ~flags_mask)
				return -EOPNOTSUPP;
			if (prot & PROT_WRITE) {
                /* 写权限冲突 */
				if (!(file->f_mode & FMODE_WRITE))
					return -EACCES;
                /* 不能写映射交换文件 */
				if (IS_SWAPFILE(file->f_mapping->host))
					return -ETXTBSY;
			}

			/*
			 * Make sure we don't allow writing to an append-only
			 * file..
			 */
			/* 可添加 并且 可写 why?? */
			if (IS_APPEND(inode) && (file->f_mode & FMODE_WRITE))
				return -EACCES; /* 不允许 */

			/*
			 * Make sure there are no mandatory locks on the file.
			 */
			if (locks_verify_locked(file))
				return -EAGAIN;

            /* 将 vma 设置为 共享 */
			vm_flags |= VM_SHARED | VM_MAYSHARE;
			if (!(file->f_mode & FMODE_WRITE))
				vm_flags &= ~(VM_MAYWRITE | VM_SHARED);
			fallthrough;

        /* 私有映射 */
		case MAP_PRIVATE:   /* 私有 */
            /* 私有必须可读 */
			if (!(file->f_mode & FMODE_READ))
				return -EACCES;
			if (path_noexec(&file->f_path)) {
				if (vm_flags & VM_EXEC) /* vma 与 file  可执行权限不一样 */
					return -EPERM;      /* 返回没有权限 */
				vm_flags &= ~VM_MAYEXEC;/* 文件不可执行，vma也不可执行 */
			}

            /* 文件操作符 mmap 指针不能为空 */
			if (!file->f_op->mmap)  /* 如果 mmap 指针为空 */
				return -ENODEV;     /* 没有这个设备 */
			if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))   /* vma 向上还是向下 */
				return -EINVAL;
			break;

		default:
			return -EINVAL;
		}

    /**
     *  匿名映射
     *
     *  匿名内存映射的一系列判断和处理
     */
	} else {    /* 如果不是文件映射 */
		switch (flags & MAP_TYPE) {
		case MAP_SHARED:    /* 共享的 */
			if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))
				return -EINVAL;
			/*
			 * Ignore pgoff.
			 */
			pgoff = 0;  /* 共享匿名映射，忽略pageoffset，我估计必须为页对齐的原因 */
			vm_flags |= VM_SHARED | VM_MAYSHARE;
			break;
            
		case MAP_PRIVATE:   /* 私有的 */
			/*
			 * Set pgoff according to addr for anon_vma.
			 */
			pgoff = addr >> PAGE_SHIFT; /* page offset */
			break;
		default:
			return -EINVAL;
		}
	}

	/*
	 * Set 'VM_NORESERVE' if we should not account for the
	 * memory use of this mapping.
	 *
	 * 如果我们不应该考虑此映射的内存使用，则设置“ VM_NORESERVE”。
	 */
	if (flags & MAP_NORESERVE) {    /* 不要为此映射提供swap空间 */
		/* We honor MAP_NORESERVE if allowed to overcommit */
		if (sysctl_overcommit_memory != OVERCOMMIT_NEVER)
			vm_flags |= VM_NORESERVE;   /*  */

		/* hugetlb applies strict overcommit unless MAP_NORESERVE */
		if (file && is_file_hugepages(file))    /* 如果是大页内存 */
			vm_flags |= VM_NORESERVE;           /* 大页内存不能 swap 交换 */
	}
    
    /**
     *  核心 mmap 函数 , 上面还有个 `get_unmapped_area()`
     *
     *  根据查找到的地址、flags，正式在线性地址红黑树中插入一个新的VMAs
     */
	addr = mmap_region(file, addr, len, vm_flags, pgoff, uf);

    /* 默认只是分配vma，不进行实际的内存分配和mmu映射，延迟到page_fault时才处理
        如果设置了立即填充的标志，在分配vma时就分配好内存 */
	if (!IS_ERR_VALUE(addr) &&
	    ((vm_flags & VM_LOCKED) ||
	     (flags & (MAP_POPULATE | MAP_NONBLOCK)) == MAP_POPULATE))
		*populate = len;
	return addr;
}
            
/* SYSCALL_DEFINE6(mmap, ...) */
/* SYSCALL_DEFINE6(mmap_pgoff, ...) */
/**
 *  
 *  参数
 *  ------------------
 *  addr: 用户传入的虚拟地址
 *  len: 需要申请的长度
 *  
 */
unsigned long ksys_mmap_pgoff(unsigned long addr, unsigned long len,
			      unsigned long prot, unsigned long flags,
			      unsigned long fd, unsigned long pgoff)
{
	struct file *file = NULL;
	unsigned long retval;

    /**
     *  文件映射 
     *  
     */
	if (!(flags & MAP_ANONYMOUS)) { /* 如果不是匿名 */
        
		audit_mmap_fd(fd, flags);   /* 查找这个FD */
		file = fget(fd);            /* 获取 file 数据结构 */
        /* 没有打开的文件 */
		if (!file)
			return -EBADF;
        
        /* 大页文件 */
		if (is_file_hugepages(file)) {  /* 如果是 大页文件 映射的 内存 */
			len = ALIGN(len, huge_page_size(hstate_file(file)));
		} else if (unlikely(flags & MAP_HUGETLB)) { /* 如果不是大页内存，并且设置了 MAP_HUGETLB 标志位 */
			retval = -EINVAL;           /* 不可用的参数 */
			goto out_fput;
		}
	} 
    /**
     *  大页内存 
     */
    else if (flags & MAP_HUGETLB) {   /* 大页内存 */
	
		struct user_struct *user = NULL;
		struct hstate *hs;

		hs = hstate_sizelog((flags >> MAP_HUGE_SHIFT) & MAP_HUGE_MASK);
		if (!hs)
			return -EINVAL;

		len = ALIGN(len, huge_page_size(hs));   /* 大小 */
        
		/*
		 * VM_NORESERVE is used because the reservations will be
		 * taken when vm_ops->mmap() is called
		 * A dummy user value is used because we are not locking
		 * memory so no accounting is necessary
		 */
		file = hugetlb_file_setup(HUGETLB_ANON_FILE, len,
                  				VM_NORESERVE,
                  				&user, HUGETLB_ANONHUGE_INODE,
                  				(flags >> MAP_HUGE_SHIFT) & MAP_HUGE_MASK);
		if (IS_ERR(file))
			return PTR_ERR(file);
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE); /* 不允许执行 + 可以写 */

    /*  */
	retval = vm_mmap_pgoff(file, addr, len, prot, flags, pgoff);    /* 最终执行的 */
out_fput:
	if (file)
		fput(file);
	return retval;
}
                  
/* void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset); */
long mmap_pgoff(unsigned long addr, unsigned long len,
                        unsigned long prot, unsigned long flags,
                        unsigned long fd, unsigned long pgoff){/* +++ */}
SYSCALL_DEFINE6(mmap_pgoff, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, pgoff)
{
	return ksys_mmap_pgoff(addr, len, prot, flags, fd, pgoff);
}

#ifdef __ARCH_WANT_SYS_OLD_MMAP
struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

SYSCALL_DEFINE1(old_mmap, struct mmap_arg_struct __user *, arg)
{
	struct mmap_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	if (offset_in_page(a.offset))
		return -EINVAL;

	return ksys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd,
			       a.offset >> PAGE_SHIFT);
}
#endif /* __ARCH_WANT_SYS_OLD_MMAP */

/*
 * Some shared mappings will want the pages marked read-only
 * to track write events. If so, we'll downgrade vm_page_prot
 * to the private version (using protection_map[] without the
 * VM_SHARED bit).
 */
int vma_wants_writenotify(struct vm_area_struct *vma, pgprot_t vm_page_prot)
{
	vm_flags_t vm_flags = vma->vm_flags;
	const struct vm_operations_struct *vm_ops = vma->vm_ops;

	/* If it was private or non-writable, the write bit is already clear */
	if ((vm_flags & (VM_WRITE|VM_SHARED)) != ((VM_WRITE|VM_SHARED)))
		return 0;

	/* The backer wishes to know when pages are first written to? */
	if (vm_ops && (vm_ops->page_mkwrite || vm_ops->pfn_mkwrite))
		return 1;

	/* The open routine did something to the protections that pgprot_modify
	 * won't preserve? */
	if (pgprot_val(vm_page_prot) !=
	    pgprot_val(vm_pgprot_modify(vm_page_prot, vm_flags)))
		return 0;

	/* Do we need to track softdirty? */
	if (IS_ENABLED(CONFIG_MEM_SOFT_DIRTY) && !(vm_flags & VM_SOFTDIRTY))
		return 1;

	/* Specialty mapping? */
	if (vm_flags & VM_PFNMAP)
		return 0;

	/* Can the mapping track the dirty pages? */
	return vma->vm_file && vma->vm_file->f_mapping &&
		mapping_can_writeback(vma->vm_file->f_mapping);
}

/*
 * We account for memory if it's a private writeable mapping,
 * not hugepages and VM_NORESERVE wasn't set.
 */
static inline int accountable_mapping(struct file *file, vm_flags_t vm_flags)
{
	/*
	 * hugetlb has its own accounting separate from the core VM
	 * VM_HUGETLB may not be set yet so we cannot check for that flag.
	 */
	if (file && is_file_hugepages(file))
		return 0;

	return (vm_flags & (VM_NORESERVE | VM_SHARED | VM_WRITE)) == VM_WRITE;
}

/**
 *  核心 mmap 函数 , 上面还有个 `get_unmapped_area()`
 *
 *  根据查找到的地址、flags，正式在线性地址红黑树中插入一个新的VMAs
 *
 *  在 do_mmap() 中调用
 */
unsigned long mmap_region(struct file *file, unsigned long addr,
		unsigned long len, vm_flags_t vm_flags, unsigned long pgoff,
		struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev, *merge;
	int error;
	struct rb_node **rb_link, *rb_parent;
	unsigned long charged = 0;

	/* Check against address space limit. */
    /**
     *  判断地址空间大小是否已经超标
     *   总的空间：mm->total_vm + npages > rlimit(RLIMIT_AS) >> PAGE_SHIFT
     *   数据空间：mm->data_vm + npages > rlimit(RLIMIT_DATA) >> PAGE_SHIFT
     *
     *  当再 加上 `len >> PAGE_SHIFT` 页的时候，ulimit 还满足吗
     */
	if (!may_expand_vm(mm, vm_flags, len >> PAGE_SHIFT)) {

        /**
         * 如果 VM 不能扩展了(超出 ULimit 限制)
         */
		unsigned long nr_pages;

		/*
		 * MAP_FIXED may remove pages of mappings that intersects with
		 * requested mapping. Account for the pages it would unmap.
		 *
		 *  `MAP_FIXED`固定映射指定地址的情况下，地址空间可能和已有的VMA重叠，其他情况下不会重叠
         *  需要先unmap移除掉和新地址交错的vma地址
         *  所以可以先减去这部分空间，再判断大小是否超标
         *
         * 这个 接口计算 区间内的 page 数量
         */
		nr_pages = count_vma_pages_range(mm, addr, addr + len);

        /**
         *  判断地址空间大小是否已经超标
         *
         *  需要添加的 page - 已经存在的 page = 地址空间内 还可放的 page 数
         *  因为当为固定映射时候`MAP_FIXED`,需要先释放区间内已经映射的内存。
         *  如果能放下就行呗。
         */
		if (!may_expand_vm(mm, vm_flags, (len >> PAGE_SHIFT) - nr_pages))
			return -ENOMEM;
	}

	/**
	 *  Clear old maps, set up prev, rb_link, rb_parent, and uf. 清理old map
	 *
	 */
	if (munmap_vma_range(mm, addr, len, &prev, &rb_link, &rb_parent, uf))
		return -ENOMEM;
    
	/*
	 * Private writable mapping: check memory availability
	 *
	 * 私有可写映射：检查内存可用性
	 */
	if (accountable_mapping(file, vm_flags)) {
		charged = len >> PAGE_SHIFT;
		if (security_vm_enough_memory_mm(mm, charged))
			return -ENOMEM;
		vm_flags |= VM_ACCOUNT;
	}

	/*
	 * Can we just expand an old mapping?
	 *
	 * 可以直接扩充原有的 mapping 吗？
	 * 尝试和临近的vma进行merge，这个临近的 VMA 由 munmap_vma_range() 获得
	 *
	 * 如果合并成功，跳转到 out
	 */ 
	vma = vma_merge(mm, prev, addr, addr + len, vm_flags,
			NULL, file, pgoff, NULL, NULL_VM_UFFD_CTX);
	if (vma)
		goto out;

	/**
	 * 如果和 prev VMA 合并失败，将进行分配
	 *
	 * Determine the object being mapped and call the appropriate
	 * specific mapper. the address has already been validated, but
	 * not unmapped, but the maps are removed from the list.
	 *
	 * 分配新的vma结构体
	 */
	vma = vm_area_alloc(mm);/* 分配一个 vma 数据结构 */
	if (!vma) {
		error = -ENOMEM;
		goto unacct_error;
	}

    /* 填充VMA结构体相关成员 */
	vma->vm_start = addr;       /*  */
	vma->vm_end = addr + len;   /*  */
	vma->vm_flags = vm_flags;   /*  */
	vma->vm_page_prot = vm_get_page_prot(vm_flags);
	vma->vm_pgoff = pgoff;      /*  */

    /**
     *  文件内存映射 
     */
	if (file) { /* 文件映射 */
		if (vm_flags & VM_DENYWRITE) {
			error = deny_write_access(file);
			if (error)
				goto free_vma;
		}
		if (vm_flags & VM_SHARED) {
			error = mapping_map_writable(file->f_mapping);
			if (error)
				goto allow_write_and_free_vma;
		}

		/* ->mmap() can change vma->vm_file, but must guarantee that
		 * vma_link() below can deny write-access if VM_DENYWRITE is set
		 * and map writably if VM_SHARED is set. This usually means the
		 * new file must not have been exposed to user-space, yet.
		 */
		/* 给vma->vm_file赋值 */
		vma->vm_file = get_file(file);  /* 引用计数+1 */

        /**
         *  调用file->f_op->mmap，给vma->vm_ops赋值
         *  例如ext4：vma->vm_ops = &ext4_file_vm_ops;
         */
		error = call_mmap(file, vma);   /* 调用文件对应的 mmap, 可能是 shm_mmap() */
		if (error)
			goto unmap_and_free_vma;

		/* Can addr have changed??
		 *
		 * Answer: Yes, several device drivers can do it in their
		 *         f_op->mmap method. -DaveM
		 * Bug: If addr is changed, prev, rb_link, rb_parent should
		 *      be updated for vma_link()
		 */
		WARN_ON_ONCE(addr != vma->vm_start);

		addr = vma->vm_start;

		/* If vm_flags changed after call_mmap(), we should try merge vma again
		 * as we may succeed this time.
		 */
		if (unlikely(vm_flags != vma->vm_flags && prev)) {
			merge = vma_merge(mm, prev, vma->vm_start, vma->vm_end, vma->vm_flags,
				NULL, vma->vm_file, vma->vm_pgoff, NULL, NULL_VM_UFFD_CTX);
			if (merge) {
				/* ->mmap() can change vma->vm_file and fput the original file. So
				 * fput the vma->vm_file here or we would add an extra fput for file
				 * and cause general protection fault ultimately.
				 */
				fput(vma->vm_file);
				vm_area_free(vma);
				vma = merge;
				/* Update vm_flags to pick up the change. */
				vm_flags = vma->vm_flags;
				goto unmap_writable;
			}
		}

		vm_flags = vma->vm_flags;

    
	} 
    /**
     *  匿名共享内存映射 
     */
    else if (vm_flags & VM_SHARED) {
		error = shmem_zero_setup(vma);
		if (error)
			goto free_vma;
	} else {
		vma_set_anonymous(vma); 
	}

	/* Allow architectures to sanity-check the vm_flags */
	if (!arch_validate_flags(vma->vm_flags)) {
		error = -EINVAL;
		if (file)
			goto unmap_and_free_vma;
		else
			goto free_vma;
	}

    /**
     *  将新的vma插入 
     *
     *  1. 红黑树
     *  2. 链表
     *  3. 文件缓存/映射
     */
	vma_link(mm, vma, prev, rb_link, rb_parent);
    
	/* Once vma denies write, undo our temporary denial count */
	if (file) {
unmap_writable:
		if (vm_flags & VM_SHARED)
			mapping_unmap_writable(file->f_mapping);
		if (vm_flags & VM_DENYWRITE)
			allow_write_access(file);
	}
    
	file = vma->vm_file;    /*  */
    
out:
	perf_event_mmap(vma);   /*  */

	vm_stat_account(mm, vm_flags, len >> PAGE_SHIFT);
	if (vm_flags & VM_LOCKED) {
		if ((vm_flags & VM_SPECIAL) || vma_is_dax(vma) ||
					is_vm_hugetlb_page(vma) ||
					vma == get_gate_vma(current->mm))
			vma->vm_flags &= VM_LOCKED_CLEAR_MASK;
		else
			mm->locked_vm += (len >> PAGE_SHIFT);
	}

    /*  */
	if (file)
		uprobe_mmap(vma);   /*  */

	/*
	 * New (or expanded) vma always get soft dirty status.
	 * Otherwise user-space soft-dirty page tracker won't
	 * be able to distinguish situation when vma area unmapped,
	 * then new mapped in-place (which must be aimed as
	 * a completely new data area).
	 */
	vma->vm_flags |= VM_SOFTDIRTY;

	vma_set_page_prot(vma);

	return addr;

unmap_and_free_vma:
	vma->vm_file = NULL;
	fput(file);

	/* Undo any partial mapping done by a device driver. */
	unmap_region(mm, vma, prev, vma->vm_start, vma->vm_end);
	charged = 0;
	if (vm_flags & VM_SHARED)
		mapping_unmap_writable(file->f_mapping);
allow_write_and_free_vma:
	if (vm_flags & VM_DENYWRITE)
		allow_write_access(file);
free_vma:
	vm_area_free(vma);
unacct_error:
	if (charged)
		vm_unacct_memory(charged);
	return error;
}

/**
 * 
 *
 *  被`vm_unmapped_area()`调用
 */
static unsigned long unmapped_area(struct vm_unmapped_area_info *info)
{
	/*
	 * We implement the search by looking for an rbtree node that
	 * immediately follows a suitable gap(差距). That is,
	 *
	 * 我们查找红黑树，找到一个合适的洞。需要满足以下条件：
	 *
	 * - gap_start = vma->vm_prev->vm_end <= info->high_limit - length;
	 * - gap_end   = vma->vm_start        >= info->low_limit  + length;
	 * - gap_end - gap_start >= length
	 */
    /*
    +-------+ info->high_limit
    |       |
    |       | 
    |       | info->high_limit - length <-- vma->vm_prev->vm_end = gap_start
    |       |                           <-- vma->vm_prev->vm_end
    |       |
    |       |
    |       | 
    |       | 
    |       | 
    |       |
    |       |                           <-- vma->vm_start = gap_end
    |       | info->low_limit  + length <-- vma->vm_start
    |       |
    |       |
    +-------+ info->low_limit
    */

	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long length, low_limit, high_limit, gap_start, gap_end;

	/* Adjust search length to account for worst case alignment overhead */
    /* 长度加上mask开销 */
	length = info->length + info->align_mask;
	if (length < info->length)
		return -ENOMEM;

	/* Adjust search limits by the desired length */
    /* 计算high_limit，gap_start<=high_limit */
	if (info->high_limit < length)
		return -ENOMEM;
	high_limit = info->high_limit - length;

    /* 计算low_limit，gap_end>=low_limit */
	if (info->low_limit > high_limit)
		return -ENOMEM;
	low_limit = info->low_limit + length;

	/* Check if rbtree root looks promising */
    /* 如果红黑树为空 */
	if (RB_EMPTY_ROOT(&mm->mm_rb))
		goto check_highest;
	vma = rb_entry(mm->mm_rb.rb_node, struct vm_area_struct, vm_rb);

    /*
     * rb_subtree_gap的定义:
	 * Largest free memory gap in bytes to the left of this VMA. 
     * 此VMA左侧的最大可用内存空白（以字节为单位）。
	 * Either between this VMA and vma->vm_prev, or between one of the
	 * VMAs below us in the VMA rbtree and its ->vm_prev. This helps
	 * get_unmapped_area find a free area of the right size.
     * 在此VMA和vma-> vm_prev之间，或在VMA rbtree中我们下面的VMA之一与其-> vm_prev之间。 
     * 这有助于get_unmapped_area找到合适大小的空闲区域。
	 */
    /* 检查子树的 vma 地址差距 */
	if (vma->rb_subtree_gap < length)
		goto check_highest;

    /**
     *  查找红黑树根节点的左子树中是否有符合要求的空洞。
     *  有个疑问：
     *      根节点的右子树不需要搜索了吗？还是根节点没有右子树？
     */
	while (true) {
		/* Visit left subtree if it looks promising */
        /*  一直往左找，找到最左边有合适大小的节点
                因为最左边的地址最小
         */
		gap_end = vm_start_gap(vma);
		if (gap_end >= low_limit && vma->vm_rb.rb_left) {
			struct vm_area_struct *left =
				rb_entry(vma->vm_rb.rb_left,
					 struct vm_area_struct, vm_rb);
			if (left->rb_subtree_gap >= length) {   /* 尝试找到一个可以装下 length 的子树 */
				vma = left;
				continue;
			}
		}

		gap_start = vma->vm_prev ? vm_end_gap(vma->vm_prev) : 0;
check_current:
		/* Check if current node has a suitable gap */
		if (gap_start > high_limit)
			return -ENOMEM;

        /* 如果已找到合适的洞，则跳出循环 */
		if (gap_end >= low_limit &&
		    gap_end > gap_start && gap_end - gap_start >= length)
			goto found;

		/* Visit right subtree if it looks promising */
        /* 如果左子树查找失败，从当前vm的右子树查找 */
		if (vma->vm_rb.rb_right) {
			struct vm_area_struct *right =
				rb_entry(vma->vm_rb.rb_right,
					 struct vm_area_struct, vm_rb);
			if (right->rb_subtree_gap >= length) {
				vma = right;
				continue;
			}
		}

		/* Go back up the rbtree to find next candidate node */
        /* 如果左右子树都搜寻失败，向回搜寻父节点 */
		while (true) {
			struct rb_node *prev = &vma->vm_rb;
			if (!rb_parent(prev))
				goto check_highest;
			vma = rb_entry(rb_parent(prev),
				       struct vm_area_struct, vm_rb);
			if (prev == vma->vm_rb.rb_left) {
				gap_start = vm_end_gap(vma->vm_prev);
				gap_end = vm_start_gap(vma);
				goto check_current;
			}
		}
	}

    /**
     *  如果红黑树中没有合适的空洞，从highest空间查找是否有合适的
     *  highest空间是还没有vma分配的空白空间
     *  但是优先查找已分配vma之间的空洞
     */
check_highest:
	/* Check highest gap, which does not precede any rbtree node */
	gap_start = mm->highest_vm_end;
	gap_end = ULONG_MAX;  /* Only for VM_BUG_ON below */
	if (gap_start > high_limit)
		return -ENOMEM;

    /* 搜索到了合适的空间，返回开始地址 */
found:
	/* We found a suitable gap. Clip it with the original low_limit. */
	if (gap_start < info->low_limit)
		gap_start = info->low_limit;

	/* Adjust gap address to the desired alignment */
	gap_start += (info->align_offset - gap_start) & info->align_mask;

	VM_BUG_ON(gap_start + info->length > info->high_limit);
	VM_BUG_ON(gap_start + info->length > gap_end);
	return gap_start;
}

/**
 * 
 *
 *  被`vm_unmapped_area()`调用
 */
static unsigned long unmapped_area_topdown(struct vm_unmapped_area_info *info)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long length, low_limit, high_limit, gap_start, gap_end;

	/* Adjust search length to account for worst case alignment overhead */
	length = info->length + info->align_mask;
	if (length < info->length)
		return -ENOMEM;

	/*
	 * Adjust search limits by the desired length.
	 * See implementation comment at top of unmapped_area().
	 *
	 * 
	 */
	gap_end = info->high_limit; /*  */
	if (gap_end < length)
		return -ENOMEM;
    
	high_limit = gap_end - length;

	if (info->low_limit > high_limit)
		return -ENOMEM;
    
	low_limit = info->low_limit + length;

	/* Check highest gap, which does not precede any rbtree node */
	gap_start = mm->highest_vm_end;
	if (gap_start <= high_limit)
		goto found_highest;

	/* Check if rbtree root looks promising */
	if (RB_EMPTY_ROOT(&mm->mm_rb))
		return -ENOMEM;
    
	vma = rb_entry(mm->mm_rb.rb_node, struct vm_area_struct, vm_rb);
	if (vma->rb_subtree_gap < length)
		return -ENOMEM;

	while (true) {
		/* Visit right subtree if it looks promising */
		gap_start = vma->vm_prev ? vm_end_gap(vma->vm_prev) : 0;
		if (gap_start <= high_limit && vma->vm_rb.rb_right) {
			struct vm_area_struct *right =
				rb_entry(vma->vm_rb.rb_right,
					 struct vm_area_struct, vm_rb);
			if (right->rb_subtree_gap >= length) {
				vma = right;
				continue;
			}
		}

check_current:
		/* Check if current node has a suitable gap */
		gap_end = vm_start_gap(vma);
		if (gap_end < low_limit)
			return -ENOMEM;
		if (gap_start <= high_limit &&
		    gap_end > gap_start && gap_end - gap_start >= length)
			goto found;

		/* Visit left subtree if it looks promising */
		if (vma->vm_rb.rb_left) {
			struct vm_area_struct *left =
				rb_entry(vma->vm_rb.rb_left,
					 struct vm_area_struct, vm_rb);
			if (left->rb_subtree_gap >= length) {
				vma = left;
				continue;
			}
		}

		/* Go back up the rbtree to find next candidate node */
		while (true) {
			struct rb_node *prev = &vma->vm_rb;
			if (!rb_parent(prev))
				return -ENOMEM;
			vma = rb_entry(rb_parent(prev),
				       struct vm_area_struct, vm_rb);
			if (prev == vma->vm_rb.rb_right) {
				gap_start = vma->vm_prev ?
					vm_end_gap(vma->vm_prev) : 0;
				goto check_current;
			}
		}
	}

found:
	/* We found a suitable gap. Clip it with the original high_limit. */
	if (gap_end > info->high_limit)
		gap_end = info->high_limit;

found_highest:
	/* Compute highest gap address at the desired alignment */
	gap_end -= info->length;
	gap_end -= (gap_end - info->align_offset) & info->align_mask;

	VM_BUG_ON(gap_end < info->low_limit);
	VM_BUG_ON(gap_end < gap_start);
	return gap_end;
}

/*
 * Search for an unmapped address range.
 *
 * We are looking for a range that:
 * - does not intersect with any VMA;
 * - is contained within the [low_limit, high_limit) interval;
 * - is at least the desired size.
 * - satisfies (begin_addr & align_mask) == (align_offset & align_mask)
 *
 * 我们正在寻找以下范围：
 *  -不与任何VMA相交；
 *  -包含在[low_limit，high_limit）间隔内；
 *  -至少是所需的大小。
 *  -满足（begin_addr和align_mask）==（align_offset和align_mask）
 *
 * https://rtoax.blog.csdn.net/article/details/118602363
 */
unsigned long vm_unmapped_area(struct vm_unmapped_area_info *info)
{
	unsigned long addr;

    /**
     *  从高往低查找 (新的 modern mmap 模式)
     */
	if (info->flags & VM_UNMAPPED_AREA_TOPDOWN)
		addr = unmapped_area_topdown(info);
    
    /**
     *  从低往高查找 (过时的 legacy mmap 模式)
     */
	else
		addr = unmapped_area(info);

	trace_vm_unmapped_area(addr, info);
	return addr;
}

#ifndef arch_get_mmap_end
#define arch_get_mmap_end(addr)	(TASK_SIZE)
#endif

#ifndef arch_get_mmap_base
#define arch_get_mmap_base(addr, base) (base)
#endif

/* Get an address range which is currently unmapped.
 * For shmat() with addr=0.
 *
 * Ugly calling convention alert:
 * Return value with the low bits set means error value,
 * ie
 *	if (ret & ~PAGE_MASK)
 *		error = ret;
 *
 * This function "knows" that -ENOMEM has the bits set.
 */
#ifndef HAVE_ARCH_UNMAPPED_AREA
//unsigned long
//arch_get_unmapped_area(struct file *filp, unsigned long addr,
//		unsigned long len, unsigned long pgoff, unsigned long flags)
//{
//	struct mm_struct *mm = current->mm;
//	struct vm_area_struct *vma, *prev;
//	struct vm_unmapped_area_info info;
//	const unsigned long mmap_end = arch_get_mmap_end(addr);
//
//	if (len > mmap_end - mmap_min_addr)
//		return -ENOMEM;
//
//	if (flags & MAP_FIXED)
//		return addr;
//
//	if (addr) {
//		addr = PAGE_ALIGN(addr);
//		vma = find_vma_prev(mm, addr, &prev);
//		if (mmap_end - len >= addr && addr >= mmap_min_addr &&
//		    (!vma || addr + len <= vm_start_gap(vma)) &&
//		    (!prev || addr >= vm_end_gap(prev)))
//			return addr;
//	}
//
//	info.flags = 0;
//	info.length = len;
//	info.low_limit = mm->mmap_base;
//	info.high_limit = mmap_end;
//	info.align_mask = 0;
//	info.align_offset = 0;
//	return vm_unmapped_area(&info);
//}
#endif

/*
 * This mmap-allocator allocates new areas top-down from below the
 * stack's low limit (the base):
 */
#ifndef HAVE_ARCH_UNMAPPED_AREA_TOPDOWN
//unsigned long
//arch_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
//			  unsigned long len, unsigned long pgoff,
//			  unsigned long flags)
//{
//	struct vm_area_struct *vma, *prev;
//	struct mm_struct *mm = current->mm;
//	struct vm_unmapped_area_info info;
//	const unsigned long mmap_end = arch_get_mmap_end(addr);
//
//	/* requested length too big for entire address space */
//	if (len > mmap_end - mmap_min_addr)
//		return -ENOMEM;
//
//	if (flags & MAP_FIXED)
//		return addr;
//
//	/* requesting a specific address */
//	if (addr) {
//		addr = PAGE_ALIGN(addr);
//		vma = find_vma_prev(mm, addr, &prev);
//		if (mmap_end - len >= addr && addr >= mmap_min_addr &&
//				(!vma || addr + len <= vm_start_gap(vma)) &&
//				(!prev || addr >= vm_end_gap(prev)))
//			return addr;
//	}
//
//	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
//	info.length = len;
//	info.low_limit = max(PAGE_SIZE, mmap_min_addr);
//	info.high_limit = arch_get_mmap_base(addr, mm->mmap_base);
//	info.align_mask = 0;
//	info.align_offset = 0;
//	addr = vm_unmapped_area(&info);
//
//	/*
//	 * A failed mmap() very likely causes application failure,
//	 * so fall back to the bottom-up function here. This scenario
//	 * can happen with large stack limits and large mmap()
//	 * allocations.
//	 */
//	if (offset_in_page(addr)) {
//		VM_BUG_ON(addr != -ENOMEM);
//		info.flags = 0;
//		info.low_limit = TASK_UNMAPPED_BASE;
//		info.high_limit = mmap_end;
//		addr = vm_unmapped_area(&info);
//	}
//
//	return addr;
//}
#endif


/**
 *  这是 MMAP 的核心函数，下面还有个 `mmap_region()`
 *
 * Obtain the address to map to. we verify (or select) it and ensure
 * that it represents a valid section of the address space.
 *
 * 从当前进程的用户地址空间中找出一块符合要求的空闲空间，给新的vma。
 * 获取地址空间未被映射的区域，从本进程的线性地址红黑树中分配一块空白地址
 *
 * 参数列表
 * ---------------------------------------
 *  file: 文件映射打开的文件，可能为 NULL
 *  addr: 
 */
unsigned long
get_unmapped_area(struct file *file, unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	unsigned long (*get_area)(struct file *, unsigned long,
				  unsigned long, unsigned long, unsigned long);

	unsigned long error = arch_mmap_check(addr, len, flags);    /* x86 为 0 */
	if (error)
		return error;

	/* Careful about overflows.. Sanity check, make sure the required map length is not too long*/
	if (len > TASK_SIZE/*0x0000 7fff ffff f000 约128T*/)    /* len 错误 */
		return -ENOMEM;

    /**
     *  可能是malloc() 分配大于等于128KB的内存空间
     *
     *  arch_get_unmapped_area() 在`arch_pick_mmap_layout()`中赋值
     */
	get_area = current->mm->get_unmapped_area;  

    /* 文件映射，一个打开的文件 */
	if (file) { 
		if (file->f_op->get_unmapped_area)
            /**
             *  文件映射，指向文件对应的 f_op。
             *  文件映射对应的 offset，所以 pgoff 不需要清零
             *  文件内存映射，且文件有自己的get_unmapped_area，则使用file->f_op->get_unmapped_area
             */
			get_area = file->f_op->get_unmapped_area;   

    /* 共享内存，但是没有打开的文件 */
	} else if (flags & MAP_SHARED) {
		/*
		 * mmap_region() will call shmem_zero_setup() to create a file,
		 * so use shmem's get_unmapped_area in case it can be huge.
		 * do_mmap() will clear pgoff, so match alignment.
		 *
		 *  如果不是文件映射，并且是共享的，这种很可能是父子进程之间共享内存
		 *  匿名共享内存映射，使用shmem_get_unmapped_area函数
		 */
		pgoff = 0;
		get_area = shmem_get_unmapped_area; /* 共享 */
	}

    /**
     *  实际的获取线性区域
     *
     *  在`arch_pick_mmap_layout()`中赋值
     *      arch_get_unmapped_area()            == legacy 模式
     *      arch_get_unmapped_area_topdown()    == modern 模式
     *
     *  也可能为`shmem_get_unmapped_area()`
     */
	addr = get_area(file, addr, len, pgoff, flags);
	if (IS_ERR_VALUE(addr))
		return addr;

    /* 地址过大 */
	if (addr > TASK_SIZE/*  */ - len)
		return -ENOMEM;

    /* 页偏移 不为0 */
	if (offset_in_page(addr))   /* 必须页对齐，低12bit为0 */
		return -EINVAL;

    /*  */
	error = security_mmap_addr(addr);
	return error ? error : addr;
}

EXPORT_SYMBOL(get_unmapped_area);

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
/* 查找最小的VMA，满足addr < vma->vm_end */
struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)   /* 从红黑树中查找 */
{
	struct rb_node *rb_node;
	struct vm_area_struct *vma;

	/* Check the cache first. 
     *  查找vma cache，是否有vma的区域能包含addr地址
     */
	vma = vmacache_find(mm, addr);
	if (likely(vma))
		return vma;

	rb_node = mm->mm_rb.rb_node;

    /* 查找vma红黑树，是否有vma的区域能包含addr地址 */
	while (rb_node) {
		struct vm_area_struct *tmp;

		tmp = rb_entry(rb_node, struct vm_area_struct, vm_rb);

		if (tmp->vm_end > addr) {
			vma = tmp;
			if (tmp->vm_start <= addr)  /* 停止条件 ， 这里和 从 vmacache 中查找是一样的 */
				break;
			rb_node = rb_node->rb_left;
		} else
			rb_node = rb_node->rb_right;
	}

    /* 利用查找到的vma来更新vma cache */
	if (vma)
		vmacache_update(addr, vma); /* 更新到 vma 缓存 中 */
	return vma;
}

EXPORT_SYMBOL(find_vma);

/*
 * Same as find_vma, but also return a pointer to the previous VMA in *pprev.
 */
struct vm_area_struct *
find_vma_prev(struct mm_struct *mm, unsigned long addr,
			struct vm_area_struct **pprev)
{
	struct vm_area_struct *vma;

	vma = find_vma(mm, addr);
	if (vma) {
		*pprev = vma->vm_prev;
	} else {
		struct rb_node *rb_node = rb_last(&mm->mm_rb);

		*pprev = rb_node ? rb_entry(rb_node, struct vm_area_struct, vm_rb) : NULL;
	}
	return vma;
}

/*
 * Verify that the stack growth is acceptable and
 * update accounting. This is shared with both the
 * grow-up and grow-down cases.
 */
static int acct_stack_growth(struct vm_area_struct *vma,
			     unsigned long size, unsigned long grow)    /*  */
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long new_start;

	/* address space limit tests */
	if (!may_expand_vm(mm, vma->vm_flags, grow))
		return -ENOMEM;

	/* Stack limit test */
	if (size > rlimit(RLIMIT_STACK))
		return -ENOMEM;

	/* mlock limit tests */
	if (vma->vm_flags & VM_LOCKED) {
		unsigned long locked;
		unsigned long limit;
		locked = mm->locked_vm + grow;
		limit = rlimit(RLIMIT_MEMLOCK);
		limit >>= PAGE_SHIFT;
		if (locked > limit && !capable(CAP_IPC_LOCK))
			return -ENOMEM;
	}

	/* Check to ensure the stack will not grow into a hugetlb-only region */
	new_start = (vma->vm_flags & VM_GROWSUP) ? vma->vm_start :
			vma->vm_end - size;
	if (is_hugepage_only_range(vma->vm_mm, new_start, size))
		return -EFAULT;

	/*
	 * Overcommit..  This must be the final test, as it will
	 * update security statistics.
	 */
	if (security_vm_enough_memory_mm(mm, grow))
		return -ENOMEM;

	return 0;
}

#if defined(CONFIG_STACK_GROWSUP) || defined(CONFIG_IA64)
///*
// * PA-RISC uses this for its stack; IA64 for its Register Backing Store.
// * vma is the last one with address > vma->vm_end.  Have to extend vma.
// */
//int expand_upwards(struct vm_area_struct *vma, unsigned long address)
//{
//	struct mm_struct *mm = vma->vm_mm;
//	struct vm_area_struct *next;
//	unsigned long gap_addr;
//	int error = 0;
//
//	if (!(vma->vm_flags & VM_GROWSUP))
//		return -EFAULT;
//
//	/* Guard against exceeding limits of the address space. */
//	address &= PAGE_MASK;
//	if (address >= (TASK_SIZE & PAGE_MASK))
//		return -ENOMEM;
//	address += PAGE_SIZE;
//
//	/* Enforce stack_guard_gap */
//	gap_addr = address + stack_guard_gap;
//
//	/* Guard against overflow */
//	if (gap_addr < address || gap_addr > TASK_SIZE)
//		gap_addr = TASK_SIZE;
//
//	next = vma->vm_next;
//	if (next && next->vm_start < gap_addr && vma_is_accessible(next)) {
//		if (!(next->vm_flags & VM_GROWSUP))
//			return -ENOMEM;
//		/* Check that both stack segments have the same anon_vma? */
//	}
//
//	/* We must make sure the anon_vma is allocated. */
//	if (unlikely(anon_vma_prepare(vma)))
//		return -ENOMEM;
//
//	/*
//	 * vma->vm_start/vm_end cannot change under us because the caller
//	 * is required to hold the mmap_lock in read mode.  We need the
//	 * anon_vma lock to serialize against concurrent expand_stacks.
//	 */
//	anon_vma_lock_write(vma->anon_vma);
//
//	/* Somebody else might have raced and expanded it already */
//	if (address > vma->vm_end) {
//		unsigned long size, grow;
//
//		size = address - vma->vm_start;
//		grow = (address - vma->vm_end) >> PAGE_SHIFT;
//
//		error = -ENOMEM;
//		if (vma->vm_pgoff + (size >> PAGE_SHIFT) >= vma->vm_pgoff) {
//			error = acct_stack_growth(vma, size, grow);
//			if (!error) {
//				/*
//				 * vma_gap_update() doesn't support concurrent
//				 * updates, but we only hold a shared mmap_lock
//				 * lock here, so we need to protect against
//				 * concurrent vma expansions.
//				 * anon_vma_lock_write() doesn't help here, as
//				 * we don't guarantee that all growable vmas
//				 * in a mm share the same root anon vma.
//				 * So, we reuse mm->page_table_lock to guard
//				 * against concurrent vma expansions.
//				 */
//				spin_lock(&mm->page_table_lock);
//				if (vma->vm_flags & VM_LOCKED)
//					mm->locked_vm += grow;
//				vm_stat_account(mm, vma->vm_flags, grow);
//				anon_vma_interval_tree_pre_update_vma(vma);
//				vma->vm_end = address;
//				anon_vma_interval_tree_post_update_vma(vma);
//				if (vma->vm_next)
//					vma_gap_update(vma->vm_next);
//				else
//					mm->highest_vm_end = vm_end_gap(vma);
//				spin_unlock(&mm->page_table_lock);
//
//				perf_event_mmap(vma);
//			}
//		}
//	}
//	anon_vma_unlock_write(vma->anon_vma);
//	khugepaged_enter_vma_merge(vma, vma->vm_flags);
//	validate_mm(mm);
//	return error;
//}
#endif /* CONFIG_STACK_GROWSUP || CONFIG_IA64 */

/*
 * vma is the first one with address < vma->vm_start.  Have to extend vma.
 *  扩展 VMA
 *
 *  情景1：
 *  =======================================================
 *  vm_start > addr 
 *
 *          vm_start    vm_end
 *             |          |
 *  +----------+##########+------+
 *        ^
 *        |
 *       addr
 *
 *  expand_stack 执行后：==>
 *  
 *  vm_start > addr 
 *
 *       start
 *        |
 *     vm_start         vm_end
 *        |               |
 *  +-----+###############+------+
 *        ^
 *        |
 *       addr
 */

int expand_downwards(struct vm_area_struct *vma, unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *prev;
	int error = 0;

	address &= PAGE_MASK;   /* 确认一下，页偏移部分为 0 */
    
	if (address < mmap_min_addr)    /* 地址错误，返回权限错误 */
		return -EPERM;

	/* Enforce stack_guard_gap */
	prev = vma->vm_prev;
    
	/* Check that both stack segments have the same anon_vma? */
	if (prev && !(prev->vm_flags & VM_GROWSDOWN) &&
			vma_is_accessible(prev)) {
		if (address - prev->vm_end < stack_guard_gap)
			return -ENOMEM;
	}

	/* We must make sure the anon_vma is allocated. */
	if (unlikely(anon_vma_prepare(vma)))
		return -ENOMEM;

	/*
	 * vma->vm_start/vm_end cannot change under us because the caller
	 * is required to hold the mmap_lock in read mode.  We need the
	 * anon_vma lock to serialize against concurrent expand_stacks.
	 *
	 * 
	 */
	anon_vma_lock_write(vma->anon_vma);

	/**
	 *  Somebody else might have raced and expanded it already 
	 *
     *  
     *  vm_start > addr 
     *
     *          vm_start    vm_end
     *             |          |
     *  +-----+====+##########+------+
     *        ^               |
     *        |               |
     *       addr             |
     *        |               |
     *        |<----size----->|
     *        |<-->| grow
     */
	if (address < vma->vm_start) {  /* 小于 */
        
		unsigned long size, grow;

        /* 计算总大小 */
		size = vma->vm_end - address;

        /* 需要增加的大小 */
		grow = (vma->vm_start - address) >> PAGE_SHIFT;

		error = -ENOMEM;
		if (grow <= vma->vm_pgoff) {    /* 小于在 page 中的偏移量 */
            
			error = acct_stack_growth(vma, size, grow); /* 鉴权与资源限制检测 */
			if (!error) {   /* 没出错 */
				/*
				 * vma_gap_update() doesn't support concurrent
				 * updates, but we only hold a shared mmap_lock
				 * lock here, so we need to protect against
				 * concurrent vma expansions.
				 * anon_vma_lock_write() doesn't help here, as
				 * we don't guarantee that all growable vmas
				 * in a mm share the same root anon vma.
				 * So, we reuse mm->page_table_lock to guard
				 * against concurrent vma expansions.
				 */
				spin_lock(&mm->page_table_lock);
                
				if (vma->vm_flags & VM_LOCKED)
					mm->locked_vm += grow;
				vm_stat_account(mm, vma->vm_flags, grow);
				anon_vma_interval_tree_pre_update_vma(vma);
				vma->vm_start = address;    /* 赋值 */
				vma->vm_pgoff -= grow;      /*  */
				anon_vma_interval_tree_post_update_vma(vma);
				vma_gap_update(vma);
                
				spin_unlock(&mm->page_table_lock);

				perf_event_mmap(vma);
			}
		}
	}
    
	anon_vma_unlock_write(vma->anon_vma);
	khugepaged_enter_vma_merge(vma, vma->vm_flags);
	validate_mm(mm);
	return error;
}

/* enforced gap between the expanding stack and other mappings. */
unsigned long stack_guard_gap = 256UL<<PAGE_SHIFT/* 12 */;/* 默认 256 个 page 大小 */

static int __init cmdline_parse_stack_guard_gap(char *p)    /* 可以重新设置 gap 大小 */
{
	unsigned long val;
	char *endptr;

	val = simple_strtoul(p, &endptr, 10);
	if (!*endptr)
		stack_guard_gap = val << PAGE_SHIFT;

	return 0;
}
__setup("stack_guard_gap=", cmdline_parse_stack_guard_gap);

#ifdef CONFIG_STACK_GROWSUP
//int expand_stack(struct vm_area_struct *vma, unsigned long address)
//{
//	return expand_upwards(vma, address);
//}
//
//struct vm_area_struct *
//find_extend_vma(struct mm_struct *mm, unsigned long addr)
//{
//	struct vm_area_struct *vma, *prev;
//
//	addr &= PAGE_MASK;
//	vma = find_vma_prev(mm, addr, &prev);
//	if (vma && (vma->vm_start <= addr))
//		return vma;
//	/* don't alter vm_end if the coredump is running */
//	if (!prev || expand_stack(prev, addr))
//		return NULL;
//	if (prev->vm_flags & VM_LOCKED)
//		populate_vma_page_range(prev, addr, prev->vm_end, NULL);
//	return prev;
//}
#else

/**
 *  扩展 VMA
 *
 *  情景1：
 *  ======================================================
 *  vm_start > addr 
 *
 *          vm_start    vm_end
 *             |          |
 *  +----------+##########+------+
 *        ^
 *        |
 *       addr
 *
 *  expand_stack 执行后：==>
 *  
 *  vm_start > addr 
 *
 *       start
 *        |
 *     vm_start         vm_end
 *        |               |
 *  +-----+###############+------+
 *        ^
 *        |
 *       addr
 *
 */
int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	return expand_downwards(vma, address);  /*  */
}

struct vm_area_struct *
find_extend_vma(struct mm_struct *mm, unsigned long addr)   /*  */
{
	struct vm_area_struct *vma;
	unsigned long start;

	addr &= PAGE_MASK;  /* 将偏移部分置零 */
    
	vma = find_vma(mm, addr);   /* 查找 vma */
	if (!vma)
		return NULL;    /* 没找到，直接返回 NULL */
    
	if (vma->vm_start <= addr)  /* 在 vma 范围内 */
		return vma;

    /**
     *  vm_start > addr 
     *
     *          vm_start    vm_end
     *             |          |
     *  +----------+##########+------+
     *        ^
     *        |
     *       addr
     */
	if (!(vma->vm_flags & VM_GROWSDOWN))    /* 如果不是向下增长的，直接返回错误 */
		return NULL;


    /**
     *  vm_start > addr 
     *
     *           start
     *             |
     *          vm_start    vm_end
     *             |          |
     *  +----------+##########+------+
     *        ^
     *        |
     *       addr
     */
	start = vma->vm_start;
    
	if (expand_stack(vma, addr))    /* 扩展 */
		return NULL;
    /**
     *  expand_stack 执行后
     *  
     *  vm_start > addr 
     *
     *       start
     *        |
     *     vm_start         vm_end
     *        |               |
     *  +-----+###############+------+
     *        ^
     *        |
     *       addr
     */

    
	if (vma->vm_flags & VM_LOCKED)  /* 如果锁定， mlock -> 人为制造缺页异常 */
		populate_vma_page_range(vma, addr, start, NULL);
    
	return vma;
}
#endif

EXPORT_SYMBOL_GPL(find_extend_vma);

/*
 * Ok - we have the memory areas we should free on the vma list,
 * so release them, and do the vma updates.
 *
 * Called with the mm semaphore held.
 */
static void remove_vma_list(struct mm_struct *mm, struct vm_area_struct *vma)
{
	unsigned long nr_accounted = 0;

	/* Update high watermark before we lower total_vm */
	update_hiwater_vm(mm);
	do {
		long nrpages = vma_pages(vma);/*  */

		if (vma->vm_flags & VM_ACCOUNT)
			nr_accounted += nrpages;
		vm_stat_account(mm, vma->vm_flags, -nrpages);
		vma = remove_vma(vma);  /* 释放内存 */
	} while (vma);
	vm_unacct_memory(nr_accounted);
	validate_mm(mm);    /*  */
}

/*
 * Get rid of page table information in the indicated region.
 *
 * Called with the mm semaphore held.
 */ /*  */
static void unmap_region(struct mm_struct *mm,
		struct vm_area_struct *vma, struct vm_area_struct *prev,
		unsigned long start, unsigned long end)
{
	struct vm_area_struct *next = vma_next(mm, prev);
	struct mmu_gather tlb;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm, start, end);
	update_hiwater_rss(mm);
	unmap_vmas(&tlb, vma, start, end);
	free_pgtables(&tlb, vma, prev ? prev->vm_end : FIRST_USER_ADDRESS,
				 next ? next->vm_start : USER_PGTABLES_CEILING);
	tlb_finish_mmu(&tlb, start, end);
}

/*
 * Create a list of vma's touched by the unmap, removing them from the mm's
 * vma list as we go..
 */
static bool
detach_vmas_to_be_unmapped(struct mm_struct *mm, struct vm_area_struct *vma,
	struct vm_area_struct *prev, unsigned long end)
{
	struct vm_area_struct **insertion_point;
	struct vm_area_struct *tail_vma = NULL;

	insertion_point = (prev ? &prev->vm_next : &mm->mmap);
	vma->vm_prev = NULL;
	do {
		vma_rb_erase(vma, &mm->mm_rb);
		mm->map_count--;
		tail_vma = vma;
		vma = vma->vm_next;
	} while (vma && vma->vm_start < end);
	*insertion_point = vma;
	if (vma) {
		vma->vm_prev = prev;
		vma_gap_update(vma);
	} else
		mm->highest_vm_end = prev ? vm_end_gap(prev) : 0;
	tail_vma->vm_next = NULL;

	/* Kill the cache */
	vmacache_invalidate(mm);

	/*
	 * Do not downgrade mmap_lock if we are next to VM_GROWSDOWN or
	 * VM_GROWSUP VMA. Such VMAs can change their size under
	 * down_read(mmap_lock) and collide with the VMA we are about to unmap.
	 */
	if (vma && (vma->vm_flags & VM_GROWSDOWN))
		return false;
	if (prev && (prev->vm_flags & VM_GROWSUP))
		return false;
	return true;
}

/*
 * __split_vma() bypasses sysctl_max_map_count checking.  We use this where it
 * has already been checked or doesn't make sense to fail.
 *//* 分离 vma */
int __split_vma(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long addr, int new_below)
{
	struct vm_area_struct *new;
	int err;

    /* 如果 这个vma 有自己的操作符 */
	if (vma->vm_ops && vma->vm_ops->split) {
		err = vma->vm_ops->split(vma, addr);    /* 使用这个 操作 */
		if (err)
			return err;
	}
    /**
    +-------+
    |  ...  |
    +-------+
        |
    +---|---+
    | next  |
    |       |
    |  vma  |
    |       |
    | prev  |
    +---|---+
        |
    +-------+
    |  ...  |
    +-------+

    ---------------------------

      NULL
        |
    +---|---+
    | next  |
    |       |
    |  new  |
    |       |
    | prev  |
    +---|---+
        |
      NULL
    */
	new = vm_area_dup(vma); /* 直接 dup 一个 vma 结构 */
	if (!new)   /* 失败的话，oom error */
		return -ENOMEM;

	if (new_below)  /*  */
        /* 
        +-------+
        |       | 
        |       | 
        |       | <-- addr  +-------+ <-- new->vm_end
        |  vma  |           |       |
        |       |           |  new  |
        +-------+           +-------+
        */
		new->vm_end = addr;
	else {
        /*
        +-------+--- end
        |       |                   +-------+ vma->vm_end   +-------+
        |       | len               |  vma  |               |       |
        |       |--- start -------->|       | <-- end       |  new  |
        |       |                   |       |               |       |
        |       |                   |       | <-- addr **** +-------+ = new->vm_start
        +-------+ mm->start_brk     |       |
                                    |       |
                                    +-------+ vma->vm_start
        */
		new->vm_start = addr;   /* 初始化地址 */
		new->vm_pgoff += ((addr - vma->vm_start) >> PAGE_SHIFT);/* 页偏移以外的其他部分内容 */
	}

	err = vma_dup_policy(vma, new); /* numa 内存策略 */
	if (err)
		goto out_free_vma;

    /* 匿名 vma  */
	err = anon_vma_clone(new, vma); /* 匿名 vma clone （可能用于写时复制） */
	if (err)
		goto out_free_mpol;

    /* 文件 vma */
	if (new->vm_file)
		get_file(new->vm_file);

    /* 使用vm_ops 操作符打开 */
	if (new->vm_ops && new->vm_ops->open)
		new->vm_ops->open(new);

	if (new_below)        
        /* 
        +-------+
        |       | 
        |       | 
        |       | <-- addr      +-------+ <-- new->vm_end
        |  vma  |               |       |
        |       |               |  new  |
        +-------+               +-------+ <-- new->vm_start
        */
		err = vma_adjust(vma, addr, vma->vm_end, vma->vm_pgoff +
			((addr - new->vm_start) >> PAGE_SHIFT), new);
	else
        /*
        +-------+ vma->vm_end   +-------+
        |       |               |       |
        |       |               |  new  |
        |  vma  |               |       |
        |       | <-- addr **** +-------+ <-- new->vm_start
        |       |
        |       |
        +-------+ vma->vm_start
        */
		err = vma_adjust(vma, vma->vm_start, addr, vma->vm_pgoff, new);

	/* Success. */
	if (!err)
		return 0;

	/* Clean everything up if vma_adjust failed. */
	if (new->vm_ops && new->vm_ops->close)
		new->vm_ops->close(new);
	if (new->vm_file)
		fput(new->vm_file);
	unlink_anon_vmas(new);
 out_free_mpol:
	mpol_put(vma_policy(new));
 out_free_vma:
	vm_area_free(new);
	return err;
}

/*
 * Split a vma into two pieces at address 'addr', a new vma is allocated
 * either for the first part or the tail.
 */
int split_vma(struct mm_struct *mm, struct vm_area_struct *vma,
	      unsigned long addr, int new_below)
{
	if (mm->map_count >= sysctl_max_map_count)
		return -ENOMEM;

	return __split_vma(mm, vma, addr, new_below);
}

/* Munmap is split into 2 main parts -- this part which finds
 * what needs doing, and the areas themselves, which do the
 * work.  This now handles partial unmappings.
 * Jeremy Fitzhardinge <jeremy@goop.org>
 */ /* free->unmap */
int __do_munmap(struct mm_struct *mm, unsigned long start, size_t len,
		struct list_head *uf, bool downgrade)
{
    
    //+-------+--- 
    //|       |
    //|       | len
    //|       |--- start
    //|       |
    //|       |
    //+-------+ mm->start_brk
    
	unsigned long end;
	struct vm_area_struct *vma, *prev, *last;

    /* 数据合法性 */
	if ((offset_in_page(start)) || start > TASK_SIZE || len > TASK_SIZE-start)
		return -EINVAL;

    /* 页对齐 */
	len = PAGE_ALIGN(len);  /*  */
    /*
    +-------+--- end
    |       |
    |       | len
    |       |--- start
    |       |
    |       |
    +-------+ mm->start_brk
    */
	end = start + len;      /* 结束点位置 */
	if (len == 0)
		return -EINVAL;

	/*
	 * arch_unmap() might do unmaps itself.  It must be called
	 * and finish any rbtree manipulation before this code
	 * runs and also starts to manipulate the rbtree.
	 */
	/* (5.3) arch相关的vma释放 */
	arch_unmap(mm, start, end); /* 架构相关的unmap, x86 问下为空*/

	/* Find the first overlapping VMA */
    /* (1) 找到第一个可能重叠的VMA */
    /*
    +-------+--- end
    |       |                   +-------+
    |       | len               |  VMA  |
    |       |--- start -------->|       |
    |       |                   |       |
    |       |                   |       |
    +-------+ mm->start_brk     |       |
                                +-------+
    */
	vma = find_vma(mm, start);  /* 发现一个 vma 结构 */
	if (!vma)
		return 0;   /* 如果没有这个 vma， 说明 地址不存在，后续可能段错误 */
    /*
    +-------+--- end
    |       |                   +-------+ vma->vm_end
    |       | len               |  vma  |
    |       |--- start -------->|       |
    |       |                   |       |
    |       |                   |       |
    +-------+ mm->start_brk     |       |
                                |       |
                                +-------+ vma->vm_start

                                
                                +-------+ 
                                | prev  |
                                |       |
                                |       |
                                |       |
                                |       |
                                |       |
                                +-------+
    */
	prev = vma->vm_prev;    /* 上一块 vma */
	/* we have  start < vma->vm_end  */

	/* if it doesn't overlap, we have nothing.. */
    /* (2) 如果地址没有重叠，直接返回 */
	if (vma->vm_start >= end)   /* vma 起始点大于 end 说明地址 不在地址空间中 */
		return 0;

    /* (3) 如果有unmap区域和vma有重叠，先尝试把unmap区域切分成独立的小块vma，再unmap掉 */
	/*
	 * If we need to split any vma, do it now to save pain later.
	 *
	 * Note: mremap's move_vma VM_ACCOUNT handling assumes a partially
	 * unmapped vm_area_struct will remain in use: so lower split_vma
	 * places tmp vma above, and higher split_vma places tmp vma below.
	 */
	/*
    +-------+--- end
    |       |                   +-------+ vma->vm_end
    |       | len               |  vma  |
    |       |--- start -------->|       | 
    |       |                   |       |
    |       |                   |       | <-- start
    +-------+ mm->start_brk     |       |
                                |       |
                                +-------+ vma->vm_start

	*/
	/* (3.1) 如果start和vma重叠，切一刀 */
	if (start > vma->vm_start) {    /* 地址在这个 vma 中 */
		int error;

		/*
		 * Make sure that map_count on return from munmap() will
		 * not exceed its limit; but let map_count go just above
		 * its limit temporarily, to help free resources as expected.
		 *//* 如果 这块 内存正好在vma的中间部分 ：
		    这时，需要将 vma 分块，判断 地址空间允许的最大分块数量
		    如果错误，返回 oom 错误
		vm_start                            vm_end
		    |-----------#############---------|*/
    	/*
        +-------+--- end
        |       |                   +-------+ vma->vm_end
        |       | len               |  vma  |
        |       |--- start -------->|       | <-- end
        |       |                   |       |
        |       |                   |       | <-- start
        +-------+ mm->start_brk     |       |
                                    |       |
                                    +-------+ vma->vm_start
        这种情况下是需要进一步切分 vma 结构的，如果超出允许切分的大小，返回 OOM
    	*/
		if (end < vma->vm_end && mm->map_count >= sysctl_max_map_count) 
			return -ENOMEM;

        /*
        +-------+--- end
        |       |                   +-------+ vma->vm_end 
        |       | len               |  vma  |
        |       |--- start -------->|       | <-- end
        |       |                   |       |
        |       |                   |       | <-- start
        +-------+ mm->start_brk     |       |
                                    |       |
                                    +-------+ vma->vm_start
        */
		error = __split_vma(mm, vma, start, 0); /* 分离一个 vma 结构 */
		if (error)
			return error;

        /*
                                              <-- end
        +-------+--- end            +-------+ <-- end
        |       |                   |       | <-- end
        |       | len               |       |
        |       |                   +-------+
        |       |                   
        |       |                   +-------+ vma->vm_end
        +-------+ mm->start_brk     | prev  |
                                    |       |
                                    +-------+ vma->vm_start
        */
		prev = vma;
	}

    /* (3.2) 如果end和vma冲切，切一刀 */
	/* Does it split the last one? */
	last = find_vma(mm, end);   /* last->vm_start <= end,否则返回最后一个vma */
	if (last && end > last->vm_start) {
        /* 需要继续切分
                                    +-------+
                                    |       | 
                                    |       | 
        +-------+--- end            |       | <-- end
        |       |                   | last  |
        |       | len               |       | 
        |       |                   +-------+ last->vm_start
        |       |                   
        |       |                   +-------+ vma->vm_end
        +-------+ mm->start_brk     | prev  |
                                    |       |
                                    +-------+ vma->vm_start
        */
		int error = __split_vma(mm, last, end, 1);  /* 拆分这个 vma  */
		if (error)
			return error;
	}
	vma = vma_next(mm, prev);

	if (unlikely(uf)) {
		/*
		 * If userfaultfd_unmap_prep returns an error the vmas
		 * will remain splitted, but userland will get a
		 * highly unexpected error anyway. This is no
		 * different than the case where the first of the two
		 * __split_vma fails, but we don't undo the first
		 * split, despite we could. This is unlikely enough
		 * failure that it's not worth optimizing it for.
		 */
		int error = userfaultfd_unmap_prep(vma, start, end, uf);
		if (error)
			return error;
	}

	/*
	 * unlock any mlock()ed ranges before detaching vmas
	 */ 
	/* (4) 移除目标vma上的相关lock */
	if (mm->locked_vm) {
		struct vm_area_struct *tmp = vma;
		while (tmp && tmp->vm_start < end) {
			if (tmp->vm_flags & VM_LOCKED) {
				mm->locked_vm -= vma_pages(tmp);
				munlock_vma_pages_all(tmp);
			}

			tmp = tmp->vm_next;
		}
	}

    /* (5) 移除目标vma */
	/* Detach vmas from rbtree */
    /* (5.1) 从vma红黑树中移除vma */
	if (!detach_vmas_to_be_unmapped(mm, vma, prev, end))
		downgrade = false;

	if (downgrade)
		mmap_write_downgrade(mm);

    /* (5.2) 释放掉vma空间对应的mmu映射表以及内存 */
	unmap_region(mm, vma, prev, start, end);

    /* (5.3) arch相关的vma释放 */
    /* 原来 arch_unmap 在这里 */

    /* (5.4) 移除掉vma的其他信息，最后释放掉vma结构体 */
	/* Fix up all other VM information */
	remove_vma_list(mm, vma);   /*  */

	return downgrade ? 1 : 0;
}

/**
 *  
 */
int do_munmap(struct mm_struct *mm, unsigned long start, size_t len,
	      struct list_head *uf)
{
    /* 成功返回 0 */
	return __do_munmap(mm, start, len, uf, false);
}

static int __vm_munmap(unsigned long start, size_t len, bool downgrade)
{
	int ret;
	struct mm_struct *mm = current->mm;
	LIST_HEAD(uf);

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = __do_munmap(mm, start, len, &uf, downgrade);
	/*
	 * Returning 1 indicates mmap_lock is downgraded.
	 * But 1 is not legal return value of vm_munmap() and munmap(), reset
	 * it to 0 before return.
	 */
	if (ret == 1) {
		mmap_read_unlock(mm);
		ret = 0;
	} else
		mmap_write_unlock(mm);

	userfaultfd_unmap_complete(mm, &uf);
	return ret;
}

int vm_munmap(unsigned long start, size_t len)
{
	return __vm_munmap(start, len, false);
}
EXPORT_SYMBOL(vm_munmap);

SYSCALL_DEFINE2(munmap, unsigned long, addr, size_t, len)
{
	addr = untagged_addr(addr);
	profile_munmap(addr);
	return __vm_munmap(addr, len, true);
}


/*
 * Emulation of deprecated remap_file_pages() syscall.
 */
SYSCALL_DEFINE5(remap_file_pages, unsigned long, start, unsigned long, size,
		unsigned long, prot, unsigned long, pgoff, unsigned long, flags)
{

	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long populate = 0;
	unsigned long ret = -EINVAL;
	struct file *file;

	pr_warn_once("%s (%d) uses deprecated remap_file_pages() syscall. See Documentation/vm/remap_file_pages.rst.\n",
		     current->comm, current->pid);

	if (prot)
		return ret;
	start = start & PAGE_MASK;
	size = size & PAGE_MASK;

	if (start + size <= start)
		return ret;

	/* Does pgoff wrap? */
	if (pgoff + (size >> PAGE_SHIFT) < pgoff)
		return ret;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	vma = find_vma(mm, start);

	if (!vma || !(vma->vm_flags & VM_SHARED))
		goto out;

	if (start < vma->vm_start)
		goto out;

	if (start + size > vma->vm_end) {
		struct vm_area_struct *next;

		for (next = vma->vm_next; next; next = next->vm_next) {
			/* hole between vmas ? */
			if (next->vm_start != next->vm_prev->vm_end)
				goto out;

			if (next->vm_file != vma->vm_file)
				goto out;

			if (next->vm_flags != vma->vm_flags)
				goto out;

			if (start + size <= next->vm_end)
				break;
		}

		if (!next)
			goto out;
	}

	prot |= vma->vm_flags & VM_READ ? PROT_READ : 0;
	prot |= vma->vm_flags & VM_WRITE ? PROT_WRITE : 0;
	prot |= vma->vm_flags & VM_EXEC ? PROT_EXEC : 0;

	flags &= MAP_NONBLOCK;
	flags |= MAP_SHARED | MAP_FIXED | MAP_POPULATE;
	if (vma->vm_flags & VM_LOCKED) {
		struct vm_area_struct *tmp;
		flags |= MAP_LOCKED;

		/* drop PG_Mlocked flag for over-mapped range */
		for (tmp = vma; tmp->vm_start >= start + size;
				tmp = tmp->vm_next) {
			/*
			 * Split pmd and munlock page on the border
			 * of the range.
			 */
			vma_adjust_trans_huge(tmp, start, start + size, 0);

			munlock_vma_pages_range(tmp,
					max(tmp->vm_start, start),
					min(tmp->vm_end, start + size));
		}
	}

	file = get_file(vma->vm_file);
	ret = do_mmap(vma->vm_file, start, size,
			prot, flags, pgoff, &populate, NULL);
	fput(file);
out:
	mmap_write_unlock(mm);
	if (populate)
		mm_populate(ret, populate);
	if (!IS_ERR_VALUE(ret))
		ret = 0;
	return ret;
}

/*
 *  this is really a simplified "do_mmap".  it only handles
 *  anonymous maps.  eventually we may be able to do some
 *  brk-specific accounting here.
 */ /* brk 内存申请流程核心函数 */
static int do_brk_flags(unsigned long addr, unsigned long len, unsigned long flags, struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	struct rb_node **rb_link, *rb_parent;
	pgoff_t pgoff = addr >> PAGE_SHIFT;
	int error;
	unsigned long mapped_addr;

	/* Until we need other flags, refuse anything except VM_EXEC. */
	if ((flags & (~VM_EXEC)) != 0)
		return -EINVAL;
	flags |= VM_DATA_DEFAULT_FLAGS | VM_ACCOUNT | mm->def_flags;

    /* 获取映射的addr */
	mapped_addr = get_unmapped_area(NULL, addr, len, 0, MAP_FIXED);
	if (IS_ERR_VALUE(mapped_addr))  /* unlikely */
		return mapped_addr;

	error = mlock_future_check(mm, mm->def_flags, len);
	if (error)
		return error;

	/* Clear old maps, set up prev, rb_link, rb_parent, and uf */
	if (munmap_vma_range(mm, addr, len, &prev, &rb_link, &rb_parent, uf))
		return -ENOMEM;

	/* Check against address space limits *after* clearing old maps... */
	if (!may_expand_vm(mm, flags, len >> PAGE_SHIFT))
		return -ENOMEM;

    /* 检查sysctl */
	if (mm->map_count > sysctl_max_map_count)
		return -ENOMEM;
    /* TODO */
	if (security_vm_enough_memory_mm(mm, len >> PAGE_SHIFT))
		return -ENOMEM;

	/* Can we just expand an old private anonymous mapping? */
	/* 可以直接用一个 old 私有匿名 映射吗 */
	vma = vma_merge(mm, prev, addr, addr + len, flags,
			NULL, NULL, pgoff, NULL, NULL_VM_UFFD_CTX);
	if (vma)
		goto out;

    /* 当 vma 不可合并 */
	/*
	 * create a vma struct for an anonymous mapping
	 */
	vma = vm_area_alloc(mm);    /* 分配这个结构 */
	if (!vma) {
		vm_unacct_memory(len >> PAGE_SHIFT);
		return -ENOMEM;
	}

	vma_set_anonymous(vma);     /* 匿名vma */
	vma->vm_start = addr;       /* start */
	vma->vm_end = addr + len;   /* end */
	vma->vm_pgoff = pgoff;      /* 页内偏移 */
	vma->vm_flags = flags;      /* 标志 */
	vma->vm_page_prot = vm_get_page_prot(flags);    /* VMA 的权限 */
	vma_link(mm, vma, prev, rb_link, rb_parent);    /* 插入 */
out:
	perf_event_mmap(vma);   /* TODO */
	mm->total_vm += len >> PAGE_SHIFT;  /* 共映射的页数计数 */
	mm->data_vm += len >> PAGE_SHIFT;   /* 数据映射计数 */
	if (flags & VM_LOCKED)
		mm->locked_vm += (len >> PAGE_SHIFT);   /* 锁定的页面计数 */
	vma->vm_flags |= VM_SOFTDIRTY;
	return 0;
}

int vm_brk_flags(unsigned long addr, unsigned long request, unsigned long flags)    /*  */
{
	struct mm_struct *mm = current->mm;
	unsigned long len;
	int ret;
	bool populate;
	LIST_HEAD(uf);

	len = PAGE_ALIGN(request);
	if (len < request)
		return -ENOMEM;
	if (!len)
		return 0;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = do_brk_flags(addr, len, flags, &uf);
	populate = ((mm->def_flags & VM_LOCKED) != 0);
	mmap_write_unlock(mm);
	userfaultfd_unmap_complete(mm, &uf);
	if (populate && !ret)
		mm_populate(addr, len);
	return ret;
}
EXPORT_SYMBOL(vm_brk_flags);

int vm_brk(unsigned long addr, unsigned long len)   /*  */
{
	return vm_brk_flags(addr, len, 0);
}
EXPORT_SYMBOL(vm_brk);

/* Release all mmaps. */
void exit_mmap(struct mm_struct *mm)
{
	struct mmu_gather tlb;
	struct vm_area_struct *vma;
	unsigned long nr_accounted = 0;

	/* mm's last user has gone, and its about to be pulled down */
	mmu_notifier_release(mm);

	if (unlikely(mm_is_oom_victim(mm))) {
		/*
		 * Manually reap the mm to free as much memory as possible.
		 * Then, as the oom reaper does, set MMF_OOM_SKIP to disregard
		 * this mm from further consideration.  Taking mm->mmap_lock for
		 * write after setting MMF_OOM_SKIP will guarantee that the oom
		 * reaper will not run on this mm again after mmap_lock is
		 * dropped.
		 *
		 * Nothing can be holding mm->mmap_lock here and the above call
		 * to mmu_notifier_release(mm) ensures mmu notifier callbacks in
		 * __oom_reap_task_mm() will not block.
		 *
		 * This needs to be done before calling munlock_vma_pages_all(),
		 * which clears VM_LOCKED, otherwise the oom reaper cannot
		 * reliably test it.
		 */
		(void)__oom_reap_task_mm(mm);

		set_bit(MMF_OOM_SKIP, &mm->flags);
		mmap_write_lock(mm);
		mmap_write_unlock(mm);
	}

	if (mm->locked_vm) {
		vma = mm->mmap;
		while (vma) {
			if (vma->vm_flags & VM_LOCKED)
				munlock_vma_pages_all(vma);
			vma = vma->vm_next;
		}
	}

	arch_exit_mmap(mm);

	vma = mm->mmap;
	if (!vma)	/* Can happen if dup_mmap() received an OOM */
		return;

	lru_add_drain();
	flush_cache_mm(mm);
	tlb_gather_mmu(&tlb, mm, 0, -1);
	/* update_hiwater_rss(mm) here? but nobody should be looking */
	/* Use -1 here to ensure all VMAs in the mm are unmapped */
	unmap_vmas(&tlb, vma, 0, -1);
	free_pgtables(&tlb, vma, FIRST_USER_ADDRESS, USER_PGTABLES_CEILING);
	tlb_finish_mmu(&tlb, 0, -1);

	/*
	 * Walk the list again, actually closing and freeing it,
	 * with preemption enabled, without holding any MM locks.
	 */
	while (vma) {
		if (vma->vm_flags & VM_ACCOUNT)
			nr_accounted += vma_pages(vma);
		vma = remove_vma(vma);
		cond_resched();
	}
	vm_unacct_memory(nr_accounted);
}

/* Insert vm structure into process list sorted by address
 * and into the inode's i_mmap tree.  If vm_file is non-NULL
 * then i_mmap_rwsem is taken here.
 */
int insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vma)  /*  */
{
	struct vm_area_struct *prev;
	struct rb_node **rb_link, *rb_parent;

	if (find_vma_links(mm, vma->vm_start, vma->vm_end,
			   &prev, &rb_link, &rb_parent))
		return -ENOMEM;
	if ((vma->vm_flags & VM_ACCOUNT) &&
	     security_vm_enough_memory_mm(mm, vma_pages(vma)))
		return -ENOMEM;

	/*
	 * The vm_pgoff of a purely anonymous vma should be irrelevant
	 * until its first write fault, when page's anon_vma and index
	 * are set.  But now set the vm_pgoff it will almost certainly
	 * end up with (unless mremap moves it elsewhere before that
	 * first wfault), so /proc/pid/maps tells a consistent story.
	 *
	 * By setting it to reflect the virtual start address of the
	 * vma, merges and splits can happen in a seamless way, just
	 * using the existing file pgoff checks and manipulations.
	 * Similarly in do_mmap and in do_brk_flags.
	 */
	if (vma_is_anonymous(vma)) {
		BUG_ON(vma->anon_vma);
		vma->vm_pgoff = vma->vm_start >> PAGE_SHIFT;
	}

	vma_link(mm, vma, prev, rb_link, rb_parent);    /*  */
	return 0;
}

/*
 * Copy the vma structure to a new location in the same mm,
 * prior to moving page table entries, to effect an mremap move.
 */
struct vm_area_struct *copy_vma(struct vm_area_struct **vmap,
	unsigned long addr, unsigned long len, pgoff_t pgoff,
	bool *need_rmap_locks)
{
	struct vm_area_struct *vma = *vmap;
	unsigned long vma_start = vma->vm_start;
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *new_vma, *prev;
	struct rb_node **rb_link, *rb_parent;
	bool faulted_in_anon_vma = true;

	/*
	 * If anonymous vma has not yet been faulted, update new pgoff
	 * to match new location, to increase its chance of merging.
	 */
	if (unlikely(vma_is_anonymous(vma) && !vma->anon_vma)) {
		pgoff = addr >> PAGE_SHIFT;
		faulted_in_anon_vma = false;
	}

	if (find_vma_links(mm, addr, addr + len, &prev, &rb_link, &rb_parent))
		return NULL;	/* should never get here */
	new_vma = vma_merge(mm, prev, addr, addr + len, vma->vm_flags,
			    vma->anon_vma, vma->vm_file, pgoff, vma_policy(vma),
			    vma->vm_userfaultfd_ctx);
	if (new_vma) {
		/*
		 * Source vma may have been merged into new_vma
		 */
		if (unlikely(vma_start >= new_vma->vm_start &&
			     vma_start < new_vma->vm_end)) {
			/*
			 * The only way we can get a vma_merge with
			 * self during an mremap is if the vma hasn't
			 * been faulted in yet and we were allowed to
			 * reset the dst vma->vm_pgoff to the
			 * destination address of the mremap to allow
			 * the merge to happen. mremap must change the
			 * vm_pgoff linearity between src and dst vmas
			 * (in turn preventing a vma_merge) to be
			 * safe. It is only safe to keep the vm_pgoff
			 * linear if there are no pages mapped yet.
			 */
			VM_BUG_ON_VMA(faulted_in_anon_vma, new_vma);
			*vmap = vma = new_vma;
		}
		*need_rmap_locks = (new_vma->vm_pgoff <= vma->vm_pgoff);
	} else {
		new_vma = vm_area_dup(vma);
		if (!new_vma)
			goto out;
		new_vma->vm_start = addr;
		new_vma->vm_end = addr + len;
		new_vma->vm_pgoff = pgoff;
		if (vma_dup_policy(vma, new_vma))
			goto out_free_vma;
		if (anon_vma_clone(new_vma, vma))
			goto out_free_mempol;
		if (new_vma->vm_file)
			get_file(new_vma->vm_file);
		if (new_vma->vm_ops && new_vma->vm_ops->open)
			new_vma->vm_ops->open(new_vma);
		vma_link(mm, new_vma, prev, rb_link, rb_parent);
		*need_rmap_locks = false;
	}
	return new_vma;

out_free_mempol:
	mpol_put(vma_policy(new_vma));
out_free_vma:
	vm_area_free(new_vma);
out:
	return NULL;
}

/*
 * Return true if the calling process may expand its vm space by the passed
 * number of pages
 *
 * 如果调用进程可以通过传递的页数扩展其 vm 空间，则返回 true
 *  
 * 判断地址空间大小是否已经超标
 */
bool may_expand_vm(struct mm_struct *mm, vm_flags_t flags, unsigned long npages)
{
    /* 检查映射的页数有没有超限 */
	if (mm->total_vm + npages > rlimit(RLIMIT_AS) >> PAGE_SHIFT)
		return false;

    /* 数据 mapping 
        1.在 brk系统调用传入的是0，此代码不执行
        2.*/
	if (is_data_mapping(flags) &&
	    mm->data_vm + npages > rlimit(RLIMIT_DATA) >> PAGE_SHIFT) {
		/* Workaround for Valgrind - Valgrind 的解决方法 */
		if (rlimit(RLIMIT_DATA) == 0 &&
		    mm->data_vm + npages <= rlimit_max(RLIMIT_DATA) >> PAGE_SHIFT)
			return true;

		pr_warn_once("%s (%d): VmData %lu exceed data ulimit %lu. Update limits%s.\n",
			     current->comm, current->pid,
			     (mm->data_vm + npages) << PAGE_SHIFT,
			     rlimit(RLIMIT_DATA),
			     ignore_rlimit_data ? "" : " or use boot option ignore_rlimit_data");

		if (!ignore_rlimit_data)
			return false;
	}

	return true;
}

void vm_stat_account(struct mm_struct *mm, vm_flags_t flags, long npages)
{
	mm->total_vm += npages;

	if (is_exec_mapping(flags))
		mm->exec_vm += npages;
	else if (is_stack_mapping(flags))
		mm->stack_vm += npages;
	else if (is_data_mapping(flags))
		mm->data_vm += npages;
}

static vm_fault_t special_mapping_fault(struct vm_fault *vmf);

/*
 * Having a close hook prevents vma merging regardless of flags.
 */
static void special_mapping_close(struct vm_area_struct *vma)
{
}

static const char *special_mapping_name(struct vm_area_struct *vma)
{
	return ((struct vm_special_mapping *)vma->vm_private_data)->name;
}

static int special_mapping_mremap(struct vm_area_struct *new_vma)
{
	struct vm_special_mapping *sm = new_vma->vm_private_data;

	if (WARN_ON_ONCE(current->mm != new_vma->vm_mm))
		return -EFAULT;

	if (sm->mremap)
		return sm->mremap(sm, new_vma);

	return 0;
}

static const struct vm_operations_struct special_mapping_vmops = {
	.close = special_mapping_close,
	.fault = special_mapping_fault,
	.mremap = special_mapping_mremap,
	.name = special_mapping_name,
	/* vDSO code relies that VVAR can't be accessed remotely */
	.access = NULL,
};

static const struct vm_operations_struct legacy_special_mapping_vmops = {
	.close = special_mapping_close,
	.fault = special_mapping_fault,
};

static vm_fault_t special_mapping_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	pgoff_t pgoff;
	struct page **pages;

	if (vma->vm_ops == &legacy_special_mapping_vmops) {
		pages = vma->vm_private_data;
	} else {
		struct vm_special_mapping *sm = vma->vm_private_data;

		if (sm->fault)
			return sm->fault(sm, vmf->vma, vmf);

		pages = sm->pages;
	}

	for (pgoff = vmf->pgoff; pgoff && *pages; ++pages)
		pgoff--;

	if (*pages) {
		struct page *page = *pages;
		get_page(page);
		vmf->page = page;
		return 0;
	}

	return VM_FAULT_SIGBUS;
}

static struct vm_area_struct *__install_special_mapping(
	struct mm_struct *mm,
	unsigned long addr, unsigned long len,
	unsigned long vm_flags, void *priv,
	const struct vm_operations_struct *ops)
{
	int ret;
	struct vm_area_struct *vma;

	vma = vm_area_alloc(mm);
	if (unlikely(vma == NULL))
		return ERR_PTR(-ENOMEM);

	vma->vm_start = addr;
	vma->vm_end = addr + len;

	vma->vm_flags = vm_flags | mm->def_flags | VM_DONTEXPAND | VM_SOFTDIRTY;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	vma->vm_ops = ops;
	vma->vm_private_data = priv;

	ret = insert_vm_struct(mm, vma);
	if (ret)
		goto out;

	vm_stat_account(mm, vma->vm_flags, len >> PAGE_SHIFT);

	perf_event_mmap(vma);

	return vma;

out:
	vm_area_free(vma);
	return ERR_PTR(ret);
}

bool vma_is_special_mapping(const struct vm_area_struct *vma,
	const struct vm_special_mapping *sm)
{
	return vma->vm_private_data == sm &&
		(vma->vm_ops == &special_mapping_vmops ||
		 vma->vm_ops == &legacy_special_mapping_vmops);
}

/*
 * Called with mm->mmap_lock held for writing.
 * Insert a new vma covering the given region, with the given flags.
 * Its pages are supplied by the given array of struct page *.
 * The array can be shorter than len >> PAGE_SHIFT if it's null-terminated.
 * The region past the last page supplied will always produce SIGBUS.
 * The array pointer and the pages it points to are assumed to stay alive
 * for as long as this mapping might exist.
 */
struct vm_area_struct *_install_special_mapping(
	struct mm_struct *mm,
	unsigned long addr, unsigned long len,
	unsigned long vm_flags, const struct vm_special_mapping *spec)
{
	return __install_special_mapping(mm, addr, len, vm_flags, (void *)spec,
					&special_mapping_vmops);
}

int install_special_mapping(struct mm_struct *mm,
			    unsigned long addr, unsigned long len,
			    unsigned long vm_flags, struct page **pages)
{
	struct vm_area_struct *vma = __install_special_mapping(
		mm, addr, len, vm_flags, (void *)pages,
		&legacy_special_mapping_vmops);

	return PTR_ERR_OR_ZERO(vma);
}

static DEFINE_MUTEX(mm_all_locks_mutex);

static void vm_lock_anon_vma(struct mm_struct *mm, struct anon_vma *anon_vma)
{
	if (!test_bit(0, (unsigned long *) &anon_vma->root->rb_root.rb_root.rb_node)) {
		/*
		 * The LSB of head.next can't change from under us
		 * because we hold the mm_all_locks_mutex.
		 */
		down_write_nest_lock(&anon_vma->root->rwsem, &mm->mmap_lock);
		/*
		 * We can safely modify head.next after taking the
		 * anon_vma->root->rwsem. If some other vma in this mm shares
		 * the same anon_vma we won't take it again.
		 *
		 * No need of atomic instructions here, head.next
		 * can't change from under us thanks to the
		 * anon_vma->root->rwsem.
		 */
		if (__test_and_set_bit(0, (unsigned long *)
				       &anon_vma->root->rb_root.rb_root.rb_node))
			BUG();
	}
}

static void vm_lock_mapping(struct mm_struct *mm, struct address_space *mapping)
{
	if (!test_bit(AS_MM_ALL_LOCKS, &mapping->flags)) {
		/*
		 * AS_MM_ALL_LOCKS can't change from under us because
		 * we hold the mm_all_locks_mutex.
		 *
		 * Operations on ->flags have to be atomic because
		 * even if AS_MM_ALL_LOCKS is stable thanks to the
		 * mm_all_locks_mutex, there may be other cpus
		 * changing other bitflags in parallel to us.
		 */
		if (test_and_set_bit(AS_MM_ALL_LOCKS, &mapping->flags))
			BUG();
		down_write_nest_lock(&mapping->i_mmap_rwsem, &mm->mmap_lock);
	}
}

/*
 * This operation locks against the VM for all pte/vma/mm related
 * operations that could ever happen on a certain mm. This includes
 * vmtruncate, try_to_unmap, and all page faults.
 *
 * The caller must take the mmap_lock in write mode before calling
 * mm_take_all_locks(). The caller isn't allowed to release the
 * mmap_lock until mm_drop_all_locks() returns.
 *
 * mmap_lock in write mode is required in order to block all operations
 * that could modify pagetables and free pages without need of
 * altering the vma layout. It's also needed in write mode to avoid new
 * anon_vmas to be associated with existing vmas.
 *
 * A single task can't take more than one mm_take_all_locks() in a row
 * or it would deadlock.
 *
 * The LSB in anon_vma->rb_root.rb_node and the AS_MM_ALL_LOCKS bitflag in
 * mapping->flags avoid to take the same lock twice, if more than one
 * vma in this mm is backed by the same anon_vma or address_space.
 *
 * We take locks in following order, accordingly to comment at beginning
 * of mm/rmap.c:
 *   - all hugetlbfs_i_mmap_rwsem_key locks (aka mapping->i_mmap_rwsem for
 *     hugetlb mapping);
 *   - all i_mmap_rwsem locks;
 *   - all anon_vma->rwseml
 *
 * We can take all locks within these types randomly because the VM code
 * doesn't nest them and we protected from parallel mm_take_all_locks() by
 * mm_all_locks_mutex.
 *
 * mm_take_all_locks() and mm_drop_all_locks are expensive operations
 * that may have to take thousand of locks.
 *
 * mm_take_all_locks() can fail if it's interrupted by signals.
 */
int mm_take_all_locks(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	struct anon_vma_chain *avc;

	BUG_ON(mmap_read_trylock(mm));

	mutex_lock(&mm_all_locks_mutex);

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->vm_file && vma->vm_file->f_mapping &&
				is_vm_hugetlb_page(vma))
			vm_lock_mapping(mm, vma->vm_file->f_mapping);
	}

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->vm_file && vma->vm_file->f_mapping &&
				!is_vm_hugetlb_page(vma))
			vm_lock_mapping(mm, vma->vm_file->f_mapping);
	}

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->anon_vma)
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				vm_lock_anon_vma(mm, avc->anon_vma);
	}

	return 0;

out_unlock:
	mm_drop_all_locks(mm);
	return -EINTR;
}

static void vm_unlock_anon_vma(struct anon_vma *anon_vma)
{
	if (test_bit(0, (unsigned long *) &anon_vma->root->rb_root.rb_root.rb_node)) {
		/*
		 * The LSB of head.next can't change to 0 from under
		 * us because we hold the mm_all_locks_mutex.
		 *
		 * We must however clear the bitflag before unlocking
		 * the vma so the users using the anon_vma->rb_root will
		 * never see our bitflag.
		 *
		 * No need of atomic instructions here, head.next
		 * can't change from under us until we release the
		 * anon_vma->root->rwsem.
		 */
		if (!__test_and_clear_bit(0, (unsigned long *)
					  &anon_vma->root->rb_root.rb_root.rb_node))
			BUG();
		anon_vma_unlock_write(anon_vma);
	}
}

static void vm_unlock_mapping(struct address_space *mapping)
{
	if (test_bit(AS_MM_ALL_LOCKS, &mapping->flags)) {
		/*
		 * AS_MM_ALL_LOCKS can't change to 0 from under us
		 * because we hold the mm_all_locks_mutex.
		 */
		i_mmap_unlock_write(mapping);
		if (!test_and_clear_bit(AS_MM_ALL_LOCKS,
					&mapping->flags))
			BUG();
	}
}

/*
 * The mmap_lock cannot be released by the caller until
 * mm_drop_all_locks() returns.
 */
void mm_drop_all_locks(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	struct anon_vma_chain *avc;

	BUG_ON(mmap_read_trylock(mm));
	BUG_ON(!mutex_is_locked(&mm_all_locks_mutex));

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (vma->anon_vma)
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				vm_unlock_anon_vma(avc->anon_vma);
		if (vma->vm_file && vma->vm_file->f_mapping)
			vm_unlock_mapping(vma->vm_file->f_mapping);
	}

	mutex_unlock(&mm_all_locks_mutex);
}

/*
 * initialise the percpu counter for VM
 * 
 */
void __init mmap_init(void) /*  */
{
	int ret;

	ret = percpu_counter_init(&vm_committed_as, 0, GFP_KERNEL);
	VM_BUG_ON(ret);
}

/*
 * Initialise sysctl_user_reserve_kbytes.
 *
 * This is intended to prevent a user from starting a single memory hogging
 * process, such that they cannot recover (kill the hog) in OVERCOMMIT_NEVER
 * mode.
 *
 * The default value is min(3% of free memory, 128MB)
 * 128MB is enough to recover with sshd/login, bash, and top/kill.
 */
static int init_user_reserve(void)
{
	unsigned long free_kbytes;

	free_kbytes = global_zone_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

	sysctl_user_reserve_kbytes = min(free_kbytes / 32, 1UL << 17);
	return 0;
}
subsys_initcall(init_user_reserve);

/*
 * Initialise sysctl_admin_reserve_kbytes.
 *
 * The purpose of sysctl_admin_reserve_kbytes is to allow the sys admin
 * to log in and kill a memory hogging process.
 *
 * Systems with more than 256MB will reserve 8MB, enough to recover
 * with sshd, bash, and top in OVERCOMMIT_GUESS. Smaller systems will
 * only reserve 3% of free pages by default.
 */
static int init_admin_reserve(void)
{
	unsigned long free_kbytes;

	free_kbytes = global_zone_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

	sysctl_admin_reserve_kbytes = min(free_kbytes / 32, 1UL << 13);
	return 0;
}
subsys_initcall(init_admin_reserve);

/*
 * Reinititalise user and admin reserves if memory is added or removed.
 *
 * The default user reserve max is 128MB, and the default max for the
 * admin reserve is 8MB. These are usually, but not always, enough to
 * enable recovery from a memory hogging process using login/sshd, a shell,
 * and tools like top. It may make sense to increase or even disable the
 * reserve depending on the existence of swap or variations in the recovery
 * tools. So, the admin may have changed them.
 *
 * If memory is added and the reserves have been eliminated or increased above
 * the default max, then we'll trust the admin.
 *
 * If memory is removed and there isn't enough free memory, then we
 * need to reset the reserves.
 *
 * Otherwise keep the reserve set by the admin.
 */
static int reserve_mem_notifier(struct notifier_block *nb,
			     unsigned long action, void *data)
{
	unsigned long tmp, free_kbytes;

	switch (action) {
	case MEM_ONLINE:
		/* Default max is 128MB. Leave alone if modified by operator. */
		tmp = sysctl_user_reserve_kbytes;
		if (0 < tmp && tmp < (1UL << 17))
			init_user_reserve();

		/* Default max is 8MB.  Leave alone if modified by operator. */
		tmp = sysctl_admin_reserve_kbytes;
		if (0 < tmp && tmp < (1UL << 13))
			init_admin_reserve();

		break;
	case MEM_OFFLINE:
		free_kbytes = global_zone_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

		if (sysctl_user_reserve_kbytes > free_kbytes) {
			init_user_reserve();
			pr_info("vm.user_reserve_kbytes reset to %lu\n",
				sysctl_user_reserve_kbytes);
		}

		if (sysctl_admin_reserve_kbytes > free_kbytes) {
			init_admin_reserve();
			pr_info("vm.admin_reserve_kbytes reset to %lu\n",
				sysctl_admin_reserve_kbytes);
		}
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block reserve_mem_nb = {
	.notifier_call = reserve_mem_notifier,
};

static int __meminit init_reserve_notifier(void)
{
	if (register_hotmemory_notifier(&reserve_mem_nb))
		pr_err("Failed registering memory add/remove notifier for admin reserve\n");

	return 0;
}
subsys_initcall(init_reserve_notifier);
