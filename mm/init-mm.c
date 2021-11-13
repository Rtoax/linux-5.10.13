// SPDX-License-Identifier: GPL-2.0
#include <linux/mm_types.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/cpumask.h>
#include <linux/mman.h>
#include <linux/pgtable.h>

#include <linux/atomic.h>
#include <linux/user_namespace.h>
#include <asm/mmu.h>

#ifndef INIT_MM_CONTEXT
#define INIT_MM_CONTEXT(name)
#endif

/*
 * For dynamically allocated mm_structs, there is a dynamically sized cpumask
 * at the end of the structure, the size of which depends on the maximum CPU
 * number the system can see. That way we allocate only as much memory for
 * mm_cpumask() as needed for the hundreds, or thousands of processes that
 * a system typically runs.
 *
 * Since there is only one init_mm in the entire system, keep it simple
 * and size this cpu_bitmask to NR_CPUS.
 */
struct mm_struct init_mm = {/* 初始的 mm_struct 结构 */
	init_mm.mm_rb		= RB_ROOT,/* 红黑树 */
	init_mm.pgd		= swapper_pg_dir,/* 一级页表 */
	init_mm.mm_users	= ATOMIC_INIT(2),/* 用户空间使用的用户数 */
	init_mm.mm_count	= ATOMIC_INIT(1),/* 内存引用次数 */
	init_mm.write_protect_seq = SEQCNT_ZERO(init_mm.write_protect_seq),/* 写被保护的 pages 强制后期 COW */
	MMAP_LOCK_INITIALIZER(init_mm)/* 读写锁 */
	init_mm.page_table_lock =  __SPIN_LOCK_UNLOCKED(init_mm.page_table_lock),/* 保护页表和一些计数器 */
	init_mm.arg_lock	=  __SPIN_LOCK_UNLOCKED(init_mm.arg_lock),/* 保护 mm_struct 中的一些参数 */
	init_mm.mmlist		= LIST_HEAD_INIT(init_mm.mmlist),/* 保存可能被 swap 的链表 */
	init_mm.user_ns	= &init_user_ns,/* namespace 资源的隔离， cgroup=资源的限制*/
	init_mm.cpu_bitmap	= CPU_BITS_NONE,/*  */
	INIT_MM_CONTEXT(init_mm)/* TODO */
};
