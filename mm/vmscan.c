// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/mm/vmscan.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, Stephen Tweedie.
 *  kswapd added: 7.1.96  sct
 *  Removed kswapd_ctl limits, and swap out as many pages as needed
 *  to bring the system back to freepages.high: 2.4.97, Rik van Riel.
 *  Zone aware kswapd started 02/00, Kanoj Sarcar (kanoj@sgi.com).
 *  Multiqueue VM started 5.8.00, Rik van Riel.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/vmpressure.h>
#include <linux/vmstat.h>
#include <linux/file.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>	/* for try_to_release_page(),
					buffer_heads_over_limit */
#include <linux/mm_inline.h>
#include <linux/backing-dev.h>
#include <linux/rmap.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/compaction.h>
#include <linux/notifier.h>
#include <linux/rwsem.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/memcontrol.h>
#include <linux/delayacct.h>
#include <linux/sysctl.h>
#include <linux/oom.h>
#include <linux/pagevec.h>
#include <linux/prefetch.h>
#include <linux/printk.h>
#include <linux/dax.h>
#include <linux/psi.h>

#include <asm/tlbflush.h>
#include <asm/div64.h>

#include <linux/swapops.h>
#include <linux/balloon_compaction.h>

#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/vmscan.h>

/**
 *  存放 `页回收操作`(页面扫描) 执行时的相关信息
 */
struct scan_control {

	/* How many pages shrink_list() should reclaim */
	unsigned long nr_to_reclaim;    /* 要回收的页面数量 */

	/*
	 * Nodemask of nodes allowed by the caller. If NULL, all nodes
	 * are scanned.
	 */
	nodemask_t	*nodemask;  /* 内存节点掩码 */

	/*
	 * The memory cgroup that hit its limit and as a result is the
	 * primary target of this reclaim invocation.
	 *
	 * 达到极限并因此达到其极限的 内存组 是此回收调用的主要目标。
	 */
	struct mem_cgroup *target_mem_cgroup;

	/*
	 * Scan pressure balancing between anon and file LRUs
	 */
	unsigned long	anon_cost;
	unsigned long	file_cost;

	/* Can active pages be deactivated as part of reclaim? */
#define DEACTIVATE_ANON 1
#define DEACTIVATE_FILE 2
	unsigned int may_deactivate:2;
	unsigned int force_deactivate:1;
	unsigned int skipped_deactivate:1;

	/* Writepage batching in laptop mode; RECLAIM_WRITE */
	unsigned int may_writepage:1;   /* 批量写页面 */

	/* Can mapped pages be reclaimed? */
    /* ==1 标识允许回收映射页面 */
	unsigned int may_unmap:1;       /* 是否允许回收映射的页面 */

	/* Can pages be swapped as part of reclaim? */
	unsigned int may_swap:1;        /* 是否允许写入交换分区来回收页面 */

	/*
	 * Cgroups are not reclaimed below their configured memory.low,
	 * unless we threaten to OOM. If any cgroups are skipped due to
	 * memory.low and nothing was reclaimed, go back for memory.low.
	 */
	unsigned int memcg_low_reclaim:1;
	unsigned int memcg_low_skipped:1;

	unsigned int hibernation_mode:1;

	/* One of the zones is ready for compaction */
	unsigned int compaction_ready:1;    /* 内存规整已完成 */

	/* There is easily reclaimable cold cache in the current node */
	unsigned int cache_trim_mode:1;

    /**
     *
     */
	/* The file pages on the current node are dangerously low */
	unsigned int file_is_tiny:1;

	/* Allocation order */
	s8 order;

	/**
	 *  Scan (total_size >> priority) pages at once
	 *  页面扫描优先级，优先级数值越小，扫描的页面数量越大
     */
	s8 priority;

	/* The highest zone to isolate pages for reclaim from */
	s8 reclaim_idx; /* 最高允许页面回收的 ZONE */

	/* This context's GFP mask */
	gfp_t gfp_mask; /* 分配掩码 */

	/* Incremented by the number of inactive pages that were scanned */
	unsigned long nr_scanned;   /* 扫描不活跃页面的数量 */

	/* Number of pages freed so far during a call to shrink_zones() */
	unsigned long nr_reclaimed; /* 已经回收页面的数量 */

    /**
     *  在页面回收过程中，可能会遇到如下问题
     *
*
     *  1. 大量脏页
     *  2. 大量正在回写的页面阻塞在块设备的IO通道上
     *
     **
     *  以上的问题会严重影响页面回收机制的性能，设置应用程序的用户体验，
     *  为了捕获这些信息，页面回收机制在 scan_control 的 nr 结构来统计信息
     */
	struct {
		unsigned int dirty;         /* 脏页的数量 */
		unsigned int unqueued_dirty;/* 还没有回写的脏页 */
		unsigned int congested;     /* congested(拥挤) 块设备IO数据回写 */
		unsigned int writeback;     /* 正在回写的数量 */
		unsigned int immediate;     /* 立刻做出特殊处理 */
		unsigned int file_taken;    /* 分离的文件页面数量 */
		unsigned int taken;         /* 分离的页面数量 */
	} nr;   /* 用于统计数量 */

	/* for recording the reclaimed slab by now */
	struct reclaim_state reclaim_state;
};

#ifdef ARCH_HAS_PREFETCHW
#define prefetchw_prev_lru_page(_page, _base, _field)			\
	do {								\
		if ((_page)->lru.prev != _base) {			\
			struct page *prev;				\
									\
			prev = lru_to_page(&(_page->lru));		\
			prefetchw(&prev->_field);			\
		}							\
	} while (0)
#else

#endif

/*
 * From 0 .. 200.  Higher means more swappy.
 */
int vm_swappiness = 60;

/**
 * @brief	设置回收状态
 *
 * @param task
 * @param rs
 */
static void set_task_reclaim_state(struct task_struct *task,
				   struct reclaim_state *rs)
{
	/* Check for an overwrite */
	WARN_ON_ONCE(rs && task->reclaim_state);

	/* Check for the nulling of an already-nulled member */
	WARN_ON_ONCE(!rs && !task->reclaim_state);

	task->reclaim_state = rs;
}

/**
 *  链表头节点为 struct shrinker
 */
static LIST_HEAD(shrinker_list);
static DECLARE_RWSEM(shrinker_rwsem);
static struct list_head shrinker_list; //++
struct rw_semaphore shrinker_rwsem = __RWSEM_INITIALIZER(shrinker_rwsem);//++

#ifdef CONFIG_MEMCG
/*
 * We allow subsystems to populate their shrinker-related
 * LRU lists before register_shrinker_prepared() is called
 * for the shrinker, since we don't want to impose
 * restrictions on their internal registration order.
 * In this case shrink_slab_memcg() may find corresponding
 * bit is set in the shrinkers map.
 *
 * This value is used by the function to detect registering
 * shrinkers and to skip do_shrink_slab() calls for them.
 */
#define SHRINKER_REGISTERING ((struct shrinker *)~0UL)

static DEFINE_IDR(shrinker_idr);
static int shrinker_nr_max;

static int prealloc_memcg_shrinker(struct shrinker *shrinker)
{
	int id, ret = -ENOMEM;

	down_write(&shrinker_rwsem);
	/* This may call shrinker, so it must use down_read_trylock() */
	id = idr_alloc(&shrinker_idr, SHRINKER_REGISTERING, 0, 0, GFP_KERNEL);
	if (id < 0)
		goto unlock;

	if (id >= shrinker_nr_max) {
		if (memcg_expand_shrinker_maps(id)) {
			idr_remove(&shrinker_idr, id);
			goto unlock;
		}

		shrinker_nr_max = id + 1;
	}
	shrinker->id = id;
	ret = 0;
unlock:
	up_write(&shrinker_rwsem);
	return ret;
}

static void unregister_memcg_shrinker(struct shrinker *shrinker)
{
	int id = shrinker->id;

	BUG_ON(id < 0);

	down_write(&shrinker_rwsem);
	idr_remove(&shrinker_idr, id);
	up_write(&shrinker_rwsem);
}

/**
 *  将要到达 cgroup 极限的  cgroup 是首选
 */
static bool cgroup_reclaim(struct scan_control *sc)
{
	return sc->target_mem_cgroup;
}

/**
 * writeback_throttling_sane - is the usual dirty throttling mechanism available?
 * @sc: scan_control in question
 *
 * The normal page dirty throttling mechanism in balance_dirty_pages() is
 * completely broken with the legacy memcg and direct stalling in
 * shrink_page_list() is used for throttling instead, which lacks all the
 * niceties such as fairness, adaptive pausing, bandwidth proportional
 * allocation and configurability.
 *
 * This function tests whether the vmscan currently in progress can assume
 * that the normal dirty throttling mechanism is operational.
 */
static bool writeback_throttling_sane(struct scan_control *sc)
{
	if (!cgroup_reclaim(sc))
		return true;
#ifdef CONFIG_CGROUP_WRITEBACK
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return true;
#endif
	return false;
}
#else

#endif

/*
 * This misses isolated pages which are not accounted for to save counters.
 * As the data only determines if reclaim or compaction continues, it is
 * not expected that isolated pages will be a dominating factor.
 *
 * 是否有可回收的 page
 * 1. 不活跃 + 活跃 的文件映射
 * 2. 如果开启了 swap： 不活跃 + 活跃 的匿名映射
 */
unsigned long zone_reclaimable_pages(struct zone *zone)
{
	unsigned long nr;

    /**
     *  1. 不活跃 + 活跃 的文件映射
     */
	nr = zone_page_state_snapshot(zone, NR_ZONE_INACTIVE_FILE) +
		zone_page_state_snapshot(zone, NR_ZONE_ACTIVE_FILE);
    /**
     *  2. 如果开启了 swap
     */
	if (get_nr_swap_pages() > 0)
        /**
         *  不活跃 + 活跃 的匿名映射
         */
		nr += zone_page_state_snapshot(zone, NR_ZONE_INACTIVE_ANON) +
			zone_page_state_snapshot(zone, NR_ZONE_ACTIVE_ANON);

	return nr;
}

/**
 * lruvec_lru_size -  Returns the number of pages on the given LRU list.
 * @lruvec: lru vector
 * @lru: lru to use
 * @zone_idx: zones to consider (use MAX_NR_ZONES for the whole LRU list)
 */
unsigned long lruvec_lru_size(struct lruvec *lruvec, enum lru_list lru, int zone_idx)
{
	unsigned long size = 0;
	int zid;

	for (zid = 0; zid <= zone_idx && zid < MAX_NR_ZONES; zid++) {
		struct zone *zone = &lruvec_pgdat(lruvec)->node_zones[zid];

		if (!managed_zone(zone))
			continue;

		if (!mem_cgroup_disabled())
			size += mem_cgroup_get_zone_lru_size(lruvec, lru, zid);
		else
			size += zone_page_state(zone, NR_ZONE_LRU_BASE + lru);
	}
	return size;
}

/*
 * Add a shrinker callback to be called from the vm.
 */
int prealloc_shrinker(struct shrinker *shrinker)
{
	unsigned int size = sizeof(*shrinker->nr_deferred);

	if (shrinker->flags & SHRINKER_NUMA_AWARE)
		size *= nr_node_ids;

	shrinker->nr_deferred = kzalloc(size, GFP_KERNEL);
	if (!shrinker->nr_deferred)
		return -ENOMEM;

	if (shrinker->flags & SHRINKER_MEMCG_AWARE) {
		if (prealloc_memcg_shrinker(shrinker))
			goto free_deferred;
	}

	return 0;

free_deferred:
	kfree(shrinker->nr_deferred);
	shrinker->nr_deferred = NULL;
	return -ENOMEM;
}

void free_prealloced_shrinker(struct shrinker *shrinker)
{
	if (!shrinker->nr_deferred)
		return;

	if (shrinker->flags & SHRINKER_MEMCG_AWARE)
		unregister_memcg_shrinker(shrinker);

	kfree(shrinker->nr_deferred);
	shrinker->nr_deferred = NULL;
}

/**
 * 添加到 shrinker_list
 *
 * 内存不足触发内存回收时，shrink_slab会把挂载在shrinker_list链上的slab收割函数执行一遍，
 * 其中 dentry slab 的收割机属于vfs层，mount的时候在alloc_sb的时候会挂到shrinker_list收割机链表里，
 * 最终是执行 prune_dcache_sb 去回收 dentry cache 内存。
 */
void register_shrinker_prepared(struct shrinker *shrinker)
{
	down_write(&shrinker_rwsem);

    /**
     *  添加到链表
     */
	list_add_tail(&shrinker->list, &shrinker_list);

#ifdef CONFIG_MEMCG
	if (shrinker->flags & SHRINKER_MEMCG_AWARE)
		idr_replace(&shrinker_idr, shrinker, shrinker->id);
#endif
	up_write(&shrinker_rwsem);
}

/**
 *  注册
 */
int register_shrinker(struct shrinker *shrinker)
{
	int err = prealloc_shrinker(shrinker);

	if (err)
		return err;
	register_shrinker_prepared(shrinker);
	return 0;
}
EXPORT_SYMBOL(register_shrinker);

/*
 * Remove one
 */
void unregister_shrinker(struct shrinker *shrinker)
{
	if (!shrinker->nr_deferred)
		return;
	if (shrinker->flags & SHRINKER_MEMCG_AWARE)
		unregister_memcg_shrinker(shrinker);
	down_write(&shrinker_rwsem);
	list_del(&shrinker->list);
	up_write(&shrinker_rwsem);
	kfree(shrinker->nr_deferred);
	shrinker->nr_deferred = NULL;
}
EXPORT_SYMBOL(unregister_shrinker);

#define SHRINK_BATCH 128

/**
 *
 */
static unsigned long do_shrink_slab(struct shrink_control *shrinkctl,
				    struct shrinker *shrinker, int priority)
{
	unsigned long freed = 0;
	unsigned long long delta;
	long total_scan;
	long freeable;
	long nr;
	long new_nr;
	int nid = shrinkctl->nid;
	long batch_size = shrinker->batch ? shrinker->batch
					  : SHRINK_BATCH;
	long scanned = 0, next_deferred;

	if (!(shrinker->flags & SHRINKER_NUMA_AWARE))
		nid = 0;

    /**
     *
     */
	freeable = shrinker->count_objects(shrinker, shrinkctl);
	if (freeable == 0 || freeable == SHRINK_EMPTY)
		return freeable;

	/*
	 * copy the current shrinker scan count into a local variable
	 * and zero it so that other concurrent shrinker invocations
	 * don't also do this scanning work.
	 */
	nr = atomic_long_xchg(&shrinker->nr_deferred[nid], 0);

	total_scan = nr;
	if (shrinker->seeks) {
		delta = freeable >> priority;
		delta *= 4;
		do_div(delta, shrinker->seeks);
	} else {
		/*
		 * These objects don't require any IO to create. Trim
		 * them aggressively under memory pressure to keep
		 * them from causing refetches in the IO caches.
		 */
		delta = freeable / 2;
	}

	total_scan += delta;
	if (total_scan < 0) {
		pr_err("shrink_slab: %pS negative objects to delete nr=%ld\n",
		       shrinker->scan_objects, total_scan);
		total_scan = freeable;
		next_deferred = nr;
	} else
		next_deferred = total_scan;

	/*
	 * We need to avoid excessive windup on filesystem shrinkers
	 * due to large numbers of GFP_NOFS allocations causing the
	 * shrinkers to return -1 all the time. This results in a large
	 * nr being built up so when a shrink that can do some work
	 * comes along it empties the entire cache due to nr >>>
	 * freeable. This is bad for sustaining a working set in
	 * memory.
	 *
	 * Hence only allow the shrinker to scan the entire cache when
	 * a large delta change is calculated directly.
	 */
	if (delta < freeable / 4)
		total_scan = min(total_scan, freeable / 2);

	/*
	 * Avoid risking looping forever due to too large nr value:
	 * never try to free more than twice the estimate number of
	 * freeable entries.
	 */
	if (total_scan > freeable * 2)
		total_scan = freeable * 2;

	trace_mm_shrink_slab_start(shrinker, shrinkctl, nr,
				   freeable, delta, total_scan, priority);

	/*
	 * Normally, we should not scan less than batch_size objects in one
	 * pass to avoid too frequent shrinker calls, but if the slab has less
	 * than batch_size objects in total and we are really tight on memory,
	 * we will try to reclaim all available objects, otherwise we can end
	 * up failing allocations although there are plenty of reclaimable
	 * objects spread over several slabs with usage less than the
	 * batch_size.
	 *
	 * We detect the "tight on memory" situations by looking at the total
	 * number of objects we want to scan (total_scan). If it is greater
	 * than the total number of objects on slab (freeable), we must be
	 * scanning at high prio and therefore should try to reclaim as much as
	 * possible.
	 */
	while (total_scan >= batch_size ||
	       total_scan >= freeable) {
		unsigned long ret;
		unsigned long nr_to_scan = min(batch_size, total_scan);

		shrinkctl->nr_to_scan = nr_to_scan;
		shrinkctl->nr_scanned = nr_to_scan;
		ret = shrinker->scan_objects(shrinker, shrinkctl);
		if (ret == SHRINK_STOP)
			break;
		freed += ret;

		count_vm_events(SLABS_SCANNED, shrinkctl->nr_scanned);
		total_scan -= shrinkctl->nr_scanned;
		scanned += shrinkctl->nr_scanned;

		cond_resched();
	}

	if (next_deferred >= scanned)
		next_deferred -= scanned;
	else
		next_deferred = 0;
	/*
	 * move the unused scan count back into the shrinker in a
	 * manner that handles concurrent updates. If we exhausted the
	 * scan, there is no need to do an update.
	 */
	if (next_deferred > 0)
		new_nr = atomic_long_add_return(next_deferred,
						&shrinker->nr_deferred[nid]);
	else
		new_nr = atomic_long_read(&shrinker->nr_deferred[nid]);

	trace_mm_shrink_slab_end(shrinker, nid, freed, nr, new_nr, total_scan);
	return freed;
}

#ifdef CONFIG_MEMCG
static unsigned long shrink_slab_memcg(gfp_t gfp_mask, int nid,
			struct mem_cgroup *memcg, int priority)
{
	struct memcg_shrinker_map *map;
	unsigned long ret, freed = 0;
	int i;

	if (!mem_cgroup_online(memcg))
		return 0;

	if (!down_read_trylock(&shrinker_rwsem))
		return 0;

	map = rcu_dereference_protected(memcg->nodeinfo[nid]->shrinker_map,
					true);
	if (unlikely(!map))
		goto unlock;

	for_each_set_bit(i, map->map, shrinker_nr_max) {
		struct shrink_control sc = {
			.gfp_mask = gfp_mask,
			.nid = nid,
			.memcg = memcg,
		};
		struct shrinker *shrinker;

		shrinker = idr_find(&shrinker_idr, i);
		if (unlikely(!shrinker || shrinker == SHRINKER_REGISTERING)) {
			if (!shrinker)
				clear_bit(i, map->map);
			continue;
		}

		/* Call non-slab shrinkers even though kmem is disabled */
		if (!memcg_kmem_enabled() &&
		    !(shrinker->flags & SHRINKER_NONSLAB))
			continue;

		ret = do_shrink_slab(&sc, shrinker, priority);
		if (ret == SHRINK_EMPTY) {
			clear_bit(i, map->map);
			/*
			 * After the shrinker reported that it had no objects to
			 * free, but before we cleared the corresponding bit in
			 * the memcg shrinker map, a new object might have been
			 * added. To make sure, we have the bit set in this
			 * case, we invoke the shrinker one more time and reset
			 * the bit if it reports that it is not empty anymore.
			 * The memory barrier here pairs with the barrier in
			 * memcg_set_shrinker_bit():
			 *
			 * list_lru_add()     shrink_slab_memcg()
			 *   list_add_tail()    clear_bit()
			 *   <MB>               <MB>
			 *   set_bit()          do_shrink_slab()
			 */
			smp_mb__after_atomic();
			ret = do_shrink_slab(&sc, shrinker, priority);
			if (ret == SHRINK_EMPTY)
				ret = 0;
			else
				memcg_set_shrinker_bit(memcg, nid, i);
		}
		freed += ret;

		if (rwsem_is_contended(&shrinker_rwsem)) {
			freed = freed ? : 1;
			break;
		}
	}
unlock:
	up_read(&shrinker_rwsem);
	return freed;
}
#else /* CONFIG_MEMCG */

#endif /* CONFIG_MEMCG */

/**
 * shrink_slab - shrink slab caches
 * @gfp_mask: allocation context
 * @nid: node whose slab caches to target
 * @memcg: memory cgroup whose slab caches to target
 * @priority: the reclaim priority
 *
 * Call the shrink functions to age shrinkable caches.
 *
 * @nid is passed along to shrinkers with SHRINKER_NUMA_AWARE set,
 * unaware shrinkers will receive a node id of 0 instead.
 *
 * @memcg specifies the memory cgroup to target. Unaware shrinkers
 * are called only if it is the root cgroup.
 *
 * @priority is sc->priority, we take the number of objects and >> by priority
 * in order to get the scan target.
 *
 * Returns the number of reclaimed slab objects.
 */
static unsigned long shrink_slab(gfp_t gfp_mask, int nid,
                				 struct mem_cgroup *memcg,
                				 int priority)
{
	unsigned long ret, freed = 0;

    /*
     *  可以注册的回调，以便对可老化的缓存施加压力。
     */
	struct shrinker *shrinker;

	/*
	 * The root memcg might be allocated even though memcg is disabled
	 * via "cgroup_disable=memory" boot parameter.  This could make
	 * mem_cgroup_is_root() return false, then just run memcg slab
	 * shrink, but skip global shrink.  This may result in premature
	 * oom.
	 */
	if (!mem_cgroup_disabled() && !mem_cgroup_is_root(memcg))
		return shrink_slab_memcg(gfp_mask, nid, memcg, priority);

	if (!down_read_trylock(&shrinker_rwsem))
		goto out;

    /**
     *
     */
	list_for_each_entry(shrinker, &shrinker_list, list) {
		struct shrink_control sc = {
			sc.gfp_mask = gfp_mask,
			sc.nid = nid,
			sc.memcg = memcg,
		};

        /**
         *	收缩 slab ， 进行回收
         */
		ret = do_shrink_slab(&sc, shrinker, priority);
		if (ret == SHRINK_EMPTY)
			ret = 0;
		freed += ret;
		/*
		 * Bail out if someone want to register a new shrinker to
		 * prevent the registration from being stalled for long periods
		 * by parallel ongoing shrinking.
		 */
		if (rwsem_is_contended(&shrinker_rwsem)) {
			freed = freed ? : 1;
			break;
		}
	}

	up_read(&shrinker_rwsem);
out:
	cond_resched();
	return freed;
}

/**
 * @brief
 *
 * @param nid
 */
void drop_slab_node(int nid)
{
	unsigned long freed;

	do {
		struct mem_cgroup *memcg = NULL;

		if (fatal_signal_pending(current))
			return;

		freed = 0;
		/**
		 * @brief
		 *
		 */
		memcg = mem_cgroup_iter(NULL, NULL, NULL);
		do {
			freed += shrink_slab(GFP_KERNEL, nid, memcg, 0);
		} while ((memcg = mem_cgroup_iter(NULL, memcg, NULL)) != NULL);
	} while (freed > 10);
}

/**
 * @brief 回收 slab
 * 	 echo 2 > /proc/sys/vm/drop_caches
 */
void drop_slab(void)
{
	int nid;
	/**
	 * @brief 遍历所有 node
	 *
	 */
	for_each_online_node(nid) {
		drop_slab_node(nid);
	}
}

static inline int is_page_cache_freeable(struct page *page)
{
	/*
	 * A freeable page cache page is referenced only by the caller
	 * that isolated the page, the page cache and optional buffer
	 * heads at page->private.
	 */
	int page_cache_pins = thp_nr_pages(page);
	return page_count(page) - page_has_private(page) == 1 + page_cache_pins;
}

static int may_write_to_inode(struct inode *inode)
{
	if (current->flags & PF_SWAPWRITE)
		return 1;
	if (!inode_write_congested(inode))
		return 1;
	if (inode_to_bdi(inode) == current->backing_dev_info)
		return 1;
	return 0;
}

/*
 * We detected a synchronous write error writing a page out.  Probably
 * -ENOSPC.  We need to propagate that into the address_space for a subsequent
 * fsync(), msync() or close().
 *
 * The tricky part is that after writepage we cannot touch the mapping: nothing
 * prevents it from being freed up.  But we have a ref on the page and once
 * that page is locked, the mapping is pinned.
 *
 * We're allowed to run sleeping lock_page() here because we know the caller has
 * __GFP_FS.
 */
static void handle_write_error(struct address_space *mapping,
				struct page *page, int error)
{
	lock_page(page);
	if (page_mapping(page) == mapping)
		mapping_set_error(mapping, error);
	unlock_page(page);
}

/* possible outcome of pageout() */
typedef enum {
	/* failed to write page out, page is locked */
	PAGE_KEEP,      //标识回写页面失败
	/* move page to the active list, page is locked */
	PAGE_ACTIVATE,  //标识回写页面失败
	/* page has been sent to the disk successfully, page is unlocked */
	PAGE_SUCCESS,   //标识页面已经成功写入存储设备
	/* page is clean and locked */
	PAGE_CLEAN,     //标识页面已经干净，可以释放了
} pageout_t;

/*
 * pageout is called by shrink_page_list() for each dirty page.
 * Calls ->writepage().
 *
 * 当一个脏页必须写回磁盘，写入交换分区
 * 返回值
 * PAGE_KEEP        标识回写页面失败
 * PAGE_ACTIVATE    标识页面需要迁移会活跃链表中
 * PAGE_SUCCESS     标识页面已经成功写入存储设备
 * PAGE_CLEAN       标识页面已经干净，可以释放了
 */
static pageout_t pageout(struct page *page, struct address_space *mapping)
{
	/*
	 * If the page is dirty, only perform writeback if that write
	 * will be non-blocking.  To prevent this allocation from being
	 * stalled by pagecache activity.  But note that there may be
	 * stalls if we need to run get_block().  We could test
	 * PagePrivate for that.
	 *
	 * If this process is currently in __generic_file_write_iter() against
	 * this page's queue, we can perform writeback even if that
	 * will block.
	 *
	 * If the page is swapcache, write it back even if that would
	 * block, for some throttling. This happens by accident, because
	 * swap_backing_dev_info is bust: it doesn't reflect the
	 * congestion state of the swapdevs.  Easy to fix, if needed.
	 */
	if (!is_page_cache_freeable(page))
		return PAGE_KEEP;

    /**
     *
     */
	if (!mapping) {
		/*
		 * Some data journaling orphaned pages can have
		 * page->mapping == NULL while being dirty with clean buffers.
		 */
		if (page_has_private(page)) {
			if (try_to_free_buffers(page)) {
				ClearPageDirty(page);
				pr_info("%s: orphaned page\n", __func__);
				return PAGE_CLEAN;
			}
		}
		return PAGE_KEEP;
	}
	if (mapping->a_ops->writepage == NULL)
		return PAGE_ACTIVATE;
	if (!may_write_to_inode(mapping->host))
		return PAGE_KEEP;

    /**
     *
     */
	if (clear_page_dirty_for_io(page)) {
		int res;
		struct writeback_control wbc = {
			wbc.sync_mode = WB_SYNC_NONE,
			wbc.nr_to_write = SWAP_CLUSTER_MAX,
			wbc.range_start = 0,
			wbc.range_end = LLONG_MAX,
			wbc.for_reclaim = 1,
		};

		SetPageReclaim(page);
		res = mapping->a_ops->writepage(page, &wbc);
		if (res < 0)
			handle_write_error(mapping, page, res);
		if (res == AOP_WRITEPAGE_ACTIVATE) {
			ClearPageReclaim(page);
			return PAGE_ACTIVATE;
		}

		if (!PageWriteback(page)) {
			/* synchronous write or broken a_ops? */
			ClearPageReclaim(page);
		}
		trace_mm_vmscan_writepage(page);
		inc_node_page_state(page, NR_VMSCAN_WRITE);
		return PAGE_SUCCESS;
	}

	return PAGE_CLEAN;
}

/*
 * Same as remove_mapping, but if the page is removed from the mapping, it
 * gets returned with a refcount of 0.
 *
 * 尝试分离 page->mapping
 */
static int
__remove_mapping(struct address_space *mapping, struct page *page,
			    bool reclaimed, struct mem_cgroup *target_memcg)
{
	unsigned long flags;
	int refcount;
	void *shadow = NULL;

	BUG_ON(!PageLocked(page));
	BUG_ON(mapping != page_mapping(page));

	xa_lock_irqsave(&mapping->i_pages, flags);
	/*
	 * The non racy check for a busy page.
	 *
	 * Must be careful with the order of the tests. When someone has
	 * a ref to the page, it may be possible that they dirty it then
	 * drop the reference. So if PageDirty is tested before page_count
	 * here, then the following race may occur:
	 *
	 * get_user_pages(&page);
	 * [user mapping goes away]
	 * write_to(page);
	 *				!PageDirty(page)    [good]
	 * SetPageDirty(page);
	 * put_page(page);
	 *				!page_count(page)   [good, discard it]
	 *
	 * [oops, our write_to data is lost]
	 *
	 * Reversing the order of the tests ensures such a situation cannot
	 * escape unnoticed. The smp_rmb is needed to ensure the page->flags
	 * load is not satisfied before that of page->_refcount.
	 *
	 * Note that if SetPageDirty is always performed via set_page_dirty,
	 * and thus under the i_pages lock, then this ordering is not required.
	 */
	refcount = 1 + compound_nr(page);
	if (!page_ref_freeze(page, refcount))
		goto cannot_free;
	/* note: atomic_cmpxchg in page_ref_freeze provides the smp_rmb */
	if (unlikely(PageDirty(page))) {
		page_ref_unfreeze(page, refcount);
		goto cannot_free;
	}

    /**
     *	swap
     */
	if (PageSwapCache(page)) {
		swp_entry_t swap = { .val = page_private(page) };
		mem_cgroup_swapout(page, swap);

        /**
         *
         */
		if (reclaimed && !mapping_exiting(mapping))
			shadow = workingset_eviction(page, target_memcg);

		/**
		 * @brief Swap 也有 pagecache?
		 *
		 */
        __delete_from_swap_cache(page, swap, shadow);
		xa_unlock_irqrestore(&mapping->i_pages, flags);
		put_swap_page(page, swap);

    /**
     *	filemap pagecache
     */
	} else {
		void (*freepage)(struct page *);

		freepage = mapping->a_ops->freepage;
		/*
		 * Remember a shadow entry for reclaimed file cache in
		 * order to detect refaults, thus thrashing, later on.
		 *
		 * But don't store shadows in an address space that is
		 * already exiting.  This is not just an optimization,
		 * inode reclaim needs to empty out the radix tree or
		 * the nodes are lost.  Don't plant shadows behind its
		 * back.
		 *
		 * We also don't store shadows for DAX mappings because the
		 * only page cache pages found in these are zero pages
		 * covering holes, and because we don't want to mix DAX
		 * exceptional entries and shadow exceptional entries in the
		 * same address_space.
		 */
		if (reclaimed && page_is_file_lru(page) &&
		    !mapping_exiting(mapping) && !dax_mapping(mapping))

            /**
             *  在 不活跃LRU链表末尾的页面会被
             *  1. 移除 LRU 链表 -> workingset_eviction
             *  2. 释放 -> __delete_from_page_cache
             *
             *  shadow 值是经过简单编码的
             */
			shadow = workingset_eviction(page, target_memcg);

        /**
         *  释放, 把当前的 lruvec inactive-age 保存在 该页面对应的radix tree 中
         */
		__delete_from_page_cache(page, shadow);
		xa_unlock_irqrestore(&mapping->i_pages, flags);

		if (freepage != NULL)
			freepage(page);
	}

	return 1;

cannot_free:
	xa_unlock_irqrestore(&mapping->i_pages, flags);
	return 0;
}

/*
 * Attempt to detach a locked page from its ->mapping.  If it is dirty or if
 * someone else has a ref on the page, abort and return 0.  If it was
 * successfully detached, return 1.  Assumes the caller has a single ref on
 * this page.
 */
int remove_mapping(struct address_space *mapping, struct page *page)
{
	if (__remove_mapping(mapping, page, false, NULL)) {
		/*
		 * Unfreezing the refcount with 1 rather than 2 effectively
		 * drops the pagecache ref for us without requiring another
		 * atomic operation.
		 */
		page_ref_unfreeze(page, 1);
		return 1;
	}
	return 0;
}

/**
 * putback_lru_page - put previously isolated page onto appropriate LRU list
 * @page: page to be put back to appropriate lru list
 *
 * Add previously isolated @page to appropriate LRU list.
 * Page may still be unevictable for other reasons.
 *
 * lru_lock must not be held, interrupts must be enabled.
 */
void putback_lru_page(struct page *page)
{
	lru_cache_add(page);
	put_page(page);		/* drop ref from isolate */
}

enum page_references {  /* 页引用 */
	PAGEREF_RECLAIM,        /* 回收 */
	PAGEREF_RECLAIM_CLEAN,  /* 回收清理 */
	PAGEREF_KEEP,           /* 保持不变 */
	PAGEREF_ACTIVATE,       /* 激活 */
};

/**
 *  页面中 访问和引用 PTE 用户数
 *
 *

*/
static enum page_references page_check_references(struct page *page,
						  struct scan_control *sc)
{
	int referenced_ptes, referenced_page;
	unsigned long vm_flags;

    /**
     *  检查该页面 访问、引用了多少个PTE
     */
	referenced_ptes = page_referenced(page, 1, sc->target_mem_cgroup, &vm_flags);

    /* 返回 页面 PG_referenced 标志位的值，并清除该标志位 */
	referenced_page = TestClearPageReferenced(page);

	/*
	 * Mlock lost the isolation race with us.  Let try_to_unmap()
	 * move the page to the unevictable list.
	 */
	if (vm_flags & VM_LOCKED)
		return PAGEREF_RECLAIM;

    /**
     *
     *
     */
	if (referenced_ptes) {
		/*
		 * All mapped pages start out with page table
		 * references from the instantiating fault, so we need
		 * to look twice if a mapped file page is used more
		 * than once.
		 *
		 * Mark it and spare it for another trip around the
		 * inactive list.  Another page table reference will
		 * lead to its activation.
		 *
		 * Note: the mark is set for activated pages as well
		 * so that recently deactivated but used pages are
		 * quickly recovered.
		 */
		SetPageReferenced(page);

        /**
         *  如果有多个文件同时映射到该页面
         *  他们应该晋升到 活跃链表
         */
		if (referenced_page || referenced_ptes > 1)
			return PAGEREF_ACTIVATE;    /* 页面将会迁移到 活跃链表 */

		/*
		 * Activate file-backed executable pages after first usage.
		 */
		if ((vm_flags & VM_EXEC) && !PageSwapBacked(page))
			return PAGEREF_ACTIVATE;

		return PAGEREF_KEEP;    /* 页面位置不变 */
	}

    /**
     *  如果页面没有访问、引用pte，可以尝试回收
     */
	/* Reclaim if clean, defer dirty pages to writeback */
	if (referenced_page && !PageSwapBacked(page))
        /**
         *  回收并清理
         */
		return PAGEREF_RECLAIM_CLEAN;   /* 可以尝试回收页面 */

	return PAGEREF_RECLAIM; /* 可以尝试回收页面 */
}

/**
 *  Check if a page is dirty or under writeback
 *  检查这个页面是不是脏页 或者 正在回写的页面
 */
static void page_check_dirty_writeback(struct page *page,
				       bool *dirty, bool *writeback)
{
	struct address_space *mapping;

	/*
	 * Anonymous pages are not handled by flushers and must be written
	 * from reclaim context. Do not stall reclaim based on them
	 */
	if (!page_is_file_lru(page) ||
	    (PageAnon(page) && !PageSwapBacked(page))) {
		*dirty = false;
		*writeback = false;
		return;
	}

    /**
     *  是脏页
     */
	/* By default assume that the page flags are accurate */
	*dirty = PageDirty(page);

    /**
     *  正在写回
     */
	*writeback = PageWriteback(page);

	/* Verify dirty/writeback state if the filesystem supports it */
	if (!page_has_private(page))
		return;

    /**
     *
     */
	mapping = page_mapping(page);

    /**
     *
     */
	if (mapping && mapping->a_ops->is_dirty_writeback)
        /**
         *
         */
		mapping->a_ops->is_dirty_writeback(page, dirty, writeback);
}

/*
 * shrink_page_list() returns the number of reclaimed pages
 *
 * @page_list   待回收的页面链表
 * @pgdat       节点
 * @sc          扫描控制结构
 * @stat        回收状态
 *
 * 扫描页面并回收 的核心函数
 */
static unsigned int shrink_page_list(struct list_head *page_list,
				     struct pglist_data *pgdat,
				     struct scan_control *sc,
				     struct reclaim_stat *stat,
				     bool ignore_references)
{
	LIST_HEAD(ret_pages);
	LIST_HEAD(free_pages);
    struct list_head ret_pages, free_pages;//+++

	unsigned int nr_reclaimed = 0;
	unsigned int pgactivate = 0;

	memset(stat, 0, sizeof(*stat));

    /**
     *
     */
	cond_resched();

    /**
     *  循环 page_list 链表，这个链表的成员都不是活跃的
     */
	while (!list_empty(page_list)) {

        /**
         *  注意 mapping 结构
         */
		struct address_space *mapping;
		struct page *page;
		enum page_references references = PAGEREF_RECLAIM;
		bool dirty, writeback, may_enter_fs;
		unsigned int nr_pages;

		cond_resched();

        /**
         *  从链表中 取 页面，并从链表移除
         */
		page = lru_to_page(page_list);
		list_del(&page->lru);

        /**
         *  如果获取页面锁失败，证明有人用呗，那么继续保留在 lru 链表中
         */
		if (!trylock_page(page))
			goto keep;

		VM_BUG_ON_PAGE(PageActive(page), page);

		nr_pages = compound_nr(page);

		/* Account the number of base pages even though THP */
		sc->nr_scanned += nr_pages;

        /**
         *  若该页面属于不可回收页面，跳转
         */
		if (unlikely(!page_evictable(page)))
			goto activate_locked;

        /**
         *  是否允许回收映射的页面
         */
		if (!sc->may_unmap && page_mapped(page))
			goto keep_locked;

        /**
         *
         */
		may_enter_fs = (sc->gfp_mask & __GFP_FS) || (PageSwapCache(page) && (sc->gfp_mask & __GFP_IO));

		/*
		 * The number of dirty pages determines if a node is marked
		 * reclaim_congested which affects wait_iff_congested. kswapd
		 * will stall and start writing pages if the tail of the LRU
		 * is all dirty unqueued pages.
		 *
		 * 检查这个页面是不是脏页 或者 正在回写的页面
		 *
		 * 如果是，nr_dirty++
		 */
		page_check_dirty_writeback(page, &dirty, &writeback);
		if (dirty || writeback)
			stat->nr_dirty++;

        /**
         *  如果是脏页，但是还没有被回写，
         */
		if (dirty && !writeback)
			stat->nr_unqueued_dirty++;

		/*
		 * Treat this page as congested if the underlying BDI is or if
		 * pages are cycling through the LRU so quickly that the
		 * pages marked for immediate reclaim are making it to the
		 * end of the LRU a second time.
		 */
		mapping = page_mapping(page);

        /**
         *  这是一个 处于临时状态的匿名页面，即将要被回收和释放的匿名页面
         */
		if (((dirty || writeback) && mapping &&
		     inode_write_congested(mapping->host)) || (writeback && PageReclaim(page)))
			stat->nr_congested++;

		/*
		 * If a page at the tail of the LRU is under writeback, there
		 * are three cases to consider.
		 *
		 * 1) If reclaim is encountering an excessive number of pages
		 *    under writeback and this page is both under writeback and
		 *    PageReclaim then it indicates that pages are being queued
		 *    for IO but are being recycled through the LRU before the
		 *    IO can complete. Waiting on the page itself risks an
		 *    indefinite stall if it is impossible to writeback the
		 *    page due to IO error or disconnected storage so instead
		 *    note that the LRU is being scanned too quickly and the
		 *    caller can stall after page list has been processed.
		 * 译文:
		 *    当遇到大量页面正在回写，并且当前页面也处于回写状态时，说明这些
		 *    页面都在等待磁盘IO，当磁盘IO完成时候，便可以回收这些页面。
		 *    如果我们在这里等待这些页面回写完成，那可能会导致无限期的等待，
		 *    因为可能出现磁盘IO出错或者磁盘链接错误等问题。
		 *    如果当前回收者是 kswapd 内核线程，并且有大量正在回写的页面，
		 *    那么我们增加 nr_immediate 计数，跳转到 activate_locked 标签处，
		 *    继续扫描 page_list 链表，而不是睡眠等待页面回写完成。
		 *
		 *
		 * 2) Global or new memcg reclaim encounters a page that is
		 *    not marked for immediate reclaim, or the caller does not
		 *    have __GFP_FS (or __GFP_IO if it's simply going to swap,
		 *    not to fs). In this case mark the page for immediate
		 *    reclaim and continue scanning.
		 *
		 *    Require may_enter_fs because we would wait on fs, which
		 *    may not have submitted IO yet. And the loop driver might
		 *    enter reclaim, and deadlock if it waits on a page for
		 *    which it is needed to do the write (loop masks off
		 *    __GFP_IO|__GFP_FS for this reason); but more thought
		 *    would probably show more reasons.
		 * 译文:
		 *    若一个页面没有标记回收，或者页面分配器的调用者 没有使用 __GFP_FS 或者 __GFP_IO
		 *    那么我们为这个页面这只 PG_PageReclaim 标志位，增加 nr_writeback  计数，然后跳转到
		 *    activate_locked 标签处，继续扫描page_list 链表
		 *
		 *
		 * 3) Legacy memcg encounters a page that is already marked
		 *    PageReclaim. memcg does not have any dirty pages
		 *    throttling so we could easily OOM just because too many
		 *    pages are in writeback and there is nothing else to
		 *    reclaim. Wait for the writeback to complete.
		 * 译文:
		 *    这种情况下，会睡眠等待 回写完成
		 *
		 *
		 * In cases 1) and 2) we activate the pages to get them out of
		 * the way while we continue scanning for clean pages on the
		 * inactive list and refilling from the active list. The
		 * observation here is that waiting for disk writes is more
		 * expensive than potentially causing reloads down the line.
		 * Since they're marked for immediate reclaim, they won't put
		 * memory pressure on the cache working set any longer than it
		 * takes to write them to disk.
		 */
		/**
         *  正在写回
         */
		if (PageWriteback(page)) {
            /**
             *  kswapd 线程
    		 *    如果当前回收者是 kswapd 内核线程，并且有大量正在回写的页面，
    		 *    那么我们增加 nr_immediate 计数，跳转到 activate_locked 标签处，
    		 *    继续扫描 page_list 链表，而不是睡眠等待页面回写完成。
             */
			/* Case 1 above */
			if (current_is_kswapd() && PageReclaim(page) && test_bit(PGDAT_WRITEBACK, &pgdat->flags)) {
				stat->nr_immediate++;
				goto activate_locked;

            /**
             *
    		 *    若一个页面没有标记回收，或者页面分配器的调用者 没有使用 __GFP_FS 或者 __GFP_IO
    		 *    那么我们为这个页面这只 PG_PageReclaim 标志位，增加 nr_writeback  计数，然后跳转到
    		 *    activate_locked 标签处，继续扫描page_list 链表
             */
			/* Case 2 above */
			} else if (writeback_throttling_sane(sc) || !PageReclaim(page) || !may_enter_fs) {
				/*
				 * This is slightly racy - end_page_writeback()
				 * might have just cleared PageReclaim, then
				 * setting PageReclaim here end up interpreted
				 * as PageReadahead - but that does not matter
				 * enough to care.  What we do want is for this
				 * page to have PageReclaim set next time memcg
				 * reclaim reaches the tests above, so it will
				 * then wait_on_page_writeback() to avoid OOM;
				 * and it's also appropriate in global reclaim.
				 */
				SetPageReclaim(page);
				stat->nr_writeback++;
				goto activate_locked;

            /**
             *    这种情况下，会睡眠等待 回写完成
             */
			/* Case 3 above */
			} else {
			    /**
                 *  睡眠等待回写完成
                 */
				unlock_page(page);
				wait_on_page_writeback(page);
				/* then go back and try same page again */
				list_add_tail(&page->lru, page_list);
				continue;
			}
		}

        /**
         *  监测页面被引用情况
         */
		if (!ignore_references)
			references = page_check_references(page, sc);

        /**
         *
         */
		switch (references) {
		case PAGEREF_ACTIVATE:
			goto activate_locked;
        /**
         *
         */
		case PAGEREF_KEEP:
			stat->nr_ref_keep += nr_pages;
			goto keep_locked;
        /**
         *  回收
         */
		case PAGEREF_RECLAIM:
		case PAGEREF_RECLAIM_CLEAN:
			; /* try to reclaim the page below */
		}

		/*
		 * Anonymous process memory has backing store?
		 * Try to allocate it some swap space here.
		 * Lazyfree page could be freed directly
		 *
		 * 这是一个匿名页面
		 */
		if (PageAnon(page) && PageSwapBacked(page)) {
            /**
             *  还没有为 页面分配交换空间
             */
			if (!PageSwapCache(page)) {

				if (!(sc->gfp_mask & __GFP_IO))
					goto keep_locked;
				if (page_maybe_dma_pinned(page))
					goto keep_locked;
				if (PageTransHuge(page)) {
					/* cannot split THP, skip it */
					if (!can_split_huge_page(page, NULL))
						goto activate_locked;
					/*
					 * Split pages without a PMD map right
					 * away. Chances are some or all of the
					 * tail pages can be freed without IO.
					 */
					if (!compound_mapcount(page) &&
					    split_huge_page_to_list(page,
								    page_list))
						goto activate_locked;
				}
                /**
                 *  分配交换空间
                 */
				if (!add_to_swap(page)) {
					if (!PageTransHuge(page))
						goto activate_locked_split;
					/* Fallback to swap normal pages */
					if (split_huge_page_to_list(page, page_list))
						goto activate_locked;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
					count_vm_event(THP_SWPOUT_FALLBACK);
#endif
					if (!add_to_swap(page))
						goto activate_locked_split;
				}

				may_enter_fs = true;

                /**
                 *  为页面分配了交换空间后，mapping 的只想繁盛了变换
                 *  有匿名页面的 anon_vma 变成了 交换分区的 swapper_spaces
                 */
				/* Adding to swap updated mapping */
				mapping = page_mapping(page);
			}
		}
        /**
         *
         */
        else if (unlikely(PageTransHuge(page))) {
			/* Split file THP */
			if (split_huge_page_to_list(page, page_list))
				goto keep_locked;
		}

		/*
		 * THP may get split above, need minus tail pages and update
		 * nr_pages to avoid accounting tail pages twice.
		 *
		 * The tail pages that are added into swap cache successfully
		 * reach here.
		 */
		if ((nr_pages > 1) && !PageTransHuge(page)) {
			sc->nr_scanned -= (nr_pages - 1);
			nr_pages = 1;
		}

		/*
		 * The page is mapped into the page tables of one or more
		 * processes. Try to unmap it here.
		 *
		 * 页面有一个或多个用户映射
		 */
		if (page_mapped(page)) {
			enum ttu_flags flags = TTU_BATCH_FLUSH;
			bool was_swapbacked = PageSwapBacked(page);

			if (unlikely(PageTransHuge(page)))
				flags |= TTU_SPLIT_HUGE_PMD;

            /**
             *  解除用户映射 PTE
             */
            /* 使用 RMAP 机制 unmap */
			if (!try_to_unmap(page, flags)) {
				stat->nr_unmap_fail += nr_pages;
				if (!was_swapbacked && PageSwapBacked(page))
					stat->nr_lazyfree_fail += nr_pages;
				goto activate_locked;
			}
		}

        /**
         *  处理页面是脏页的情况
         */
		if (PageDirty(page)) {
			/*
			 * Only kswapd can writeback filesystem pages
			 * to avoid risk of stack overflow. But avoid
			 * injecting inefficient single-page IO into
			 * flusher writeback as much as possible: only
			 * write pages when we've encountered many
			 * dirty pages, and when we've already scanned
			 * the rest of the LRU for clean pages and see
			 * the same dirty pages again (PageReclaim).
			 */
			/**
             *  如果页面是文件映射页面
             *
             * kswapd 不会对零星的几个 内容缓存页面进行回写，
             * 除非有很多没有回写的脏页。
             */
			if (page_is_file_lru(page) &&
			    (!current_is_kswapd() || !PageReclaim(page) ||
			     !test_bit(PGDAT_DIRTY, &pgdat->flags))) {
				/*
				 * Immediately reclaim when written back.
				 * Similar in principal to deactivate_page()
				 * except we already have the page isolated
				 * and know it's dirty
				 */
				inc_node_page_state(page, NR_VMSCAN_IMMEDIATE);
				SetPageReclaim(page);

				goto activate_locked;
			}

			if (references == PAGEREF_RECLAIM_CLEAN)
				goto keep_locked;
			if (!may_enter_fs)
				goto keep_locked;
			if (!sc->may_writepage)
				goto keep_locked;

			/*
			 * Page is dirty. Flush the TLB if a writable entry
			 * potentially exists to avoid CPU writes after IO
			 * starts and then write it out here.
			 */
			try_to_unmap_flush_dirty();

            /**
             *  如果是匿名页面，写如交换分区
             *
             * 返回值
             * PAGE_KEEP        标识回写页面失败
             * PAGE_ACTIVATE    标识页面需要迁移会活跃链表中
             * PAGE_SUCCESS     标识页面已经成功写入存储设备
             * PAGE_CLEAN       标识页面已经干净，可以释放了
             */
			switch (pageout(page, mapping)) {
			case PAGE_KEEP:
				goto keep_locked;
			case PAGE_ACTIVATE:
				goto activate_locked;
			case PAGE_SUCCESS:
				stat->nr_pageout += thp_nr_pages(page);

				if (PageWriteback(page))
					goto keep;
				if (PageDirty(page))
					goto keep;

				/*
				 * A synchronous write - probably a ramdisk.  Go
				 * ahead and try to reclaim the page.
				 */
				if (!trylock_page(page))
					goto keep;
				if (PageDirty(page) || PageWriteback(page))
					goto keep_locked;
				mapping = page_mapping(page);
			case PAGE_CLEAN:
				; /* try to free the page below */
			}
		}

		/*
		 * If the page has buffers, try to free the buffer mappings
		 * associated with this page. If we succeed we try to free
		 * the page as well.
		 *
		 * We do this even if the page is PageDirty().
		 * try_to_release_page() does not perform I/O, but it is
		 * possible for a page to have PageDirty set, but it is actually
		 * clean (all its buffers are clean).  This happens if the
		 * buffers were written out directly, with submit_bh(). ext3
		 * will do this, as well as the blockdev mapping.
		 * try_to_release_page() will discover that cleanness and will
		 * drop the buffers and mark the page clean - it can be freed.
		 *
		 * Rarely, pages can have buffers and no ->mapping.  These are
		 * the pages which were not successfully invalidated in
		 * truncate_complete_page().  We try to drop those buffers here
		 * and if that worked, and the page is no longer mapped into
		 * process address space (page_count == 1) it can be freed.
		 * Otherwise, leave the page on the LRU so it is swappable.
		 */
		/**
         *  处理页面用户块设备的 buffer_head 缓存
         */
		if (page_has_private(page)) {

            /**
             *  释放 buffer_head 缓存
             */
			if (!try_to_release_page(page, sc->gfp_mask))
				goto activate_locked;
			if (!mapping && page_count(page) == 1) {
				unlock_page(page);
				if (put_page_testzero(page))
					goto free_it;
				else {
					/*
					 * rare race with speculative reference.
					 * the speculative reference will free
					 * this page shortly, so we may
					 * increment nr_reclaimed here (and
					 * leave it off the LRU).
					 */
					nr_reclaimed++;
					continue;
				}
			}
		}

        /**
         *  若这些匿名页面清除了 PG_swapbasked 标志位，通常标识这些匿名页面马上就要释放了
         */
		if (PageAnon(page) && !PageSwapBacked(page)) {
			/* follow __remove_mapping for reference */

            /**
             *  直接释放这个页面
             */
			if (!page_ref_freeze(page, 1))
				goto keep_locked;
			if (PageDirty(page)) {
				page_ref_unfreeze(page, 1);
				goto keep_locked;
			}

			count_vm_event(PGLAZYFREED);
			count_memcg_page_event(page, PGLAZYFREED);

		}
        /**
         *  尝试分离 page->mapping
         *  程序执行到这里，说明该页面已经完成了大部分的回收工作。
         */
        else if (!mapping || !__remove_mapping(mapping, page, true,
							 sc->target_mem_cgroup))
			goto keep_locked;

		unlock_page(page);
free_it:
		/*
		 * THP may get swapped out in a whole, need account
		 * all base pages.
		 *
		 * 统计已经回收的页面数量
		 */
		nr_reclaimed += nr_pages;

		/*
		 * Is there need to periodically free_page_list? It would
		 * appear not as the counts should be low
		 */
		if (unlikely(PageTransHuge(page)))
			destroy_compound_page(page);
		else
            /**
             *  讲这些回收的 页面 放到 free_pages 链表
             */
			list_add(&page->lru, &free_pages);
		continue;

activate_locked_split:
		/*
		 * The tail pages that are failed to add into swap cache
		 * reach here.  Fixup nr_scanned and nr_pages.
		 */
		if (nr_pages > 1) {
			sc->nr_scanned -= (nr_pages - 1);
			nr_pages = 1;
		}

/**
 *  改标签标识页面不能回收，需要重新返回 LRU 链表
 */
activate_locked:
		/* Not a candidate for swapping, so reclaim swap space. */
		if (PageSwapCache(page) && (mem_cgroup_swap_full(page) || PageMlocked(page)))
			try_to_free_swap(page);

        VM_BUG_ON_PAGE(PageActive(page), page);

		if (!PageMlocked(page)) {
			int type = page_is_file_lru(page);
			SetPageActive(page);
			stat->nr_activate[type] += nr_pages;
			count_memcg_page_event(page, PGACTIVATE);
		}
keep_locked:
		unlock_page(page);

/**
 *  keep 标签标识让页面继续报纸在不活跃 LRU 链表中
 */
keep:
        /**
         *  keep 标签: 各种原因导致 页面继续保留在 lru 链表中
         */
		list_add(&page->lru, &ret_pages);
		VM_BUG_ON_PAGE(PageLRU(page) || PageUnevictable(page), page);
	}

	pgactivate = stat->nr_activate[0] + stat->nr_activate[1];

	mem_cgroup_uncharge_list(&free_pages);
	try_to_unmap_flush();
	free_unref_page_list(&free_pages);

	list_splice(&ret_pages, page_list);

    /**
     *  更新 vmstat 信息
     */
	count_vm_events(PGACTIVATE, pgactivate);

    /**
     *  返回成功回收的页面数量
     */
	return nr_reclaimed;
}

unsigned int reclaim_clean_pages_from_list(struct zone *zone,
					    struct list_head *page_list)
{
	struct scan_control sc = {
		.gfp_mask = GFP_KERNEL,
		.priority = DEF_PRIORITY,
		.may_unmap = 1,
	};
	struct reclaim_stat stat;
	unsigned int nr_reclaimed;
	struct page *page, *next;
	LIST_HEAD(clean_pages);

	list_for_each_entry_safe(page, next, page_list, lru) {
		if (page_is_file_lru(page) && !PageDirty(page) &&
		    !__PageMovable(page) && !PageUnevictable(page)) {
			ClearPageActive(page);
			list_move(&page->lru, &clean_pages);
		}
	}

	nr_reclaimed = shrink_page_list(&clean_pages, zone->zone_pgdat, &sc,
					&stat, true);
	list_splice(&clean_pages, page_list);
	mod_node_page_state(zone->zone_pgdat, NR_ISOLATED_FILE,
			    -(long)nr_reclaimed);
	/*
	 * Since lazyfree pages are isolated from file LRU from the beginning,
	 * they will rotate back to anonymous LRU in the end if it failed to
	 * discard so isolated count will be mismatched.
	 * Compensate the isolated count for both LRU lists.
	 */
	mod_node_page_state(zone->zone_pgdat, NR_ISOLATED_ANON,
			    stat.nr_lazyfree_fail);
	mod_node_page_state(zone->zone_pgdat, NR_ISOLATED_FILE,
			    -(long)stat.nr_lazyfree_fail);
	return nr_reclaimed;
}

/*
 * Attempt to remove the specified page from its LRU.  Only take this page
 * if it is of the appropriate PageActive status.  Pages which are being
 * freed elsewhere are also ignored.
 *
 * page:	page to consider
 * mode:	one of the LRU isolation modes defined above
 *
 * returns 0 on success, -ve errno on failure.
 *
 * 分离页面 的核心函数
 */
int __isolate_lru_page(struct page *page, isolate_mode_t mode)
{
	int ret = -EINVAL;

	/* Only take pages on the LRU. 价差标记位 */
	if (!PageLRU(page))
		return ret;

    /**
     *  页面是不可回收的， 模式 不是 分离不可回收，则返回
     */
	/* Compaction should not handle unevictable pages but CMA can do so */
	if (PageUnevictable(page) && !(mode & ISOLATE_UNEVICTABLE))
		return ret;

	ret = -EBUSY;

	/*
	 * To minimise LRU disruption, the caller can indicate that it only
	 * wants to isolate pages it will be able to operate on without
	 * blocking - clean pages for the most part.
	 *
	 * ISOLATE_ASYNC_MIGRATE is used to indicate that it only wants to pages
	 * that it is possible to migrate without blocking
	 */
	if (mode & ISOLATE_ASYNC_MIGRATE) {
		/* All the caller can do on PageWriteback is block */
		if (PageWriteback(page))
			return ret;

		if (PageDirty(page)) {
			struct address_space *mapping;
			bool migrate_dirty;

			/*
			 * Only pages without mappings or that have a
			 * ->migratepage callback are possible to migrate
			 * without blocking. However, we can be racing with
			 * truncation so it's necessary to lock the page
			 * to stabilise the mapping as truncation holds
			 * the page lock until after the page is removed
			 * from the page cache.
			 */
			if (!trylock_page(page))
				return ret;

            /**
             *
             */
			mapping = page_mapping(page);
			migrate_dirty = !mapping || mapping->a_ops->migratepage;

			unlock_page(page);
			if (!migrate_dirty)
				return ret;
		}
	}

	if ((mode & ISOLATE_UNMAPPED) && page_mapped(page))
		return ret;

    /**
     *  判断page->_refcount是否为0，
     */
	if (likely(get_page_unless_zero(page))) {
		/*
		 * Be careful not to clear PageLRU until after we're
		 * sure the page is not being freed elsewhere -- the
		 * page release code relies on it.
		 */
		ClearPageLRU(page);
		ret = 0;
	}

	return ret;
}


/*
 * Update LRU sizes after isolating pages. The LRU size updates must
 * be complete before mem_cgroup_update_lru_size due to a sanity check.
 */
static __always_inline void update_lru_sizes(struct lruvec *lruvec,
			enum lru_list lru, unsigned long *nr_zone_taken)
{
	int zid;

	for (zid = 0; zid < MAX_NR_ZONES; zid++) {
		if (!nr_zone_taken[zid])
			continue;

		update_lru_size(lruvec, lru, zid, -nr_zone_taken[zid]);
	}

}

/**
 * pgdat->lru_lock is heavily contended.  Some of the functions that
 * shrink the lists perform better by taking out a batch of pages
 * and working on them outside the LRU lock.
 *
 * For pagecache intensive workloads, this function is the hottest
 * spot in the kernel (apart from copy_*_user functions).
 *
 * Appropriate locks must be held before calling this function.
 *
 * @nr_to_scan:	The number of eligible pages to look through on the list.
 * @lruvec:	The LRU vector to pull pages from.
 * @dst:	The temp list to put pages on to.
 * @nr_scanned:	The number of pages that were scanned.
 * @sc:		The scan_control struct for this reclaim session
 * @lru:	LRU list id for isolating
 *
 * returns how many pages were moved onto *@dst.
 *
 * 将LRU 中的 page 移至 dst 链表中
 *  把一部分 页面先放到 临时链表里，减小对 lru 链表的加锁时间
 */
static unsigned long isolate_lru_pages(unsigned long nr_to_scan,
        		struct lruvec *lruvec, struct list_head *dst,
        		unsigned long *nr_scanned, struct scan_control *sc,
        		enum lru_list lru)
{
    /**
     *
     */
	struct list_head *src = &lruvec->lists[lru];
	unsigned long nr_taken = 0;
	unsigned long nr_zone_taken[MAX_NR_ZONES] = { 0 };
	unsigned long nr_skipped[MAX_NR_ZONES] = { 0, };
	unsigned long skipped = 0;
	unsigned long scan, total_scan, nr_pages;
	LIST_HEAD(pages_skipped);
    struct list_head pages_skipped;//+++

	isolate_mode_t mode = (sc->may_unmap ? 0 : ISOLATE_UNMAPPED);

	total_scan = 0;
	scan = 0;

    /**
     *
     */
	while (scan < nr_to_scan && !list_empty(src)) {
		struct page *page;

		page = lru_to_page(src);
		prefetchw_prev_lru_page(page, src, flags);

		VM_BUG_ON_PAGE(!PageLRU(page), page);

		nr_pages = compound_nr(page);
		total_scan += nr_pages;

		if (page_zonenum(page) > sc->reclaim_idx) {
			list_move(&page->lru, &pages_skipped);
			nr_skipped[page_zonenum(page)] += nr_pages;
			continue;
		}

		/*
		 * Do not count skipped pages because that makes the function
		 * return with no isolated pages if the LRU mostly contains
		 * ineligible pages.  This causes the VM to not reclaim any
		 * pages, triggering a premature OOM.
		 *
		 * Account all tail pages of THP.  This would not cause
		 * premature OOM since __isolate_lru_page() returns -EBUSY
		 * only when the page is being freed somewhere else.
		 */
		scan += nr_pages;

        /**
         *  分离页面
         */
		switch (__isolate_lru_page(page, mode)) {
        /**
         *  0 标识 分离成功
         */
		case 0:
			nr_taken += nr_pages;
			nr_zone_taken[page_zonenum(page)] += nr_pages;

            /**
             *  移动到 dst 链表中
             */
			list_move(&page->lru, dst);
			break;

		case -EBUSY:
			/* else it is being freed elsewhere */
			list_move(&page->lru, src);
			continue;

		default:
			BUG();
		}
	}

	/*
	 * Splice any skipped pages to the start of the LRU list. Note that
	 * this disrupts the LRU order when reclaiming for lower zones but
	 * we cannot splice to the tail. If we did then the SWAP_CLUSTER_MAX
	 * scanning would soon rescan the same pages to skip and put the
	 * system at risk of premature OOM.
	 */
	if (!list_empty(&pages_skipped)) {
		int zid;

		list_splice(&pages_skipped, src);
		for (zid = 0; zid < MAX_NR_ZONES; zid++) {
			if (!nr_skipped[zid])
				continue;

			__count_zid_vm_events(PGSCAN_SKIP, zid, nr_skipped[zid]);
			skipped += nr_skipped[zid];
		}
	}
	*nr_scanned = total_scan;
	trace_mm_vmscan_lru_isolate(sc->reclaim_idx, sc->order, nr_to_scan,
				    total_scan, skipped, nr_taken, mode, lru);
	update_lru_sizes(lruvec, lru, nr_zone_taken);

    /**
     *  返回 转移的 page 数
     */
	return nr_taken;
}

/**
 * isolate_lru_page - tries to isolate a page from its LRU list
 * @page: page to isolate from its LRU list
 *
 * Isolates a @page from an LRU list, clears PageLRU and adjusts the
 * vmstat statistic corresponding to whatever LRU list the page was on.
 *
 * Returns 0 if the page was removed from an LRU list.
 * Returns -EBUSY if the page was not on an LRU list.
 *
 * The returned page will have PageLRU() cleared.  If it was found on
 * the active list, it will have PageActive set.  If it was found on
 * the unevictable list, it will have the PageUnevictable bit set. That flag
 * may need to be cleared by the caller before letting the page go.
 *
 * The vmstat statistic corresponding to the list on which the page was
 * found will be decremented.
 *
 * Restrictions:
 *
 * (1) Must be called with an elevated refcount on the page. This is a
 *     fundamental difference from isolate_lru_pages (which is called
 *     without a stable reference).
 * (2) the lru_lock must not be held.
 * (3) interrupts must be enabled.
 */
int isolate_lru_page(struct page *page)
{
	int ret = -EBUSY;

	VM_BUG_ON_PAGE(!page_count(page), page);
	WARN_RATELIMIT(PageTail(page), "trying to isolate tail page");

	if (PageLRU(page)) {
		pg_data_t *pgdat = page_pgdat(page);
		struct lruvec *lruvec;

		spin_lock_irq(&pgdat->lru_lock);
		lruvec = mem_cgroup_page_lruvec(page, pgdat);
		if (PageLRU(page)) {
			int lru = page_lru(page);
			get_page(page);
			ClearPageLRU(page);
			del_page_from_lru_list(page, lruvec, lru);
			ret = 0;
		}
		spin_unlock_irq(&pgdat->lru_lock);
	}
	return ret;
}

/*
 * A direct reclaimer may isolate SWAP_CLUSTER_MAX pages from the LRU list and
 * then get rescheduled. When there are massive number of tasks doing page
 * allocation, such sleeping direct reclaimers may keep piling up on each CPU,
 * the LRU list will go small and be scanned faster than necessary, leading to
 * unnecessary swapping, thrashing and OOM.
 *
 * 当前页面回收者是否为 kswapd 线程还是页面直接回收者
 * 已经分离的页面数量是否大于不活跃页面数量
 *
 * 在该函数被调用的地方睡眠100ms 的原因
 * ===========================================================================
 *  若当前页面税收这是直接页面回收并且 有大量的已分离页面，
 *  说明可能有很多进程正在做页面回收，而且有不少的进程已经触发了直接页面回收
 *  这回导致不必要的内存抖动并触发 OOM killer 机制，
 *  隐私可以让直接页面回收者先睡眠 100ms
 */
static int too_many_isolated(struct pglist_data *pgdat, int file,
		struct scan_control *sc)
{
	unsigned long inactive, isolated;

    /**
     *  是否为 kswapd
     */
	if (current_is_kswapd())
		return 0;

	if (!writeback_throttling_sane(sc))
		return 0;

    /**
     *
     */
	if (file) {
		inactive = node_page_state(pgdat, NR_INACTIVE_FILE);
		isolated = node_page_state(pgdat, NR_ISOLATED_FILE);
	} else {
		inactive = node_page_state(pgdat, NR_INACTIVE_ANON);
		isolated = node_page_state(pgdat, NR_ISOLATED_ANON);
	}

	/*
	 * GFP_NOIO/GFP_NOFS callers are allowed to isolate more pages, so they
	 * won't get blocked by normal direct-reclaimers, forming a circular
	 * deadlock.
	 */
	if ((sc->gfp_mask & (__GFP_IO | __GFP_FS)) == (__GFP_IO | __GFP_FS))
		inactive >>= 3;

    /**
     *  已经分离的页面数量是否大于不活跃页面数量
     */
	return isolated > inactive;
}

/*
 * This moves pages from @list to corresponding LRU list.
 *
 * We move them the other way if the page is referenced by one or more
 * processes, from rmap.
 *
 * If the pages are mostly unmapped, the processing is fast and it is
 * appropriate to hold zone_lru_lock across the whole operation.  But if
 * the pages are mapped, the processing is slow (page_referenced()) so we
 * should drop zone_lru_lock around each page.  It's impossible to balance
 * this, so instead we remove the pages from the LRU while processing them.
 * It is safe to rely on PG_active against the non-LRU pages in here because
 * nobody will play with that bit on a non-LRU page.
 *
 * The downside is that we have to touch page->_refcount against each page.
 * But we had to alter page->flags anyway.
 *
 * Returns the number of pages moved to the given lruvec.
 *
 * 从 list 中迁移到对应的 lruvec 中
 */
static unsigned noinline_for_stack move_pages_to_lru(struct lruvec *lruvec, struct list_head *list)
{
	struct pglist_data *pgdat = lruvec_pgdat(lruvec);
	int nr_pages, nr_moved = 0;
	LIST_HEAD(pages_to_free);
	struct page *page;
	enum lru_list lru;

    /**
     *  遍历链表
     */
	while (!list_empty(list)) {
		page = lru_to_page(list);
		VM_BUG_ON_PAGE(PageLRU(page), page);

        /**
         *  可回收
         */
		if (unlikely(!page_evictable(page))) {
			list_del(&page->lru);
			spin_unlock_irq(&pgdat->lru_lock);
			putback_lru_page(page);
			spin_lock_irq(&pgdat->lru_lock);
			continue;
		}

		lruvec = mem_cgroup_page_lruvec(page, pgdat);

        /**
         *  设置标志位，求 lru 枚举值
         */
		SetPageLRU(page);
		lru = page_lru(page);

		nr_pages = thp_nr_pages(page);

        /**
         *
         */
		update_lru_size(lruvec, lru, page_zonenum(page), nr_pages);

        /**
         *  移动到 lruvec 中
         */
		list_move(&page->lru, &lruvec->lists[lru]);

        /**
         *  如果引用计数 为0，返回true
         */
		if (put_page_testzero(page)) {
            /**
             *  清除标志位
             */
			__ClearPageLRU(page);
			__ClearPageActive(page);

            /**
             *  从 lru 删除这个页面
             */
			del_page_from_lru_list(page, lruvec, lru);

			if (unlikely(PageCompound(page))) {
				spin_unlock_irq(&pgdat->lru_lock);
				destroy_compound_page(page);
				spin_lock_irq(&pgdat->lru_lock);
			} else
                /**
                 *  可直接添加到释放的链表中，释放
                 */
				list_add(&page->lru, &pages_to_free);
		}
        /**
         *  引用计数 不为 0，也就是有进程在用这个 page
         */
        else {
			nr_moved += nr_pages;
			if (PageActive(page))
				workingset_age_nonresident(lruvec, nr_pages);
		}
	}

	/*
	 * To save our caller's stack, now use input list for pages to free.
	 */
	list_splice(&pages_to_free, list);

	return nr_moved;
}

/*
 * If a kernel thread (such as nfsd for loop-back mounts) services
 * a backing device by writing to the page cache it sets PF_LOCAL_THROTTLE.
 * In that case we should only throttle if the backing device it is
 * writing to is congested.  In other cases it is safe to throttle.
 *
 * 块设备IO需要节流
 */
static int current_may_throttle(void)
{
	return !(current->flags & PF_LOCAL_THROTTLE) ||
		current->backing_dev_info == NULL ||
		bdi_write_congested(current->backing_dev_info);
}

/*
 * shrink_inactive_list() is a helper for shrink_node().  It returns the number
 * of reclaimed pages
 *
 * @nr_to_scan  待扫描页面数量
 * @lruvec      LRU链表集合
 * @sc          页面扫描控制参数
 * @lru         待扫描的LRU链表类型
 *
 * 扫描不活跃链表 并且 尝试 回收页面
 */
static noinline_for_stack unsigned long
shrink_inactive_list(unsigned long nr_to_scan, struct lruvec *lruvec,
		                 struct scan_control *sc, enum lru_list lru)
{
	/**
	 *  定义临时链表
	 */
	LIST_HEAD(__page_list);

	unsigned long nr_scanned;
	unsigned int nr_reclaimed = 0;
	unsigned long nr_taken;
	struct reclaim_stat stat;

	/**
	 *
	 */
	bool file = is_file_lru(lru);
	enum vm_event_item item;
	struct pglist_data *pgdat = lruvec_pgdat(lruvec);
	bool stalled = false;

	/**
	 *
	 * 1. 当前页面回收者是否为 kswapd 线程还是页面直接回收者
	 * 2. 已经分离的页面数量是否大于不活跃页面数量
	 */
	while (unlikely(too_many_isolated(pgdat, file, sc))) {
		if (stalled)
			return 0;

		/**
		 *  这里睡眠是我没想到的
		 *
		 *  解释一波
		 *  ====================================
		 *  若当前页面 是直接页面回收 并且 有大量的已分离页面，
		 *  说明可能有很多进程正在做页面回收，而且有不少的进程已经触发了直接页面回收
		 *  这会导致不必要的内存抖动 并触发 OOM killer 机制，
		 *  因此，可以让直接页面回收者先睡眠 100ms
		 */
		/* wait a bit for the reclaimer. */
		msleep(100);
		stalled = true;

		/* We are about to die and free our memory. Return now. */
		if (fatal_signal_pending(current))
			return SWAP_CLUSTER_MAX;
	}

	lru_add_drain();

	spin_lock_irq(&pgdat->lru_lock);

	/**
	 *  分离页面到临时链表         __page_list 中
	 */
	nr_taken = isolate_lru_pages(nr_to_scan, lruvec, &__page_list, &nr_scanned, sc, lru);

	/**
	 *  vmstat ， 增加计数
	 */
	__mod_node_page_state(pgdat, NR_ISOLATED_ANON + file, nr_taken);

	/**
	 *  直接回收还是使用 kswapd 回收
	 */
	item = current_is_kswapd() ? PGSCAN_KSWAPD : PGSCAN_DIRECT;
	if (!cgroup_reclaim(sc))
		__count_vm_events(item, nr_scanned);

	/**
	 *  vmstat
	 */
	__count_memcg_events(lruvec_memcg(lruvec), item, nr_scanned);
	__count_vm_events(PGSCAN_ANON + file, nr_scanned);

	spin_unlock_irq(&pgdat->lru_lock);

	if (nr_taken == 0)
		return 0;

	/**
	 *  扫描页面并回收页面
	 */
	nr_reclaimed = shrink_page_list(&__page_list, pgdat, sc, &stat, false);

	spin_lock_irq(&pgdat->lru_lock);

	/**
	 *  将   页面 添加到 lruvec 中
	 */
	move_pages_to_lru(lruvec, &__page_list);

	/**
	 *  vmstat
	 */
	__mod_node_page_state(pgdat, NR_ISOLATED_ANON + file, -nr_taken);
	lru_note_cost(lruvec, file, stat.nr_pageout);
	item = current_is_kswapd() ? PGSTEAL_KSWAPD : PGSTEAL_DIRECT;
	if (!cgroup_reclaim(sc))
		__count_vm_events(item, nr_reclaimed);
	__count_memcg_events(lruvec_memcg(lruvec), item, nr_reclaimed);
	__count_vm_events(PGSTEAL_ANON + file, nr_reclaimed);

	spin_unlock_irq(&pgdat->lru_lock);

	/**
	 *
	 */
	mem_cgroup_uncharge_list(&__page_list);

	/**
	 *  尝试释放 引用计数 为 0 的页面
	 */
	free_unref_page_list(&__page_list);

	/*
	 * If dirty pages are scanned that are not queued for IO, it
	 * implies that flushers are not doing their job. This can
	 * happen when memory pressure pushes dirty pages to the end of
	 * the LRU before the dirty limits are breached and the dirty
	 * data has expired. It can also happen when the proportion of
	 * dirty pages grows not through writes but through memory
	 * pressure reclaiming all the clean cache. And in some cases,
	 * the flushers simply cannot keep up with the allocation
	 * rate. Nudge the flusher threads in case they are asleep.
	 */
	if (stat.nr_unqueued_dirty == nr_taken)
		wakeup_flusher_threads(WB_REASON_VMSCAN);

	/**
	 *  用于统计数据
	 */
	sc->nr.dirty += stat.nr_dirty;
	sc->nr.congested += stat.nr_congested;
	sc->nr.unqueued_dirty += stat.nr_unqueued_dirty;
	sc->nr.writeback += stat.nr_writeback;
	sc->nr.immediate += stat.nr_immediate;
	sc->nr.taken += nr_taken;
	if (file)
		sc->nr.file_taken += nr_taken;

	trace_mm_vmscan_lru_shrink_inactive(pgdat->node_id,
			nr_scanned, nr_reclaimed, &stat, sc->priority, file);
	return nr_reclaimed;
}

/**
 * @brief 扫描活跃链表 包括匿名页面 或 文件映射页面
 * 	收割 和迁移 一部分(最近一直没被访问)活跃页面到 不活跃链表中
 *
 * @param nr_to_scan
 * @param lruvec
 * @param sc
 * @param lru
 */
static void shrink_active_list(unsigned long nr_to_scan,
              			       struct lruvec *lruvec,
              			       struct scan_control *sc,
              			       enum lru_list lru)
{
	unsigned long nr_taken;
	unsigned long nr_scanned;
	unsigned long vm_flags;

	LIST_HEAD(l_hold);	/* The pages which were snipped off */
	LIST_HEAD(l_active);
	LIST_HEAD(l_inactive);
	struct list_head l_hold, l_active, l_inactive; /*+++*/

	struct page *page;
	unsigned nr_deactivate, nr_activate;
	unsigned nr_rotated = 0;

	/**
	 *  是文件 LRU 链表
	 */
	int file = is_file_lru(lru);

	/**
	 *  获取NODE
	 */
	struct pglist_data *pgdat = lruvec_pgdat(lruvec);

	lru_add_drain();

	/**
	 *
	 */
	spin_lock_irq(&pgdat->lru_lock);

	/**
	 *  隔离 page，把一部分 页面先放到 临时链表里，减小对 lru 链表的加锁时间
	 */
	nr_taken = isolate_lru_pages(nr_to_scan, lruvec, &l_hold, &nr_scanned, sc, lru);

	/**
	 *  vmstat 增加计数
	 */
	__mod_node_page_state(pgdat, NR_ISOLATED_ANON + file, nr_taken);

	if (!cgroup_reclaim(sc))
		__count_vm_events(PGREFILL, nr_scanned);

	/**
	 *
	 */
	__count_memcg_events(lruvec_memcg(lruvec), PGREFILL, nr_scanned);

	spin_unlock_irq(&pgdat->lru_lock);

	/**
	 *  遍历这些 page
	 */
	while (!list_empty(&l_hold)) {

		/**
		 * @brief 让出 CPU
		 *
		 */
		cond_resched();

		/**
		 *  从 链表中 获取 page
		 */
		page = lru_to_page(&l_hold);

		/**
		 *  从 LRU 链表中删除，下面将根据情况添加到指定链表中
		 */
		list_del(&page->lru);

		/**
		 * 如果页面是不可回收的，continue
		 */
		if (unlikely(!page_evictable(page))) {
			putback_lru_page(page);
			continue;
		}

		/**
		 *
		 */
		if (unlikely(buffer_heads_over_limit)) {
			if (page_has_private(page) && trylock_page(page)) {
				if (page_has_private(page))
					try_to_release_page(page, 0);
				unlock_page(page);
			}
		}

		/**
		 *  映射了这个 page 的 PTE 数，使用 RMAP 机制。返回页面被引用的次数
		 */
		if (page_referenced(page, 0, sc->target_mem_cgroup, &vm_flags)) {
			/*
			 * Identify referenced, file-backed active pages and
			 * give them one more trip around the active list. So
			 * that executable code get better chances to stay in
			 * memory under moderate memory pressure.  Anon pages
			 * are not likely to be evicted by use-once streaming
			 * IO, plus JVM can create lots of anon VM_EXEC pages,
			 * so we ignore them here.
			 *
			 * 可执行 并且 是文件映射
			 */
			if ((vm_flags & VM_EXEC) && page_is_file_lru(page)) {
				/**
				 *  添加到激活 链表中，继续下一个页
				 */
				nr_rotated += thp_nr_pages(page);
				list_add(&page->lru, &l_active);
				continue;
			}
		}

		/**
		 *  如果页面没有被引用，page 去激活，添加到 inactive 链表
		 */
		ClearPageActive(page);	/* we are de-activating */
		SetPageWorkingset(page);
		list_add(&page->lru, &l_inactive);
	}

	/*
	 * Move pages back to the lru list.
	 */
	spin_lock_irq(&pgdat->lru_lock);

	/**
	 *  l_active/l_inactive 中可能保存了 引用计数为 0 的页面
	 */
	nr_activate = move_pages_to_lru(lruvec, &l_active);
	nr_deactivate = move_pages_to_lru(lruvec, &l_inactive);

	/**
	 *  都转到 active 里
	 */
	/* Keep all free pages in l_active list */
	list_splice(&l_inactive, &l_active);

	/**
	 *  vmstat
	 */
	__count_vm_events(PGDEACTIVATE, nr_deactivate);
	__count_memcg_events(lruvec_memcg(lruvec), PGDEACTIVATE, nr_deactivate);

	__mod_node_page_state(pgdat, NR_ISOLATED_ANON + file, -nr_taken);
	spin_unlock_irq(&pgdat->lru_lock);

	/**
	 *
	 */
	mem_cgroup_uncharge_list(&l_active);

	/**
	 *
	 */
	free_unref_page_list(&l_active);

	/**
	 * @brief 跟踪 tracepoint:vmscan:mm_vmscan_lru_shrink_active
	 *
	 */
	trace_mm_vmscan_lru_shrink_active(pgdat->node_id, nr_taken, nr_activate,
			nr_deactivate, nr_rotated, sc->priority, file);
}

/**
 *	回收页面
 */
unsigned long reclaim_pages(struct list_head *page_list)
{
	int nid = NUMA_NO_NODE;
	unsigned int nr_reclaimed = 0;
	LIST_HEAD(node_page_list);
	struct reclaim_stat dummy_stat;
	struct page *page;
	struct scan_control sc = {
		.gfp_mask = GFP_KERNEL,
		.priority = DEF_PRIORITY,
		.may_writepage = 1,
		.may_unmap = 1,
		.may_swap = 1,
	};

	while (!list_empty(page_list)) {
		page = lru_to_page(page_list);
		if (nid == NUMA_NO_NODE) {
			nid = page_to_nid(page);
			INIT_LIST_HEAD(&node_page_list);
		}

		if (nid == page_to_nid(page)) {
			ClearPageActive(page);
			list_move(&page->lru, &node_page_list);
			continue;
		}

		nr_reclaimed += shrink_page_list(&node_page_list,
						NODE_DATA(nid),
						&sc, &dummy_stat, false);
		while (!list_empty(&node_page_list)) {
			page = lru_to_page(&node_page_list);
			list_del(&page->lru);
			putback_lru_page(page);
		}

		nid = NUMA_NO_NODE;
	}

	if (!list_empty(&node_page_list)) {
		nr_reclaimed += shrink_page_list(&node_page_list,
						NODE_DATA(nid),
						&sc, &dummy_stat, false);
		while (!list_empty(&node_page_list)) {
			page = lru_to_page(&node_page_list);
			list_del(&page->lru);
			putback_lru_page(page);
		}
	}

	return nr_reclaimed;
}

/**
 *  根据 lru 链表类型来调用不同的处理函数
 */
static unsigned long shrink_list(enum lru_list lru, unsigned long nr_to_scan,
				 struct lruvec *lruvec, struct scan_control *sc)
{
	/**
	 *  活跃的 匿名 或 文件映射
	 */
	if (is_active_lru(lru)) {
		/**
		 *  可以 去激活 的文件映射
		 */
		if (sc->may_deactivate & (1 << is_file_lru(lru)))
			/**
			 *  收割 和迁移 一部分活跃页面到 不活跃链表中
			 */
			shrink_active_list(nr_to_scan, lruvec, sc, lru);
		else
			sc->skipped_deactivate = 1;
		return 0;
	}

	/**
	 *  调用该函数，扫描不活跃 LRU 链表并且回收页面
	 */
	return shrink_inactive_list(nr_to_scan, lruvec, sc, lru);
}

/*
 * The inactive anon list should be small enough that the VM never has
 * to do too much work.
 *
 * The inactive file list should be small enough to leave most memory
 * to the established workingset on the scan-resistant active list,
 * but large enough to avoid thrashing the aggregate readahead window.
 *
 * Both inactive lists should also be large enough that each inactive
 * page has a chance to be referenced again before it is reclaimed.
 *
 * If that fails and refaulting is observed, the inactive list grows.
 *
 * The inactive_ratio is the target ratio of ACTIVE to INACTIVE pages
 * on this LRU, maintained by the pageout code. An inactive_ratio
 * of 3 means 3:1 or 25% of the pages are kept on the inactive list.
 *
 * total     target    max
 * memory    ratio     inactive
 * -------------------------------------
 *   10MB       1         5MB
 *  100MB       1        50MB
 *    1GB       3       250MB
 *   10GB      10       0.9GB
 *  100GB      31         3GB
 *    1TB     101        10GB
 *   10TB     320        32GB
 */
static bool inactive_is_low(struct lruvec *lruvec, enum lru_list inactive_lru)
{
	enum lru_list active_lru = inactive_lru + LRU_ACTIVE;
	unsigned long inactive, active;
	unsigned long inactive_ratio;
	unsigned long gb;

	inactive = lruvec_page_state(lruvec, NR_LRU_BASE + inactive_lru);
	active = lruvec_page_state(lruvec, NR_LRU_BASE + active_lru);

	gb = (inactive + active) >> (30 - PAGE_SHIFT);
	if (gb)
		inactive_ratio = int_sqrt(10 * gb);
	else
		inactive_ratio = 1;

	return inactive * inactive_ratio < active;
}

enum scan_balance {
	SCAN_EQUAL,
	SCAN_FRACT,
	SCAN_ANON,
	SCAN_FILE,
};

/*
 * Determine how aggressively the anon and file LRU lists should be
 * scanned.  The relative value of each set of LRU lists is determined
 * by looking at the fraction of the pages scanned we did rotate back
 * onto the active list instead of evict.
 *
 * nr[0] = anon inactive pages to scan; nr[1] = anon active pages to scan
 * nr[2] = file inactive pages to scan; nr[3] = file active pages to scan
 *
 * 计算四个 lru 链表 中应该扫描的页面数量，存放在 nr 数组中
 */
static void get_scan_count(struct lruvec *lruvec, struct scan_control *sc,
			   unsigned long *nr)
{
	struct mem_cgroup *memcg = lruvec_memcg(lruvec);
	unsigned long anon_cost, file_cost, total_cost;
	int swappiness = mem_cgroup_swappiness(memcg);
	u64 fraction[ANON_AND_FILE];
	u64 denominator = 0;	/* gcc */
	enum scan_balance scan_balance;
	unsigned long ap, fp;
	enum lru_list lru;

	/* If we have no swap space, do not bother scanning anon pages. */
	if (!sc->may_swap || mem_cgroup_get_nr_swap_pages(memcg) <= 0) {
		scan_balance = SCAN_FILE;
		goto out;
	}

	/*
	 * Global reclaim will swap to prevent OOM even with no
	 * swappiness, but memcg users want to use this knob to
	 * disable swapping for individual groups completely when
	 * using the memory controller's swap limit feature would be
	 * too expensive.
	 */
	if (cgroup_reclaim(sc) && !swappiness) {
		scan_balance = SCAN_FILE;
		goto out;
	}

	/*
	 * Do not apply any pressure balancing cleverness when the
	 * system is close to OOM, scan both anon and file equally
	 * (unless the swappiness setting disagrees with swapping).
	 */
	if (!sc->priority && swappiness) {
		scan_balance = SCAN_EQUAL;
		goto out;
	}

	/*
	 * If the system is almost out of file pages, force-scan anon.
	 */
	if (sc->file_is_tiny) {
		scan_balance = SCAN_ANON;
		goto out;
	}

	/*
	 * If there is enough inactive page cache, we do not reclaim
	 * anything from the anonymous working right now.
	 */
	if (sc->cache_trim_mode) {
		scan_balance = SCAN_FILE;
		goto out;
	}

	scan_balance = SCAN_FRACT;
	/*
	 * Calculate the pressure balance between anon and file pages.
	 *
	 * The amount of pressure we put on each LRU is inversely
	 * proportional to the cost of reclaiming each list, as
	 * determined by the share of pages that are refaulting, times
	 * the relative IO cost of bringing back a swapped out
	 * anonymous page vs reloading a filesystem page (swappiness).
	 *
	 * Although we limit that influence to ensure no list gets
	 * left behind completely: at least a third of the pressure is
	 * applied, before swappiness.
	 *
	 * With swappiness at 100, anon and file have equal IO cost.
	 */
	total_cost = sc->anon_cost + sc->file_cost;
	anon_cost = total_cost + sc->anon_cost;
	file_cost = total_cost + sc->file_cost;
	total_cost = anon_cost + file_cost;

	ap = swappiness * (total_cost + 1);
	ap /= anon_cost + 1;

	fp = (200 - swappiness) * (total_cost + 1);
	fp /= file_cost + 1;

	fraction[0] = ap;
	fraction[1] = fp;
	denominator = ap + fp;

out:
	/**
	 *
	 */
	for_each_evictable_lru(lru) {
		int file = is_file_lru(lru);
		unsigned long lruvec_size;
		unsigned long scan;
		unsigned long protection;

		lruvec_size = lruvec_lru_size(lruvec, lru, sc->reclaim_idx);
		protection = mem_cgroup_protection(sc->target_mem_cgroup,
						   memcg,
						   sc->memcg_low_reclaim);

		if (protection) {
			/*
			 * Scale a cgroup's reclaim pressure by proportioning
			 * its current usage to its memory.low or memory.min
			 * setting.
			 *
			 * This is important, as otherwise scanning aggression
			 * becomes extremely binary -- from nothing as we
			 * approach the memory protection threshold, to totally
			 * nominal as we exceed it.  This results in requiring
			 * setting extremely liberal protection thresholds. It
			 * also means we simply get no protection at all if we
			 * set it too low, which is not ideal.
			 *
			 * If there is any protection in place, we reduce scan
			 * pressure by how much of the total memory used is
			 * within protection thresholds.
			 *
			 * There is one special case: in the first reclaim pass,
			 * we skip over all groups that are within their low
			 * protection. If that fails to reclaim enough pages to
			 * satisfy the reclaim goal, we come back and override
			 * the best-effort low protection. However, we still
			 * ideally want to honor how well-behaved groups are in
			 * that case instead of simply punishing them all
			 * equally. As such, we reclaim them based on how much
			 * memory they are using, reducing the scan pressure
			 * again by how much of the total memory used is under
			 * hard protection.
			 */
			unsigned long cgroup_size = mem_cgroup_size(memcg);

			/* Avoid TOCTOU with earlier protection check */
			cgroup_size = max(cgroup_size, protection);

			scan = lruvec_size - lruvec_size * protection /
				cgroup_size;

			/*
			 * Minimally target SWAP_CLUSTER_MAX pages to keep
			 * reclaim moving forwards, avoiding decrementing
			 * sc->priority further than desirable.
			 */
			scan = max(scan, SWAP_CLUSTER_MAX);
		} else {
			scan = lruvec_size;
		}

		scan >>= sc->priority;

		/*
		 * If the cgroup's already been deleted, make sure to
		 * scrape out the remaining cache.
		 */
		if (!scan && !mem_cgroup_online(memcg))
			scan = min(lruvec_size, SWAP_CLUSTER_MAX);

		switch (scan_balance) {
		case SCAN_EQUAL:
			/* Scan lists relative to size */
			break;
		case SCAN_FRACT:
			/*
			 * Scan types proportional to swappiness and
			 * their relative recent reclaim efficiency.
			 * Make sure we don't miss the last page on
			 * the offlined memory cgroups because of a
			 * round-off error.
			 */
			scan = mem_cgroup_online(memcg) ?
			       div64_u64(scan * fraction[file], denominator) :
			       DIV64_U64_ROUND_UP(scan * fraction[file],
						  denominator);
			break;
		case SCAN_FILE:
		case SCAN_ANON:
			/* Scan one type exclusively */
			if ((scan_balance == SCAN_FILE) != file)
				scan = 0;
			break;
		default:
			/* Look ma, no brain */
			BUG();
		}

		nr[lru] = scan;
	}
}

/**
 *
 */
static void shrink_lruvec(struct lruvec *lruvec, struct scan_control *sc)
{
	/**
	 *
	 */
	unsigned long nr[NR_LRU_LISTS];
	unsigned long targets[NR_LRU_LISTS];
	unsigned long nr_to_scan;
	enum lru_list lru;
	unsigned long nr_reclaimed = 0;
	unsigned long nr_to_reclaim = sc->nr_to_reclaim;
	struct blk_plug plug;
	bool scan_adjusted;

	/**
	 *  获取数据
	 * 计算四个 lru 链表 中应该扫描的页面数量，存放在 nr 数组中
	 */
	get_scan_count(lruvec, sc, nr);

	/* Record the original scan target for proportional adjustments later */
	memcpy(targets, nr, sizeof(nr));

	/*
	 * Global reclaiming within direct reclaim at DEF_PRIORITY is a normal
	 * event that can occur when there is little memory pressure e.g.
	 * multiple streaming readers/writers. Hence, we do not abort scanning
	 * when the requested number of pages are reclaimed when scanning at
	 * DEF_PRIORITY on the assumption that the fact we are direct
	 * reclaiming implies that kswapd is not keeping up and it is best to
	 * do a batch of work at once. For memcg reclaim one check is made to
	 * abort proportional reclaim if either the file or anon lru has already
	 * dropped to zero at the first pass.
	 */
	scan_adjusted = (!cgroup_reclaim(sc) &&
	                 !current_is_kswapd() &&
			         sc->priority == DEF_PRIORITY);

	/**
	 *
	 */
	blk_start_plug(&plug);

	/**
	 *
	 */
	while (nr[LRU_INACTIVE_ANON] || nr[LRU_ACTIVE_FILE] || nr[LRU_INACTIVE_FILE]) {
		unsigned long nr_anon, nr_file, percentage;
		unsigned long nr_scanned;

		/**
		 *  遍历 可回收 LRU
		 */
		for_each_evictable_lru(lru) {
			if (nr[lru]) {
				nr_to_scan = min(nr[lru], SWAP_CLUSTER_MAX);
				nr[lru] -= nr_to_scan;

				/**
				 *  回收
				 */
				nr_reclaimed += shrink_list(lru, nr_to_scan, lruvec, sc);
			}
		}

		/**
		 *
		 */
		cond_resched();

		if (nr_reclaimed < nr_to_reclaim || scan_adjusted)
			continue;

		/*
		 * For kswapd and memcg, reclaim at least the number of pages
		 * requested. Ensure that the anon and file LRUs are scanned
		 * proportionally what was requested by get_scan_count(). We
		 * stop reclaiming one LRU and reduce the amount scanning
		 * proportional to the original scan target.
		 */
		nr_file = nr[LRU_INACTIVE_FILE] + nr[LRU_ACTIVE_FILE];
		nr_anon = nr[LRU_INACTIVE_ANON] + nr[LRU_ACTIVE_ANON];

		/*
		 * It's just vindictive to attack the larger once the smaller
		 * has gone to zero.  And given the way we stop scanning the
		 * smaller below, this makes sure that we only make one nudge
		 * towards proportionality once we've got nr_to_reclaim.
		 */
		if (!nr_file || !nr_anon)
			break;

		/**
		 *  文件映射 大于 匿名映射
		 */
		if (nr_file > nr_anon) {
			unsigned long scan_target = targets[LRU_INACTIVE_ANON] +
						                targets[LRU_ACTIVE_ANON] + 1;
			lru = LRU_BASE;
            /**
             *  计算百分比
             */
			percentage = nr_anon * 100 / scan_target;

		/**
		 *  文件映射 <= 匿名映射
		 */
		} else {

			unsigned long scan_target = targets[LRU_INACTIVE_FILE] +
						                targets[LRU_ACTIVE_FILE] + 1;
			lru = LRU_FILE;
			percentage = nr_file * 100 / scan_target;
		}

		/* Stop scanning the smaller of the LRU */
		nr[lru] = 0;
		nr[lru + LRU_ACTIVE] = 0;

		/*
		 * Recalculate the other LRU scan count based on its original
		 * scan target and the percentage scanning already complete
		 */
		lru = (lru == LRU_FILE) ? LRU_BASE : LRU_FILE;
		nr_scanned = targets[lru] - nr[lru];
		nr[lru] = targets[lru] * (100 - percentage) / 100;
		nr[lru] -= min(nr[lru], nr_scanned);

		lru += LRU_ACTIVE;
		nr_scanned = targets[lru] - nr[lru];
		nr[lru] = targets[lru] * (100 - percentage) / 100;
		nr[lru] -= min(nr[lru], nr_scanned);

		scan_adjusted = true;
	}

	/**
	 *
	 */
	blk_finish_plug(&plug);
	sc->nr_reclaimed += nr_reclaimed;

	/*
	 * Even if we did not try to evict(回收) anon pages at all, we want to
	 * rebalance the anon lru active/inactive ratio.
	 */
	if (total_swap_pages && inactive_is_low(lruvec, LRU_INACTIVE_ANON))
		shrink_active_list(SWAP_CLUSTER_MAX, lruvec, sc, LRU_ACTIVE_ANON);
}

/* Use reclaim/compaction for costly allocs or under memory pressure */
static bool in_reclaim_compaction(struct scan_control *sc)
{
	if (IS_ENABLED(CONFIG_COMPACTION) && sc->order &&
			(sc->order > PAGE_ALLOC_COSTLY_ORDER ||
			 sc->priority < DEF_PRIORITY - 2))
		return true;

	return false;
}

/*
 * Reclaim/compaction is used for high-order allocation requests. It reclaims
 * order-0 pages before compacting the zone. should_continue_reclaim() returns
 * true if more pages should be reclaimed such that when the page allocator
 * calls try_to_compact_pages() that it will have enough free pages to succeed.
 * It will give up earlier than that if there is difficulty reclaiming pages.
 *
 *  如果已回收 的页面数量 sc->nr_reclaimed 小于 (2<<sc->order)
 *  且不活跃的页面总数 大于 (2<<sc->order) ， 那么需要继续回收
 */
static inline bool should_continue_reclaim(struct pglist_data *pgdat,
					unsigned long nr_reclaimed,
					struct scan_control *sc)
{
	unsigned long pages_for_compaction;
	unsigned long inactive_lru_pages;
	int z;

	/* If not in reclaim/compaction mode, stop */
	if (!in_reclaim_compaction(sc))
		return false;

	/*
	 * Stop if we failed to reclaim any pages from the last SWAP_CLUSTER_MAX
	 * number of pages that were scanned. This will return to the caller
	 * with the risk reclaim/compaction and the resulting allocation attempt
	 * fails. In the past we have tried harder for __GFP_RETRY_MAYFAIL
	 * allocations through requiring that the full LRU list has been scanned
	 * first, by assuming that zero delta of sc->nr_scanned means full LRU
	 * scan, but that approximation was wrong, and there were corner cases
	 * where always a non-zero amount of pages were scanned.
	 */
	if (!nr_reclaimed)
		return false;

	/* If compaction would go ahead or the allocation would succeed, stop */
	for (z = 0; z <= sc->reclaim_idx; z++) {
		struct zone *zone = &pgdat->node_zones[z];
		if (!managed_zone(zone))
			continue;

		/**
		 *  判断是否做 内存规整
		 *
		 *  如果适合，直接返回，稍后的内存规整机制有助于 释放大块内存
		 */
		switch (compaction_suitable(zone, sc->order, 0, sc->reclaim_idx)) {
		case COMPACT_SUCCESS:
		case COMPACT_CONTINUE:
			return false;
		default:
			/* check next zone */
			;
		}
	}

	/*
	 * If we have not reclaimed enough pages for compaction and the
	 * inactive lists are large enough, continue reclaiming
	 */
	pages_for_compaction = compact_gap(sc->order);
	inactive_lru_pages = node_page_state(pgdat, NR_INACTIVE_FILE);
	if (get_nr_swap_pages() > 0)
		inactive_lru_pages += node_page_state(pgdat, NR_INACTIVE_ANON);

	return inactive_lru_pages > pages_for_compaction;
}

/**
 *  基于内存节点的 内存回收
 */
static void shrink_node_memcgs(pg_data_t *pgdat, struct scan_control *sc)
{
	/**
	 *  将要到达极限 的 目标 cgroup
	 */
	struct mem_cgroup *target_memcg = sc->target_mem_cgroup;
	struct mem_cgroup *memcg;

	/**
	 *
	 */
	memcg = mem_cgroup_iter(target_memcg, NULL, NULL);

	do {
		/**
		 *  获取 LRU 向量
		 */
		struct lruvec *lruvec = mem_cgroup_lruvec(memcg, pgdat);
		unsigned long reclaimed;
		unsigned long scanned;

		/*
		 * This loop can become CPU-bound when target memcgs
		 * aren't eligible for reclaim - either because they
		 * don't have any reclaimable pages, or because their
		 * memory is explicitly protected. Avoid soft lockups.
		 */
		cond_resched();

		/**
		 *
		 */
		mem_cgroup_calculate_protection(target_memcg, memcg);

		if (mem_cgroup_below_min(memcg)) {
			/*
			 * Hard protection.
			 * If there is no reclaimable memory, OOM.
			 */
			continue;
		} else if (mem_cgroup_below_low(memcg)) {
			/*
			 * Soft protection.
			 * Respect the protection only as long as
			 * there is an unprotected supply
			 * of reclaimable memory from other cgroups.
			 */
			if (!sc->memcg_low_reclaim) {
				sc->memcg_low_skipped = 1;
				continue;
			}
			memcg_memory_event(memcg, MEMCG_LOW);
		}

		/**
		 *  扫描的 、 回收的
		 */
		reclaimed = sc->nr_reclaimed;
		scanned = sc->nr_scanned;

		/**
		 *  lruvec 回收
		 */
		shrink_lruvec(lruvec, sc);

		/**
		 *  slab收割
		 */
		shrink_slab(sc->gfp_mask, pgdat->node_id, memcg,
			    sc->priority);

		/**
		 *  记录 group 的回收效率
		 */
		/* Record the group's reclaim efficiency */
		vmpressure(sc->gfp_mask, memcg, false,
    			   sc->nr_scanned - scanned,
    			   sc->nr_reclaimed - reclaimed);

	}
    while ((memcg = mem_cgroup_iter(target_memcg, memcg, NULL)));
}

/**
 *  扫描内存节点中 所有 可回收页面
 */
static void shrink_node(pg_data_t *pgdat, struct scan_control *sc)
{
	struct reclaim_state *reclaim_state = current->reclaim_state;
	unsigned long nr_reclaimed, nr_scanned;
	struct lruvec *target_lruvec;
	bool reclaimable = false;
	unsigned long file;

	/**
	 *  获取 lruvec
	 */
	target_lruvec = mem_cgroup_lruvec(sc->target_mem_cgroup, pgdat);

/* 在 linux-5.0 中为 do{}while(should_continue_reclaim(..)); 循环 */
again:

	/**
	 *  将 统计数量 归零
	 */
	memset(&sc->nr, 0, sizeof(sc->nr));

	/**
	 *  已回收 和 已扫描 页数
	 */
	nr_reclaimed = sc->nr_reclaimed;
	nr_scanned = sc->nr_scanned;

	/*
	 * Determine the scan balance between anon and file LRUs.
	 */
	spin_lock_irq(&pgdat->lru_lock);
	sc->anon_cost = target_lruvec->anon_cost;
	sc->file_cost = target_lruvec->file_cost;
	spin_unlock_irq(&pgdat->lru_lock);

	/*
	 * Target desirable inactive:active list ratios for the anon
	 * and file LRU lists.*
	 *
	 * 目标期望 的 不活跃 比 活跃 链表 比例 (匿名和文件映射)
	 */
	if (!sc->force_deactivate) {

		unsigned long refaults;

		/**
		 *
		 */
		refaults = lruvec_page_state(target_lruvec, WORKINGSET_ACTIVATE_ANON);
		if (refaults != target_lruvec->refaults[0] ||
			inactive_is_low(target_lruvec, LRU_INACTIVE_ANON))
			sc->may_deactivate |= DEACTIVATE_ANON;
		else
			sc->may_deactivate &= ~DEACTIVATE_ANON;

		/*
		 * When refaults are being observed, it means a new
		 * workingset is being established. Deactivate to get
		 * rid of any stale active pages quickly.
		 */
		refaults = lruvec_page_state(target_lruvec, WORKINGSET_ACTIVATE_FILE);
		if (refaults != target_lruvec->refaults[1] ||
		    inactive_is_low(target_lruvec, LRU_INACTIVE_FILE))
			sc->may_deactivate |= DEACTIVATE_FILE;
		else
			sc->may_deactivate &= ~DEACTIVATE_FILE;

	}
    /**
     *
     */
    else
		sc->may_deactivate = DEACTIVATE_ANON | DEACTIVATE_FILE;

	/*
	 * If we have plenty of inactive file pages that aren't
	 * thrashing, try to reclaim those first before touching
	 * anonymous pages.
	 */
	file = lruvec_page_state(target_lruvec, NR_INACTIVE_FILE);
	if (file >> sc->priority && !(sc->may_deactivate & DEACTIVATE_FILE))
		sc->cache_trim_mode = 1;
	else
		sc->cache_trim_mode = 0;

	/*
	 * Prevent the reclaimer from falling into the cache trap: as
	 * cache pages start out inactive, every cache fault will tip
	 * the scan balance towards the file LRU.  And as the file LRU
	 * shrinks, so does the window for rotation from references.
	 * This means we have a runaway feedback loop where a tiny
	 * thrashing file LRU becomes infinitely more attractive than
	 * anon pages.  Try to detect this based on file LRU size.
	 *
	 * 将要 到达 cgroup 极限的 memcg 没有被设置
	 */
	if (!cgroup_reclaim(sc)) {

		unsigned long total_high_wmark = 0;
		unsigned long free, anon;
		int z;

		/**
		 *  计算当前节点的所有 zone 的 free 页面统计和
		 */
		free = sum_zone_node_page_state(pgdat->node_id, NR_FREE_PAGES);

		/**
		 *  统计 文件映射的页面数量
		 */
		file = node_page_state(pgdat, NR_ACTIVE_FILE) + node_page_state(pgdat, NR_INACTIVE_FILE);

		/**
		 *  遍历 NODE 的所有 ZONE，计算ZONE  的所有高水位和
		 */
		for (z = 0; z < MAX_NR_ZONES; z++) {

			struct zone *zone = &pgdat->node_zones[z];

			/**
			 *  有管理的页面
			 */
			if (!managed_zone(zone))
				continue;

			/**
			 *  返回 这个 ZONE 的 高水位，并计算 NODE 所有 ZONE 的 高水位和
			 */
			total_high_wmark += high_wmark_pages(zone);
		}

		/*
		 * Consider anon: if that's low too, this isn't a
		 * runaway file reclaim problem, but rather just
		 * extreme pressure. Reclaim as per usual then.
		 *
		 * 未激活的 匿名页面 的统计值
		 */
		anon = node_page_state(pgdat, NR_INACTIVE_ANON);

		/**
		 *
		 *
		 * 1. 文件映射页面 + 空闲页面 <= NODE高水位(也就是低于高水位)
		 * 2.
		 */
		sc->file_is_tiny =
    			file + free <= total_high_wmark &&
    			!(sc->may_deactivate & DEACTIVATE_ANON) &&
    			anon >> sc->priority;
	}

	/**
	 *  基于内存节点的 页面回收
	 */
	shrink_node_memcgs(pgdat, sc);

	/**
	 *
	 */
	if (reclaim_state) {
		sc->nr_reclaimed += reclaim_state->reclaimed_slab;
		reclaim_state->reclaimed_slab = 0;
	}

	/**
	 *  Record the subtree's reclaim efficiency
	 *
	 *  判断内存压力
	 */
	vmpressure(sc->gfp_mask, sc->target_mem_cgroup, true,
    		   sc->nr_scanned - nr_scanned,
    		   sc->nr_reclaimed - nr_reclaimed);

	/**
	 *
	 */
	if (sc->nr_reclaimed - nr_reclaimed)
		reclaimable = true;

	/* 直接回收机制下 也会 使用，所以需要判断 是否为kswapd 线程 */
	if (current_is_kswapd()) {
		/*
		 * If reclaim is isolating dirty pages under writeback,
		 * it implies that the long-lived page allocation rate
		 * is exceeding the page laundering rate. Either the
		 * global limits are not being effective at throttling
		 * processes due to the page distribution throughout
		 * zones or there is heavy usage of a slow backing
		 * device. The only option is to throttle from reclaim
		 * context which is not ideal as there is no guarantee
		 * the dirtying process is throttled in the same way
		 * balance_dirty_pages() manages.
		 *
		 * Once a node is flagged PGDAT_WRITEBACK, kswapd will
		 * count the number of pages under pages flagged for
		 * immediate reclaim and stall if any are encountered
		 * in the nr_immediate check below.
		 */
		/**
		 *  如果有回写的页面，并且 回写页面==扫描数
		 *
		 */
		if (sc->nr.writeback && sc->nr.writeback == sc->nr.taken)
			/**
			 *  设置该节点 有大量该页面正等待 回写 到磁盘
			 */
			set_bit(PGDAT_WRITEBACK, &pgdat->flags);

		/**
		 *  发现有大量的脏文件页面
		 *  还没有回写的脏页数 == 扫描的文件映射数
		 */
		/* Allow kswapd to start writing pages during reclaim.*/
		if (sc->nr.unqueued_dirty == sc->nr.file_taken)
			/**
			 *  标记该节点 有大量的脏页面等待回写
			 */
			set_bit(PGDAT_DIRTY, &pgdat->flags);

		/*
		 * If kswapd scans pages marked for immediate
		 * reclaim and under writeback (nr_immediate), it
		 * implies that pages are cycling through the LRU
		 * faster than they are written so also forcibly stall.
		 *
		 * 立刻做出处理
		 */
		if (sc->nr.immediate)
			congestion_wait(BLK_RW_ASYNC, HZ/10);//等待 100ms
	}

	/*
	 * Tag a node/memcg as congested if all the dirty pages
	 * scanned were backed by a congested BDI and
	 * wait_iff_congested will stall.
	 *
	 * Legacy memcg will stall in page writeback so avoid forcibly
	 * stalling in wait_iff_congested().
	 */
	if ((current_is_kswapd() ||
	     (cgroup_reclaim(sc) && writeback_throttling_sane(sc))) &&
	    sc->nr.dirty && sc->nr.dirty == sc->nr.congested)
		set_bit(LRUVEC_CONGESTED, &target_lruvec->flags);

	/*
	 * Stall direct reclaim for IO completions if underlying BDIs
	 * and node is congested. Allow kswapd to continue until it
	 * starts encountering unqueued dirty pages or cycling through
	 * the LRU too quickly.
	 *
	 * current_may_throttle: 块设备IO需要节流
	 */
	if (!current_is_kswapd() && current_may_throttle() &&
	    !sc->hibernation_mode &&
	    test_bit(LRUVEC_CONGESTED, &target_lruvec->flags))
		wait_iff_congested(BLK_RW_ASYNC, HZ/10); //等待 100ms

	/**
	 *  如果已回收 的页面数量 sc->nr_reclaimed 小于 (2<<sc->order)
	 *  且不活跃的页面总数 大于 (2<<sc->order) ， 那么需要继续回收
	 */
	if (should_continue_reclaim(pgdat, sc->nr_reclaimed - nr_reclaimed, sc))
		goto again;

	/*
	 * Kswapd gives up on balancing particular nodes after too
	 * many failures to reclaim anything from them and goes to
	 * sleep. On reclaim progress, reset the failure counter. A
	 * successful direct reclaim run will revive a dormant kswapd.
	 */
	if (reclaimable)
		pgdat->kswapd_failures = 0;
}

/*
 * Returns true if compaction should go ahead for a costly-order request, or
 * the allocation would already succeed without compaction. Return false if we
 * should reclaim first.
 */
static inline bool compaction_ready(struct zone *zone, struct scan_control *sc)
{
	unsigned long watermark;
	enum compact_result suitable;

	suitable = compaction_suitable(zone, sc->order, 0, sc->reclaim_idx);
	if (suitable == COMPACT_SUCCESS)
		/* Allocation should succeed already. Don't reclaim. */
		return true;
	if (suitable == COMPACT_SKIPPED)
		/* Compaction cannot yet proceed. Do reclaim. */
		return false;

	/*
	 * Compaction is already possible, but it takes time to run and there
	 * are potentially other callers using the pages just freed. So proceed
	 * with reclaim to make a buffer of free pages available to give
	 * compaction a reasonable chance of completing and allocating the page.
	 * Note that we won't actually reclaim the whole buffer in one attempt
	 * as the target watermark in should_continue_reclaim() is lower. But if
	 * we are already above the high+gap watermark, don't reclaim at all.
	 */
	watermark = high_wmark_pages(zone) + compact_gap(sc->order);

	return zone_watermark_ok_safe(zone, 0, watermark, sc->reclaim_idx);
}

/*
 * This is the direct reclaim path, for page-allocating processes.  We only
 * try to reclaim pages from zones which will satisfy the caller's allocation
 * request.
 *
 * If a zone is deemed to be full of pinned pages then just give it a light
 * scan then give up on it.
 *
 * 对页高速缓存和用户态地址空间进行页回收
 */
static void shrink_zones(struct zonelist *zonelist, struct scan_control *sc)
{
	struct zoneref *z;
	struct zone *zone;
	unsigned long nr_soft_reclaimed;
	unsigned long nr_soft_scanned;
	gfp_t orig_mask;
	pg_data_t *last_pgdat = NULL;

	/*
	 * If the number of buffer_heads in the machine exceeds the maximum
	 * allowed level, force direct reclaim to scan the highmem zone as
	 * highmem pages could be pinning lowmem pages storing buffer_heads
	 */
	orig_mask = sc->gfp_mask;
	if (buffer_heads_over_limit) {
		sc->gfp_mask |= __GFP_HIGHMEM;
		sc->reclaim_idx = gfp_zone(sc->gfp_mask);
	}

    /**
     *  遍历 zonelist
     */
	for_each_zone_zonelist_nodemask(zone, z, zonelist,
					sc->reclaim_idx, sc->nodemask) {
		/*
		 * Take care memory controller reclaiming has small influence
		 * to global LRU.
		 */
		if (!cgroup_reclaim(sc)) {
			if (!cpuset_zone_allowed(zone,
						 GFP_KERNEL | __GFP_HARDWALL))
				continue;

			/*
			 * If we already have plenty of memory free for
			 * compaction in this zone, don't free any more.
			 * Even though compaction is invoked for any
			 * non-zero order, only frequent costly order
			 * reclamation is disruptive enough to become a
			 * noticeable problem, like transparent huge
			 * page allocations.
			 */
			if (IS_ENABLED(CONFIG_COMPACTION) &&
			    sc->order > PAGE_ALLOC_COSTLY_ORDER &&
			    compaction_ready(zone, sc)) {
				sc->compaction_ready = true;
				continue;
			}

			/*
			 * Shrink each node in the zonelist once. If the
			 * zonelist is ordered by zone (not the default) then a
			 * node may be shrunk multiple times but in that case
			 * the user prefers lower zones being preserved.
			 */
			if (zone->zone_pgdat == last_pgdat)
				continue;

			/*
			 * This steals pages from memory cgroups over softlimit
			 * and returns the number of reclaimed pages and
			 * scanned pages. This works for global memory pressure
			 * and balancing, not for a memcg's limit.
			 */
			nr_soft_scanned = 0;
			nr_soft_reclaimed = mem_cgroup_soft_limit_reclaim(zone->zone_pgdat,
						sc->order, sc->gfp_mask,
						&nr_soft_scanned);
			sc->nr_reclaimed += nr_soft_reclaimed;
			sc->nr_scanned += nr_soft_scanned;
			/* need some check for avoid more shrink_zone() */
		}

		/* See comment about same check for global reclaim above */
		if (zone->zone_pgdat == last_pgdat)
			continue;
		last_pgdat = zone->zone_pgdat;

        /**
         *
         */
		shrink_node(zone->zone_pgdat, sc);
	}

	/*
	 * Restore to original mask to avoid the impact on the caller if we
	 * promoted it to __GFP_HIGHMEM.
	 */
	sc->gfp_mask = orig_mask;
}

/**
 *
 */
static void snapshot_refaults(struct mem_cgroup *target_memcg, pg_data_t *pgdat)
{
	struct lruvec *target_lruvec;
	unsigned long refaults;

	target_lruvec = mem_cgroup_lruvec(target_memcg, pgdat);
	refaults = lruvec_page_state(target_lruvec, WORKINGSET_ACTIVATE_ANON);
	target_lruvec->refaults[0] = refaults;
	refaults = lruvec_page_state(target_lruvec, WORKINGSET_ACTIVATE_FILE);
	target_lruvec->refaults[1] = refaults;
}

/*
 * This is the main entry point to direct page reclaim. (直接内存回收的核心函数)
 *
 * If a full scan of the inactive list fails to free enough memory then we
 * are "out of memory" and something needs to be killed.
 *
 * If the caller is !__GFP_FS then the probability of a failure is reasonably
 * high - the zone may be full of dirty or under-writeback pages, which this
 * caller can't do much about.  We kick the writeback threads and take explicit
 * naps in the hope that some of these pages can be written.  But if the
 * allocating task holds filesystem locks which prevent writeout this might not
 * work, and the allocation attempt will fail.
 *
 * returns:	0, if no pages reclaimed
 * 		else, the number of pages reclaimed
 *
 * 直接内存回收的核心函数
 */
static unsigned long do_try_to_free_pages(struct zonelist *zonelist,
					  struct scan_control *sc)
{
	int initial_priority = sc->priority;
	pg_data_t *last_pgdat;
	struct zoneref *z;
	struct zone *zone;

retry:
	delayacct_freepages_start();

	if (!cgroup_reclaim(sc))
		__count_zid_vm_events(ALLOCSTALL, sc->reclaim_idx, 1);

	do {
		vmpressure_prio(sc->gfp_mask, sc->target_mem_cgroup,
				sc->priority);
		sc->nr_scanned = 0;

        /**
         *  压缩 zone
         */
		shrink_zones(zonelist, sc);

		if (sc->nr_reclaimed >= sc->nr_to_reclaim)
			break;

		if (sc->compaction_ready)
			break;

		/*
		 * If we're getting trouble reclaiming, start doing
		 * writepage even in laptop mode.
		 */
		if (sc->priority < DEF_PRIORITY - 2)
			sc->may_writepage = 1;
	} while (--sc->priority >= 0);

	last_pgdat = NULL;

    /**
     *  遍历 zone
     */
	for_each_zone_zonelist_nodemask(zone, z, zonelist, sc->reclaim_idx,
					sc->nodemask) {
		if (zone->zone_pgdat == last_pgdat)
			continue;
		last_pgdat = zone->zone_pgdat;

        /**
         *
         */
		snapshot_refaults(sc->target_mem_cgroup, zone->zone_pgdat);

        /**
         *
         */
		if (cgroup_reclaim(sc)) {
			struct lruvec *lruvec;

            /**
             *
             */
			lruvec = mem_cgroup_lruvec(sc->target_mem_cgroup,
						   zone->zone_pgdat);
			clear_bit(LRUVEC_CONGESTED, &lruvec->flags);
		}
	}

	delayacct_freepages_end();

	if (sc->nr_reclaimed)
		return sc->nr_reclaimed;

	/* Aborted reclaim to try compaction? don't OOM, then */
	if (sc->compaction_ready)
		return 1;

	/*
	 * We make inactive:active ratio decisions based on the node's
	 * composition of memory, but a restrictive reclaim_idx or a
	 * memory.low cgroup setting can exempt large amounts of
	 * memory from reclaim. Neither of which are very common, so
	 * instead of doing costly eligibility calculations of the
	 * entire cgroup subtree up front, we assume the estimates are
	 * good, and retry with forcible deactivation if that fails.
	 */
	if (sc->skipped_deactivate) {
		sc->priority = initial_priority;
		sc->force_deactivate = 1;
		sc->skipped_deactivate = 0;
		goto retry;
	}

	/* Untapped cgroup reserves?  Don't OOM, retry. */
	if (sc->memcg_low_skipped) {
		sc->priority = initial_priority;
		sc->force_deactivate = 0;
		sc->memcg_low_reclaim = 1;
		sc->memcg_low_skipped = 0;
		goto retry;
	}

	return 0;
}

/**
 *  node 是否运行内存直接回收
 */
static bool allow_direct_reclaim(pg_data_t *pgdat)
{
	struct zone *zone;
	unsigned long pfmemalloc_reserve = 0;
	unsigned long free_pages = 0;
	int i;
	bool wmark_ok;

	if (pgdat->kswapd_failures >= MAX_RECLAIM_RETRIES)
		return true;

    /**
     *  ZONE_DMA = 0
     *  ZONE_DMA32
     *  ZONE_NORMAL
     *  ZONE_HIGHMEM
     *  ZONE_MOVABLE
     *  ZONE_DEVICE
     */
	for (i = 0; i <= ZONE_NORMAL; i++) {
		zone = &pgdat->node_zones[i];
        /**
         *  没有管理的页
         */
		if (!managed_zone(zone))
			continue;

        /**
         *  没有可回收的页, 跳过这个 zone，可回收的页包括
         * 1. 不活跃 + 活跃 的文件映射
         * 2. 如果开启了 swap： 不活跃 + 活跃 的匿名映射
         */
		if (!zone_reclaimable_pages(zone))
			continue;

        /**
         *  获取预留的 最低境界水位 之和
         */
		pfmemalloc_reserve += min_wmark_pages(zone);
        /**
         *  空闲页
         */
		free_pages += zone_page_state(zone, NR_FREE_PAGES);
	}

	/* If there are no reserves (unexpected config) then do not throttle */
	if (!pfmemalloc_reserve)
		return true;

    /**
     *  空闲页面 > 最低警戒水位之和 的 1/2, 则 水位 OK
     */
	wmark_ok = free_pages > pfmemalloc_reserve / 2;

	/**
	 *  kswapd must be awake if processes are being throttled(节流)
	 *
	 *  1. 水位不满足要求
	 *  2. kswapd 已经是激活状态？
	 */
	if (!wmark_ok && waitqueue_active(&pgdat->kswapd_wait)) {
		if (READ_ONCE(pgdat->kswapd_highest_zoneidx) > ZONE_NORMAL)
			WRITE_ONCE(pgdat->kswapd_highest_zoneidx, ZONE_NORMAL);

        /**
         *  激活 kswapd
         */
		wake_up_interruptible(&pgdat->kswapd_wait);
	}

	return wmark_ok;
}

/*
 * Throttle direct reclaimers if backing storage is backed by the network
 * and the PFMEMALLOC reserve for the preferred node is getting dangerously
 * depleted. kswapd will continue to make progress and wake the processes
 * when the low watermark is reached.
 *
 * Returns true if a fatal signal was delivered during throttling. If this
 * happens, the page allocator should not consider triggering the OOM killer.
 */
static bool throttle_direct_reclaim(gfp_t gfp_mask, struct zonelist *zonelist,
					nodemask_t *nodemask)
{
	struct zoneref *z;
	struct zone *zone;
	pg_data_t *pgdat = NULL;

	/*
	 * Kernel threads should not be throttled as they may be indirectly
	 * responsible for cleaning pages necessary for reclaim to make forward
	 * progress. kjournald for example may enter direct reclaim while
	 * committing a transaction where throttling it could forcing other
	 * processes to block on log_wait_commit().
	 */
	if (current->flags & PF_KTHREAD)
		goto out;

	/*
	 * If a fatal signal is pending, this process should not throttle.
	 * It should return quickly so it can exit and free its memory
	 */
	if (fatal_signal_pending(current))
		goto out;

	/*
	 * Check if the pfmemalloc reserves are ok by finding the first node
	 * with a usable ZONE_NORMAL or lower zone. The expectation is that
	 * GFP_KERNEL will be required for allocating network buffers when
	 * swapping over the network so ZONE_HIGHMEM is unusable.
	 *
	 * Throttling is based on the first usable node and throttled processes
	 * wait on a queue until kswapd makes progress and wakes them. There
	 * is an affinity then between processes waking up and where reclaim
	 * progress has been made assuming the process wakes on the same node.
	 * More importantly, processes running on remote nodes will not compete
	 * for remote pfmemalloc reserves and processes on different nodes
	 * should make reasonable progress.
	 */
	for_each_zone_zonelist_nodemask(zone, z, zonelist,
					gfp_zone(gfp_mask), nodemask) {
		if (zone_idx(zone) > ZONE_NORMAL)
			continue;

		/* Throttle(节流) based on the first usable node */
		pgdat = zone->zone_pgdat;
		if (allow_direct_reclaim(pgdat))
			goto out;
		break;
	}

	/* If no zone was usable by the allocation flags then do not throttle */
	if (!pgdat)
		goto out;

	/* Account for the throttling */
	count_vm_event(PGSCAN_DIRECT_THROTTLE);

	/*
	 * If the caller cannot enter the filesystem, it's possible that it
	 * is due to the caller holding an FS lock or performing a journal
	 * transaction in the case of a filesystem like ext[3|4]. In this case,
	 * it is not safe to block on pfmemalloc_wait as kswapd could be
	 * blocked waiting on the same lock. Instead, throttle for up to a
	 * second before continuing.
	 */
	if (!(gfp_mask & __GFP_FS)) {
		wait_event_interruptible_timeout(pgdat->pfmemalloc_wait,
			allow_direct_reclaim(pgdat), HZ);

		goto check_pending;
	}

	/* Throttle until kswapd wakes the process */
	wait_event_killable(zone->zone_pgdat->pfmemalloc_wait,
		allow_direct_reclaim(pgdat));

check_pending:
	if (fatal_signal_pending(current))
		return true;

out:
	return false;
}

/**
 *  内存紧缺时 进行页回收
 */
unsigned long try_to_free_pages(struct zonelist *zonelist, int order,
				gfp_t gfp_mask, nodemask_t *nodemask)
{
	unsigned long nr_reclaimed;
	struct scan_control sc = {
		.nr_to_reclaim = SWAP_CLUSTER_MAX,
		.gfp_mask = current_gfp_context(gfp_mask),
		.reclaim_idx = gfp_zone(gfp_mask),
		.order = order,
		.nodemask = nodemask,
		.priority = DEF_PRIORITY,
		.may_writepage = !laptop_mode,
		.may_unmap = 1,
		.may_swap = 1,
	};

	/*
	 * scan_control uses s8 fields for order, priority, and reclaim_idx.
	 * Confirm they are large enough for max values.
	 */
	BUILD_BUG_ON(MAX_ORDER > S8_MAX);
	BUILD_BUG_ON(DEF_PRIORITY > S8_MAX);
	BUILD_BUG_ON(MAX_NR_ZONES > S8_MAX);

	/*
	 * Do not enter reclaim if fatal signal was delivered while throttled.
	 * 1 is returned so that the page allocator does not OOM kill at this
	 * point.
	 */
	if (throttle_direct_reclaim(sc.gfp_mask, zonelist, nodemask))
		return 1;

    /**
     *
     */
	set_task_reclaim_state(current, &sc.reclaim_state);
	trace_mm_vmscan_direct_reclaim_begin(order, sc.gfp_mask);

    /**
     *  释放
     */
	nr_reclaimed = do_try_to_free_pages(zonelist, &sc);

	trace_mm_vmscan_direct_reclaim_end(nr_reclaimed);
	set_task_reclaim_state(current, NULL);

	return nr_reclaimed;
}

#ifdef CONFIG_MEMCG

/* Only used by soft limit reclaim. Do not reuse for anything else. */
unsigned long mem_cgroup_shrink_node(struct mem_cgroup *memcg,
						gfp_t gfp_mask, bool noswap,
						pg_data_t *pgdat,
						unsigned long *nr_scanned)
{
	struct lruvec *lruvec = mem_cgroup_lruvec(memcg, pgdat);
	struct scan_control sc = {
		.nr_to_reclaim = SWAP_CLUSTER_MAX,
		.target_mem_cgroup = memcg,
		.may_writepage = !laptop_mode,
		.may_unmap = 1,
		.reclaim_idx = MAX_NR_ZONES - 1,
		.may_swap = !noswap,
	};

	WARN_ON_ONCE(!current->reclaim_state);

	sc.gfp_mask = (gfp_mask & GFP_RECLAIM_MASK) |
			(GFP_HIGHUSER_MOVABLE & ~GFP_RECLAIM_MASK);

	trace_mm_vmscan_memcg_softlimit_reclaim_begin(sc.order,
						      sc.gfp_mask);

	/*
	 * NOTE: Although we can get the priority field, using it
	 * here is not a good idea, since it limits the pages we can scan.
	 * if we don't reclaim here, the shrink_node from balance_pgdat
	 * will pick up pages from other mem cgroup's as well. We hack
	 * the priority and make it zero.
	 */
	shrink_lruvec(lruvec, &sc);

	trace_mm_vmscan_memcg_softlimit_reclaim_end(sc.nr_reclaimed);

	*nr_scanned = sc.nr_scanned;

	return sc.nr_reclaimed;
}

/**
 *
 */
unsigned long try_to_free_mem_cgroup_pages(struct mem_cgroup *memcg,
					   unsigned long nr_pages,
					   gfp_t gfp_mask,
					   bool may_swap)
{
	unsigned long nr_reclaimed;
	unsigned int noreclaim_flag;
	struct scan_control sc = {
		.nr_to_reclaim = max(nr_pages, SWAP_CLUSTER_MAX),
		.gfp_mask = (current_gfp_context(gfp_mask) & GFP_RECLAIM_MASK) |
				(GFP_HIGHUSER_MOVABLE & ~GFP_RECLAIM_MASK),
		.reclaim_idx = MAX_NR_ZONES - 1,
		.target_mem_cgroup = memcg,
		.priority = DEF_PRIORITY,
		.may_writepage = !laptop_mode,
		.may_unmap = 1,
		.may_swap = may_swap,
	};
	/*
	 * Traverse the ZONELIST_FALLBACK zonelist of the current node to put
	 * equal pressure on all the nodes. This is based on the assumption that
	 * the reclaim does not bail out early.
	 */
	struct zonelist *zonelist = node_zonelist(numa_node_id(), sc.gfp_mask);

	set_task_reclaim_state(current, &sc.reclaim_state);
	trace_mm_vmscan_memcg_reclaim_begin(0, sc.gfp_mask);
	noreclaim_flag = memalloc_noreclaim_save();

    /**
     *
     */
	nr_reclaimed = do_try_to_free_pages(zonelist, &sc);

	memalloc_noreclaim_restore(noreclaim_flag);
	trace_mm_vmscan_memcg_reclaim_end(nr_reclaimed);
	set_task_reclaim_state(current, NULL);

	return nr_reclaimed;
}
#endif

/**
 * @brief 对 匿名页面的活跃 LRU 链表进行老化
 *
 * @param pgdat
 * @param sc
 */
static void age_active_anon(struct pglist_data *pgdat,
				struct scan_control *sc)
{
	struct mem_cgroup *memcg;
	struct lruvec *lruvec;

	/**
	 * @brief swap 页数等于0 直接退出
	 *
	 */
	if (!total_swap_pages)
		return;

	/**
	 * @brief 从 memcg 中获取 LRU 链表向量
	 *
	 */
	lruvec = mem_cgroup_lruvec(NULL, pgdat);
	if (!inactive_is_low(lruvec, LRU_INACTIVE_ANON))
		return;

	/**
	 * @brief 遍历 memcg 等级
	 *
	 */
	memcg = mem_cgroup_iter(NULL, NULL, NULL);
	do {
		lruvec = mem_cgroup_lruvec(memcg, pgdat);
		/**
		 * @brief 扫描活跃链表 包括匿名页面 或 文件映射页面
 		 * 	收割 和迁移 一部分(最近一直没被访问)活跃页面到 不活跃链表中
		 *
		 */
		shrink_active_list(SWAP_CLUSTER_MAX, lruvec, sc, LRU_ACTIVE_ANON);
		memcg = mem_cgroup_iter(NULL, memcg, NULL);
	} while (memcg);
}

/**
 *  boost_watermark 函数及用于临时提高水位。
 *  该函数获取当前 zone 水位是否被临时提高了。
 *
 *  boosted: 提升
 */
static bool pgdat_watermark_boosted(pg_data_t *pgdat, int highest_zoneidx)
{
	int i;
	struct zone *zone;

	/*
	 * Check for watermark boosts top-down as the higher zones
	 * are more likely to be boosted. Both watermarks and boosts
	 * should not be checked at the same time as reclaim would
	 * start prematurely when there is no boosting and a lower
	 * zone is balanced.
	 */
	for (i = highest_zoneidx; i >= 0; i--) {
        /**
         *
         */
		zone = pgdat->node_zones + i;
		if (!managed_zone(zone))
			continue;

        /**
         *  如果水位被提升了，那么 watermark_boost 不为 0
         */
		if (zone->watermark_boost)
			return true;
	}

	return false;
}

/*
 * Returns true if there is an eligible(资格) zone balanced for the request order
 * and highest_zoneidx
 *
 * 检查这个内存节点中是否由合适的 ZONE，其水位高于高水位并且能
 * 分配出 2 的 sc.order 次幂 个连续的物理页面
 */
static bool pgdat_balanced(pg_data_t *pgdat, int order, int highest_zoneidx)
{
	int i;
	unsigned long mark = -1;
	struct zone *zone;

	/*
	 * Check watermarks bottom-up as lower zones are more likely to
	 * meet watermarks.
	 */
	for (i = 0; i <= highest_zoneidx; i++) {
		zone = pgdat->node_zones + i;

        /*
         * zone 必须有管理的 page
         */
		if (!managed_zone(zone))
			continue;

        /**
         *  获取高水位位置
         */
		mark = high_wmark_pages(zone);

        /**
         *  水位是否满足要求
         */
		if (zone_watermark_ok_safe(zone, order, mark, highest_zoneidx))
			return true;
	}

	/*
	 * If a node has no populated zone within highest_zoneidx, it does not
	 * need balancing by definition. This can happen if a zone-restricted
	 * allocation tries to wake a remote kswapd.
	 */
	if (mark == -1)
		return true;

	return false;
}

/* Clear pgdat state for congested, dirty or under writeback. */
static void clear_pgdat_congested(pg_data_t *pgdat)
{
	struct lruvec *lruvec = mem_cgroup_lruvec(NULL, pgdat);

	clear_bit(LRUVEC_CONGESTED, &lruvec->flags);
	clear_bit(PGDAT_DIRTY, &pgdat->flags);
	clear_bit(PGDAT_WRITEBACK, &pgdat->flags);
}

/*
 * Prepare kswapd for sleeping. This verifies that there are no processes
 * waiting in throttle_direct_reclaim() and that watermarks have been met.
 *
 * Returns true if kswapd is ready to sleep
 */
static bool prepare_kswapd_sleep(pg_data_t *pgdat, int order,
				int highest_zoneidx)
{
	/*
	 * The throttled processes are normally woken up in balance_pgdat() as
	 * soon as allow_direct_reclaim() is true. But there is a potential
	 * race between when kswapd checks the watermarks and a process gets
	 * throttled. There is also a potential race if processes get
	 * throttled, kswapd wakes, a large process exits thereby balancing the
	 * zones, which causes kswapd to exit balance_pgdat() before reaching
	 * the wake up checks. If kswapd is going to sleep, no process should
	 * be sleeping on pfmemalloc_wait, so wake them now if necessary. If
	 * the wake up is premature, processes will wake kswapd and get
	 * throttled again. The difference from wake ups in balance_pgdat() is
	 * that here we are under prepare_to_wait().
	 */
	if (waitqueue_active(&pgdat->pfmemalloc_wait))
		wake_up_all(&pgdat->pfmemalloc_wait);

	/* Hopeless node, leave it to direct reclaim */
	if (pgdat->kswapd_failures >= MAX_RECLAIM_RETRIES)
		return true;

	/**
	 * @brief 是否已经平衡了
	 *
	 * 检查这个内存节点中是否由合适的 ZONE，其水位高于高水位并且能
	 * 分配出 2 的 sc.order 次幂 个连续的物理页面
	 */
	if (pgdat_balanced(pgdat, order, highest_zoneidx)) {
		clear_pgdat_congested(pgdat);
		return true;
	}

	return false;
}

/*
 * kswapd shrinks a node of pages that are at or below the highest usable
 * zone that is currently unbalanced.
 *
 * Returns true if kswapd scanned at least the requested number of pages to
 * reclaim or if the lack of progress was due to pages under writeback.
 * This is used to determine if the scanning priority needs to be raised.
 *
 * 回收页面 的 核心函数
 */
static bool kswapd_shrink_node(pg_data_t *pgdat,
			       struct scan_control *sc)
{
	struct zone *zone;
	int z;

	/* Reclaim a number of pages proportional to the number of zones */
	sc->nr_to_reclaim = 0;
	for (z = 0; z <= sc->reclaim_idx; z++) {
		zone = pgdat->node_zones + z;
		if (!managed_zone(zone))
			continue;

		sc->nr_to_reclaim += max(high_wmark_pages(zone), SWAP_CLUSTER_MAX);
	}

	/*
	 * Historically care was taken to put equal pressure on all zones but
	 * now pressure is applied based on node LRU order.
	 */
	shrink_node(pgdat, sc);

	/*
	 * Fragmentation may mean that the system cannot be rebalanced for
	 * high-order allocations. If twice the allocation size has been
	 * reclaimed then recheck watermarks only at order-0 to prevent
	 * excessive reclaim. Assume that a process requested a high-order
	 * can direct reclaim/compact.
	 */
	if (sc->order && sc->nr_reclaimed >= compact_gap(sc->order))
		sc->order = 0;

	return sc->nr_scanned >= sc->nr_to_reclaim;
}

/*
 * For kswapd, balance_pgdat() will reclaim pages across a node from zones
 * that are eligible for use by the caller until at least one zone is
 * balanced.
 *
 * Returns the order kswapd finished reclaiming at.
 *
 * kswapd scans the zones in the highmem->normal->dma direction.  It skips
 * zones which have free_pages > high_wmark_pages(zone), but once a zone is
 * found to have free_pages <= high_wmark_pages(zone), any page in that zone
 * or lower is eligible for reclaim until at least one usable zone is
 * balanced.
 *
 *  回收页面的主函数 - 被 kswapd() 调用
 *
 * kswapd 将横跨整个 node zone, 回收那些认为合适的 pages
 * 一旦 zone 被发现当 free_pages <= high_wmark_pages(zone) 将被回收
 */
static int balance_pgdat(pg_data_t *pgdat, int order, int highest_zoneidx)
{
	int i;
	unsigned long nr_soft_reclaimed;
	unsigned long nr_soft_scanned;
	unsigned long pflags;

    /**
     *  提升回收
     *
     *  用于优化外碎片化的机制
	 *
	 * zone_boosts[i] = zone->watermark_boost;
     */
	unsigned long nr_boost_reclaim;
	unsigned long zone_boosts[MAX_NR_ZONES] = { 0, };
	bool boosted;
	struct zone *zone;

	struct scan_control sc = {
		sc.gfp_mask = GFP_KERNEL,
		sc.order = order,
		sc.may_unmap = 1,
	};

	/**
	 * task->reclaim_state = &sc.reclaim_state;
	 *
	 */
	set_task_reclaim_state(current, &sc.reclaim_state);

	psi_memstall_enter(&pflags);

	/**
	 * @brief
	 *
	 */
	__fs_reclaim_acquire();

	/**
	 * @brief vm_event_states.event[PAGEOUTRUN]+1
	 *
	 */
	count_vm_event(PAGEOUTRUN);

	/*
	 * Account for the reclaim boost. Note that the zone boost is left in
	 * place so that parallel allocations that are near the watermark will
	 * stall or direct reclaim until kswapd is finished.
	 */
	nr_boost_reclaim = 0;

	/**
	 * @brief 遍历回收的 zone
	 *
	 */
	for (i = 0; i <= highest_zoneidx; i++) {
		zone = pgdat->node_zones + i;
		/**
		 * @brief 必须有管理的 page
		 *
		 */
		if (!managed_zone(zone))
			continue;

		/**
		 * @brief 提升水位
		 *
		 */
		nr_boost_reclaim += zone->watermark_boost;
		zone_boosts[i] = zone->watermark_boost;
	}
	/**
	 * @brief 是否提升
	 *
	 */
	boosted = nr_boost_reclaim;

restart:
	sc.priority = DEF_PRIORITY; /* DEF_PRIORITY=12 */
	do {
		/**
		 * @brief 已经回收
		 *
		 */
		unsigned long nr_reclaimed = sc.nr_reclaimed;
		bool raise_priority = true;
		bool balanced;
		bool ret;

		sc.reclaim_idx = highest_zoneidx;

		/*
		 * If the number of buffer_heads exceeds the maximum allowed
		 * then consider reclaiming from all zones. This has a dual
		 * purpose -- on 64-bit systems it is expected that
		 * buffer_heads are stripped during active rotation. On 32-bit
		 * systems, highmem pages can pin lowmem memory and shrinking
		 * buffers can relieve lowmem pressure. Reclaim may still not
		 * go ahead if all eligible zones for the original allocation
		 * request are balanced to avoid excessive reclaim from kswapd.
		 */
		if (buffer_heads_over_limit) {
			for (i = MAX_NR_ZONES - 1; i >= 0; i--) {
				zone = pgdat->node_zones + i;
				if (!managed_zone(zone))
					continue;

				sc.reclaim_idx = i;
				break;
			}
		}

		/*
		 * If the pgdat is imbalanced then ignore boosting and preserve
		 * the watermarks for a later time and restart. Note that the
		 * zone watermarks will be still reset at the end of balancing
		 * on the grounds that the normal reclaim should be enough to
		 * re-evaluate if boosting is required when kswapd next wakes.
		 *
		 * 检查这个内存节点中是否由合适的 ZONE，其水位高于高水位并且能
		 * 分配出 2 的 sc.order 次幂 个连续的物理页面。
		 * 如果 没有满足，并且 nr_boost_reclaim
		 */
		balanced = pgdat_balanced(pgdat, sc.order, highest_zoneidx);
		if (!balanced && nr_boost_reclaim) {
			nr_boost_reclaim = 0;
			goto restart;
		}

		/*
		 * If boosting is not active then only reclaim if there are no
		 * eligible zones. Note that sc.reclaim_idx is not used as
		 * buffer_heads_over_limit may have adjusted it.
		 *
		 * 若符合条件，跳转 out
		 */
		if (!nr_boost_reclaim && balanced)
			goto out;

		/* Limit the priority of boosting to avoid reclaim writeback */
		if (nr_boost_reclaim && sc.priority == DEF_PRIORITY - 2)
			raise_priority = false;

		/*
		 * Do not writeback or swap pages for boosted reclaim. The
		 * intent is to relieve pressure not issue sub-optimal IO
		 * from reclaim context. If no pages are reclaimed, the
		 * reclaim will be aborted.
		 */
		sc.may_writepage = !laptop_mode && !nr_boost_reclaim;
		sc.may_swap = !nr_boost_reclaim;

		/*
		 * Do some background aging of the anon list, to give
		 * pages a chance to be referenced before reclaiming. All
		 * pages are rotated regardless of classzone as this is
		 * about consistent aging.
		 *
		 * 对 匿名页面的活跃 LRU 链表进行老化
		 */
		age_active_anon(pgdat, &sc);

		/*
		 * If we're getting trouble reclaiming, start doing writepage
		 * even in laptop mode.
		 */
		if (sc.priority < DEF_PRIORITY - 2)
			sc.may_writepage = 1;

		/* Call soft limit reclaim before calling shrink_node. */
		sc.nr_scanned = 0;
		nr_soft_scanned = 0;
		nr_soft_reclaimed = mem_cgroup_soft_limit_reclaim(pgdat, sc.order,
						        sc.gfp_mask, &nr_soft_scanned);
		sc.nr_reclaimed += nr_soft_reclaimed;

		/*
		 * There should be no need to raise the scanning priority if
		 * enough pages are already being scanned that that high
		 * watermark would be met at 100% efficiency.
		 *
		 * 回收页面 的 核心函数
		 */
		if (kswapd_shrink_node(pgdat, &sc))
			raise_priority = false;

		/*
		 * If the low watermark is met there is no need for processes
		 * to be throttled on pfmemalloc_wait as they should not be
		 * able to safely make forward progress. Wake them
		 */
		if (waitqueue_active(&pgdat->pfmemalloc_wait) &&
				allow_direct_reclaim(pgdat))
			wake_up_all(&pgdat->pfmemalloc_wait);

		/* Check if kswapd should be suspending */
		__fs_reclaim_release();
		ret = try_to_freeze();
		__fs_reclaim_acquire();
		if (ret || kthread_should_stop())
			break;

		/*
		 * Raise priority if scanning rate is too low or there was no
		 * progress in reclaiming pages
		 */
		nr_reclaimed = sc.nr_reclaimed - nr_reclaimed;
		nr_boost_reclaim -= min(nr_boost_reclaim, nr_reclaimed);

		/*
		 * If reclaim made no progress for a boost, stop reclaim as
		 * IO cannot be queued and it could be an infinite loop in
		 * extreme circumstances.
		 */
		if (nr_boost_reclaim && !nr_reclaimed)
			break;


		if (raise_priority || !nr_reclaimed)
			sc.priority--;  /* 不断加大扫描粒度(页 (total_size >> priority)) */

	} while (sc.priority >= 1);

	if (!sc.nr_reclaimed)
		pgdat->kswapd_failures++;

out:
	/* If reclaim was boosted, account for the reclaim done in this pass */
	if (boosted) {
		unsigned long flags;

		for (i = 0; i <= highest_zoneidx; i++) {
			if (!zone_boosts[i])
				continue;

			/* Increments are under the zone lock */
			zone = pgdat->node_zones + i;
			spin_lock_irqsave(&zone->lock, flags);

            /**
             *  将水位恢复为原来的水位
             *
             * 临时提高水位是发生在 使用后备 free_area 时，由 boost_watermark 提高的水位
             * 在 rmqueue 中根据 标志位 ZONE_BOOSTED_WATERMARK 决定是否要唤醒 kswapd 线程。
             *
             * zone->watermark_boost 在 boost_watermark() 中被提高。
             */
			zone->watermark_boost -= min(zone->watermark_boost, zone_boosts[i]);
			spin_unlock_irqrestore(&zone->lock, flags);
		}

		/*
		 * As there is now likely space, wakeup kcompact to defragment(解冻)
		 * pageblocks.
		 *
		 * 唤醒 内存规整进程
		 */
		wakeup_kcompactd(pgdat, pageblock_order, highest_zoneidx);
	}

	snapshot_refaults(NULL, pgdat);
	__fs_reclaim_release();
	psi_memstall_leave(&pflags);
	set_task_reclaim_state(current, NULL);

	/*
	 * Return the order kswapd stopped reclaiming at as
	 * prepare_kswapd_sleep() takes it into account. If another caller
	 * entered the allocator slow path while kswapd was awake, order will
	 * remain at the higher level.
	 */
	return sc.order;
}

/*
 * The pgdat->kswapd_highest_zoneidx is used to pass the highest zone index to
 * be reclaimed by kswapd from the waker. If the value is MAX_NR_ZONES which is
 * not a valid index then either kswapd runs for first time or kswapd couldn't
 * sleep after previous reclaim attempt (node is still unbalanced). In that
 * case return the zone index of the previous kswapd reclaim cycle.
 */
static enum zone_type kswapd_highest_zoneidx(pg_data_t *pgdat,
					   enum zone_type prev_highest_zoneidx)
{
	enum zone_type curr_idx = READ_ONCE(pgdat->kswapd_highest_zoneidx);

	return curr_idx == MAX_NR_ZONES ? prev_highest_zoneidx : curr_idx;
}

/**
 *  系统启动时，会在该函数中睡眠并让出 CPU 控制权
 *
 *  kswapd 睡眠于此(kswapd 唤醒点),
 *  唤醒方式 `wakeup_kswapd()`->`wake_up_interruptible(&pgdat->kswapd_wait)`
 */
static void kswapd_try_to_sleep(pg_data_t *pgdat, int alloc_order, int reclaim_order,
				unsigned int highest_zoneidx)
{
	long remaining = 0;
	DEFINE_WAIT(wait);

	if (freezing(current) || kthread_should_stop())
		return;

	/**
	 * @brief
	 *
	 */
	prepare_to_wait(&pgdat->kswapd_wait, &wait, TASK_INTERRUPTIBLE);

	/*
	 * Try to sleep for a short interval. Note that kcompactd will only be
	 * woken if it is possible to sleep for a short interval. This is
	 * deliberate on the assumption that if reclaim cannot keep an
	 * eligible zone balanced that it's also unlikely that compaction will
	 * succeed.
	 */
	if (prepare_kswapd_sleep(pgdat, reclaim_order, highest_zoneidx)) {
		/*
		 * Compaction records what page blocks it recently failed to
		 * isolate pages from and skips them in the future scanning.
		 * When kswapd is going to sleep, it is reasonable to assume
		 * that pages and compaction may succeed so reset the cache.
		 */
		reset_isolation_suitable(pgdat);

		/*
		 * We have freed the memory, now we should compact it to make
		 * allocation of the requested order possible.
		 *
		 * 唤醒 内存规整进程
		 */
		wakeup_kcompactd(pgdat, alloc_order, highest_zoneidx);

		remaining = schedule_timeout(HZ/10);

		/*
		 * If woken prematurely then reset kswapd_highest_zoneidx and
		 * order. The values will either be from a wakeup request or
		 * the previous request that slept prematurely.
		 */
		if (remaining) {
			WRITE_ONCE(pgdat->kswapd_highest_zoneidx,
					kswapd_highest_zoneidx(pgdat, highest_zoneidx));

			if (READ_ONCE(pgdat->kswapd_order) < reclaim_order)
				WRITE_ONCE(pgdat->kswapd_order, reclaim_order);
		}

		/**
		 * @brief
		 *
		 */
		finish_wait(&pgdat->kswapd_wait, &wait);
		prepare_to_wait(&pgdat->kswapd_wait, &wait, TASK_INTERRUPTIBLE);
	}

	/*
	 * After a short sleep, check if it was a premature sleep. If not, then
	 * go fully to sleep until explicitly woken up.
	 */
	if (!remaining &&
	    prepare_kswapd_sleep(pgdat, reclaim_order, highest_zoneidx)) {
		trace_mm_vmscan_kswapd_sleep(pgdat->node_id);

		/*
		 * vmstat counters are not perfectly accurate and the estimated
		 * value for counters such as NR_FREE_PAGES can deviate from the
		 * true value by nr_online_cpus * threshold. To avoid the zone
		 * watermarks being breached while under pressure, we reduce the
		 * per-cpu vmstat threshold while kswapd is awake and restore
		 * them before going back to sleep.
		 */
		set_pgdat_percpu_threshold(pgdat, calculate_normal_threshold);

		/**
		 * @brief
		 *
		 */
		if (!kthread_should_stop())
			schedule();

		set_pgdat_percpu_threshold(pgdat, calculate_pressure_threshold);
	/**
	 * @brief
	 *
	 */
	} else {
		if (remaining)
			count_vm_event(KSWAPD_LOW_WMARK_HIT_QUICKLY);
		else
			count_vm_event(KSWAPD_HIGH_WMARK_HIT_QUICKLY);
	}
	finish_wait(&pgdat->kswapd_wait, &wait);
}

/*
 * The background pageout daemon, started as a kernel thread
 * from the init process.
 *
 * This basically trickles out pages so that we have _some_
 * free memory available even if there is no other activity
 * that frees anything up. This is needed for things like routing
 * etc, where we otherwise might have all activity going on in
 * asynchronous contexts that cannot page things out.
 *
 * If there are applications that are active memory-allocators
 * (most normal use), this basically shouldn't matter.
 *
 * 页面回收线程 - 每一个 NODE 一个线程
 */
static int kswapd(void *p)  /* swap 线程回调函数 */
{
	unsigned int alloc_order, reclaim_order;
	unsigned int highest_zoneidx = MAX_NR_ZONES - 1;

    /**
     *  NODE 内存结构
     */
	pg_data_t *pgdat = (pg_data_t*)p;   /* NUMA 内存 */

	struct task_struct *tsk = current;  /* current 为swap 进程 */

	/**
	 * @brief 在这个 NODE 上的CPU
	 *
	 */
    const struct cpumask *cpumask = cpumask_of_node(pgdat->node_id);

	if (!cpumask_empty(cpumask))    /* 如果当前 node    的 CPU mask 不为空 */
		set_cpus_allowed_ptr(tsk, cpumask); /* 设置 swap 进程的调度类 的CPUmask  */

	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__alloc_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 *
	 * 设置进程描述符中的 标志位
	 * PF_MEMALLOC  标志进程可以使用 系统预留 内存，不用关心ZONE水位
	 * PF_SWAPWRITE 允许写交换分区
	 * PF_KSWAPD    kswapd 线程
	 */
	tsk->flags |= PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD;

	/**
	 * @brief 设置进程可以冻结
	 *
	 */
	set_freezable();

	WRITE_ONCE(pgdat->kswapd_order, 0);
	WRITE_ONCE(pgdat->kswapd_highest_zoneidx, MAX_NR_ZONES);

    /**
     *  主循环
     */
	for ( ; ; ) {   /* 开始回收 */

		bool ret;

        /**
         * @brief 要回收的页面数量
         *
         */
		alloc_order = reclaim_order = READ_ONCE(pgdat->kswapd_order);

        /**
         * @brief 可以扫描和回收的最高 ZONE，扫描顺序: 从 高ZONE 到 低ZONE
         *
         */
		highest_zoneidx = kswapd_highest_zoneidx(pgdat, highest_zoneidx);

kswapd_try_sleep:

        /**
         *  系统启动时，会在该函数中睡眠并让出 CPU 控制权
         *
         *  kswapd 睡眠于此(kswapd 唤醒点),
         *  唤醒方式 `wakeup_kswapd()`->`wake_up_interruptible(&pgdat->kswapd_wait)`
         */
		kswapd_try_to_sleep(pgdat, alloc_order, reclaim_order, highest_zoneidx);

		/* Read the new order and highest_zoneidx */
		alloc_order = reclaim_order = READ_ONCE(pgdat->kswapd_order);

        /**
         * @brief 可以扫描和回收的最高 ZONE，扫描顺序: 从 高ZONE 到 低ZONE
         *
         */
		highest_zoneidx = kswapd_highest_zoneidx(pgdat, highest_zoneidx);

		WRITE_ONCE(pgdat->kswapd_order, 0);
		WRITE_ONCE(pgdat->kswapd_highest_zoneidx, MAX_NR_ZONES);

		/**
		 * @brief
		 *
		 */
		ret = try_to_freeze();  /* 冻结 */

		/**
		 * @brief 是否需要停止
		 *
		 */
		if (kthread_should_stop())
			break;

		/*
		 * We can speed up thawing tasks if we don't call balance_pgdat
		 * after returning from the refrigerator
		 */
		if (ret)    /* 如果 冻结成功，继续 */
			continue;

		/*
		 * Reclaim begins at the requested order but if a high-order
		 * reclaim fails then kswapd falls back to reclaiming for
		 * order-0. If that happens, kswapd will consider sleeping
		 * for the order it finished reclaiming at (reclaim_order)
		 * but kcompactd is woken to compact for the original
		 * request (alloc_order).
		 *
		 * 追踪
		 */
		trace_mm_vmscan_kswapd_wake(pgdat->node_id, highest_zoneidx, alloc_order);

        /**
         *  回收页面的主函数
         *
         * kswapd 将横跨整个 node zone, 回收那些认为合适的 pages
         * 一旦 zone 被发现当 free_pages <= high_wmark_pages(zone) 将被回收
         */
		reclaim_order = balance_pgdat(pgdat,
                                      alloc_order,   /* 进行 swap */
						              highest_zoneidx);
		if (reclaim_order < alloc_order)
			goto kswapd_try_sleep;
	}

	tsk->flags &= ~(PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD);

	return 0;
}

/*
 * A zone is low on free memory or too fragmented for high-order memory.  If
 * kswapd should reclaim (direct reclaim is deferred), wake it up for the zone's
 * pgdat.  It will wake up kcompactd after reclaiming memory.  If kswapd reclaim
 * has failed or is not needed, still wake up kcompactd if only compaction is
 * needed.
 *
 * 一个区域的可用内存不足，或者对于高阶内存来说太碎片化了。
 * 如果 kswapd 应该回收（直接回收被推迟），则为区域的 pgdat 唤醒它。
 * 回收内存后会唤醒kcompactd。
 * 如果 kswapd reclaim 失败或不需要，如果只需要压缩，仍然唤醒 kcompactd。
 *
 * 唤醒 kswapd 进程进行异步内存回收
 */
void wakeup_kswapd(struct zone *zone, gfp_t gfp_flags, int order,
		   enum zone_type highest_zoneidx)
{
	pg_data_t *pgdat;
	enum zone_type curr_idx;

    /* zone 管理的 page 为 0 */
	if (!managed_zone(zone))
		return;


	if (!cpuset_zone_allowed(zone, gfp_flags))
		return;

    /* zone 隶属 NODE */
	pgdat = zone->zone_pgdat;
	curr_idx = READ_ONCE(pgdat->kswapd_highest_zoneidx);

	if (curr_idx == MAX_NR_ZONES || curr_idx < highest_zoneidx)
		WRITE_ONCE(pgdat->kswapd_highest_zoneidx, highest_zoneidx);

	if (READ_ONCE(pgdat->kswapd_order) < order)
		WRITE_ONCE(pgdat->kswapd_order, order);

    /**
     *  唤醒 kswapd ?
     */
	if (!waitqueue_active(&pgdat->kswapd_wait))
		return;

	/**
	 *  Hopeless node, leave it to direct reclaim if possible
	 *
	 *  1. kswapd 失败 >= 16 次
	 *  2. 检查这个内存节点中是否由合适的 ZONE，其水位高于高水位并且能
	 * 		分配出 2 的 sc.order 次幂 个连续的物理页面
	 * 	   并且 水位没有被临时提升
	 */
	if (pgdat->kswapd_failures >= MAX_RECLAIM_RETRIES ||
	    (pgdat_balanced(pgdat, order, highest_zoneidx) &&
	     !pgdat_watermark_boosted(pgdat, highest_zoneidx))) {
		/*
		 * There may be plenty of free memory available, but it's too
		 * fragmented for high-order allocations.  Wake up kcompactd
		 * and rely on compaction_suitable() to determine if it's
		 * needed.  If it fails, it will defer subsequent attempts to
		 * ratelimit its work.
		 *
		 * 如果没有设置 直接内存回收 标志，那么就得做内存规整了
		 */
		if (!(gfp_flags & __GFP_DIRECT_RECLAIM)) {
            /**
             *  唤醒 内存规整进程
             */
			wakeup_kcompactd(pgdat, order, highest_zoneidx);
        }
		return;
	}

    /**
     *  跟踪唤醒 kswapd 事件
     */
	trace_mm_vmscan_wakeup_kswapd(pgdat->node_id, highest_zoneidx, order, gfp_flags);

    /**
     *  唤醒进程
     */
	wake_up_interruptible(&pgdat->kswapd_wait);
}

#ifdef CONFIG_HIBERNATION
/*
 * Try to free `nr_to_reclaim' of memory, system-wide, and return the number of
 * freed pages.
 *
 * Rather than trying to age LRUs the aim is to preserve the overall
 * LRU order by reclaiming preferentially
 * inactive > active > active referenced > active mapped
 */
unsigned long shrink_all_memory(unsigned long nr_to_reclaim)
{
	struct scan_control sc = {
		.nr_to_reclaim = nr_to_reclaim,
		.gfp_mask = GFP_HIGHUSER_MOVABLE,
		.reclaim_idx = MAX_NR_ZONES - 1,
		.priority = DEF_PRIORITY,
		.may_writepage = 1,
		.may_unmap = 1,
		.may_swap = 1,
		.hibernation_mode = 1,
	};
	struct zonelist *zonelist = node_zonelist(numa_node_id(), sc.gfp_mask);
	unsigned long nr_reclaimed;
	unsigned int noreclaim_flag;

	fs_reclaim_acquire(sc.gfp_mask);
	noreclaim_flag = memalloc_noreclaim_save();
	set_task_reclaim_state(current, &sc.reclaim_state);

	nr_reclaimed = do_try_to_free_pages(zonelist, &sc);

	set_task_reclaim_state(current, NULL);
	memalloc_noreclaim_restore(noreclaim_flag);
	fs_reclaim_release(sc.gfp_mask);

	return nr_reclaimed;
}
#endif /* CONFIG_HIBERNATION */

/*
 * This kswapd start function will be called by init and node-hot-add.
 * On node-hot-add, kswapd will moved to proper cpus if cpus are hot-added.
 */
int kswapd_run(int nid) /* 运行 */
{
	pg_data_t *pgdat = NODE_DATA(nid);  /* 获取 NUMA 内存布局 */
	int ret = 0;

	if (pgdat->kswapd)  /* 如果已经运行，直接返回 */
		return 0;

	/**
	 * @brief 创建 kswapd
	 *
	 */
	pgdat->kswapd = kthread_run(kswapd, pgdat, "kswapd%d", nid);
	if (IS_ERR(pgdat->kswapd)) {
		/* failure at boot is fatal */
		BUG_ON(system_state < SYSTEM_RUNNING);
		pr_err("Failed to start kswapd on node %d\n", nid);
		ret = PTR_ERR(pgdat->kswapd);
		pgdat->kswapd = NULL;
	}
	return ret;
}

/*
 * Called by memory hotplug when all memory in a node is offlined.  Caller must
 * hold mem_hotplug_begin/end().
 */
void kswapd_stop(int nid)
{
	struct task_struct *kswapd = NODE_DATA(nid)->kswapd;

	if (kswapd) {
		kthread_stop(kswapd);
		NODE_DATA(nid)->kswapd = NULL;
	}
}

/**
 * @brief 初始化 kswapd
 *
 * @return int
 */
static int __init kswapd_init(void)
{
	int nid;

	/**
	 * @brief
	 *
	 */
	swap_setup();

	/**
	 * @brief 每个 node 创建一个 kswapd
	 *
	 */
	for_each_node_state(nid, N_MEMORY) {
		/**
		 * @brief
		 *
		 */
 		kswapd_run(nid);
	}
	return 0;
}

module_init(kswapd_init)

#ifdef CONFIG_NUMA
/*
 * Node reclaim mode
 *
 * If non-zero call node_reclaim when the number of free pages falls below
 * the watermarks.
 *
 * vm.zone_reclaim_mode = 0, see `struct ctl_table vm_table[]`
 *
 *  =0 表示可以从下一个内存管理区或下一个内存节点分配内存
 * !=0 否则表示可以再这个内存管理区进行一些内存回收，然后继续尝试在该zone中分配内存
 */
int __read_mostly node_reclaim_mode ;

#define RECLAIM_WRITE (1<<0)	/* Writeout pages during reclaim 回收过程中的注销页 */
#define RECLAIM_UNMAP (1<<1)	/* Unmap pages during reclaim 在回收过程中取消映射 */

/*
 * Priority for NODE_RECLAIM. This determines the fraction of pages
 * of a node considered for each zone_reclaim. 4 scans 1/16th of
 * a zone.
 */
#define NODE_RECLAIM_PRIORITY 4

/*
 * Percentage of pages in a zone that must be unmapped for node_reclaim to
 * occur.
 */
int sysctl_min_unmapped_ratio = 1;

/*
 * If the number of slab pages in a zone grows beyond this percentage then
 * slab reclaim needs to occur.
 */
int sysctl_min_slab_ratio = 5;

/**
 *  从 NODE 中回收 文件映射的页面
 */
static inline unsigned long node_unmapped_file_pages(struct pglist_data *pgdat)
{
    /**
     *  直接共统计信息中获取
     */
	unsigned long file_mapped = node_page_state(pgdat, NR_FILE_MAPPED);
	unsigned long file_lru = node_page_state(pgdat, NR_INACTIVE_FILE) + node_page_state(pgdat, NR_ACTIVE_FILE);

	/*
	 * It's possible for there to be more file mapped pages than
	 * accounted for by the pages on the file LRU lists because
	 * tmpfs pages accounted for as ANON can also be FILE_MAPPED
	 */
	return (file_lru > file_mapped) ? (file_lru - file_mapped) : 0;
}

/**
 *  有多少 pagecache 页面可以回收
 */
/* Work out how many page cache pages we can reclaim in this reclaim_mode */
static unsigned long node_pagecache_reclaimable(struct pglist_data *pgdat)
{
	unsigned long nr_pagecache_reclaimable;
	unsigned long delta = 0;

	/*
	 * If RECLAIM_UNMAP is set, then all file pages are considered
	 * potentially reclaimable. Otherwise, we have to worry about
	 * pages like swapcache and node_unmapped_file_pages() provides
	 * a better estimate
	 *
	 * 如果 设置了 RECLAIM_UNMAP, 那么所有文件页都考虑被回收。
	 *
	 * node_reclaim_mode
     *   =0 表示可以从下一个内存管理区或下一个内存节点分配内存
     *  !=0 否则表示可以再这个内存管理区进行一些内存回收，然后继续尝试在该zone中分配内存
	 */
	if (node_reclaim_mode & RECLAIM_UNMAP)
		nr_pagecache_reclaimable = node_page_state(pgdat, NR_FILE_PAGES);
    /**
     *  否则，下面的是比较好的方案，返回回收页面数
     */
	else
		nr_pagecache_reclaimable = node_unmapped_file_pages(pgdat);

    /**
     *  脏页 不能回收
     */
	/* If we can't clean pages, remove dirty pages from consideration */
	if (!(node_reclaim_mode & RECLAIM_WRITE))
		delta += node_page_state(pgdat, NR_FILE_DIRTY);

	/* Watch for any possible underflows due to delta */
	if (unlikely(delta > nr_pagecache_reclaimable))
		delta = nr_pagecache_reclaimable;

    /**
     *  可回收的页面 - 脏页
     */
	return nr_pagecache_reclaimable - delta;
}

/*
 * Try to free up some pages from this node through reclaim.
 */
static int __node_reclaim(struct pglist_data *pgdat, gfp_t gfp_mask, unsigned int order)
{
	/* Minimum pages needed in order to stay on node */
	const unsigned long nr_pages = 1 << order;
	struct task_struct *p = current;
	unsigned int noreclaim_flag;

    /**
     *  扫描控制 结构
     */
	struct scan_control sc = {
		sc.nr_to_reclaim    = max(nr_pages, SWAP_CLUSTER_MAX/* 32 */),  /* 最少也要回收 32 个页面 */
		sc.gfp_mask         = current_gfp_context(gfp_mask),
		sc.order            = order,
		sc.priority         = NODE_RECLAIM_PRIORITY,    /* 回收优先级, 和上面的32 有关系 */
		sc.may_writepage    = !!(node_reclaim_mode & RECLAIM_WRITE),
		sc.may_unmap        = !!(node_reclaim_mode & RECLAIM_UNMAP),
		sc.may_swap         = 1,
		sc.reclaim_idx      = gfp_zone(gfp_mask),   /* 最高允许页面回收的zone */
	};

    /**
     *  跟踪 页面回收开始
     */
	trace_mm_vmscan_node_reclaim_begin(pgdat->node_id, order,
					   sc.gfp_mask);

    /**
     *  主动让出 cpu 防止 softlockup
     */
	cond_resched();

    /**
     *  fs回收？
     */
	fs_reclaim_acquire(sc.gfp_mask);

	/*
	 * We need to be able to allocate from the reserves for RECLAIM_UNMAP
	 * and we also need to be able to write out pages for RECLAIM_WRITE
	 * and RECLAIM_UNMAP.
	 */
	noreclaim_flag = memalloc_noreclaim_save();
	p->flags |= PF_SWAPWRITE;
	set_task_reclaim_state(p, &sc.reclaim_state);

    /**
     *  可回收的 页面数 > 最小不可映射页面数
     */
	if (node_pagecache_reclaimable(pgdat) > pgdat->min_unmapped_pages) {
		/*
		 * Free memory by calling shrink node with increasing
		 * priorities until we have enough memory freed.
		 */
		do {
            /**
             *  进行回收
             */
			shrink_node(pgdat, &sc);

        /**
         *  没回收完，继续，判断条件
         *  1. 回收的页面数少于需要的页面数
         *  2. priority
         */
		} while (sc.nr_reclaimed < nr_pages && --sc.priority >= 0);
	}

    /**
     *
     */
	set_task_reclaim_state(p, NULL);
	current->flags &= ~PF_SWAPWRITE;
	memalloc_noreclaim_restore(noreclaim_flag);
	fs_reclaim_release(sc.gfp_mask);

    /**
     *  回收结束
     */
	trace_mm_vmscan_node_reclaim_end(sc.nr_reclaimed);

	return sc.nr_reclaimed >= nr_pages;
}

/**
 *  分配过程，主动回收页面(get_page_from_freelist()被调用)
 */
int node_reclaim(struct pglist_data *pgdat, gfp_t gfp_mask, unsigned int order)
{
	int ret;

	/*
	 * Node reclaim reclaims unmapped file backed pages and
	 * slab pages if we are over the defined limits.
	 *
	 * A small portion of unmapped file backed pages is needed for
	 * file I/O otherwise pages read by file I/O will be immediately
	 * thrown out if the node is overallocated. So we do not reclaim
	 * if less than a specified percentage of the node is used by
	 * unmapped file backed pages.
	 *
	 * 如果我们超过定义的限度，节点回收未映射的文件备份页面和板页。
	 *
     * 文件 I/O 需要一小部分未映射的文件支持页面，否则如果节点被整体分配，
     * 文件 I/O 读取的页面将立即被丢弃。
     * 因此，如果未映射的文件支持页面使用少于指定比例的节点，则我们不会收回。
     *
	 * 先回收 page cache, 也就是 文件缓存
	 */
	if (node_pagecache_reclaimable(pgdat) <= pgdat->min_unmapped_pages &&   /* 可回收 <= 最低 unmap 页面 */
	    node_page_state_pages(pgdat, NR_SLAB_RECLAIMABLE_B) <= pgdat->min_slab_pages)
		return NODE_RECLAIM_FULL;

	/*
	 * Do not scan if the allocation should not be delayed.
	 *
	 * 如果分配内存不能延迟太久，就别 scan了
	 */
	if (!gfpflags_allow_blocking(gfp_mask) || (current->flags & PF_MEMALLOC))
		return NODE_RECLAIM_NOSCAN;

	/*
	 * Only run node reclaim on the local node or on nodes that do not
	 * have associated processors. This will favor the local processor
	 * over remote processors and spread off node memory allocations
	 * as wide as possible.
	 *
	 *
	 */
	if (node_state(pgdat->node_id, N_CPU) && pgdat->node_id != numa_node_id())
		return NODE_RECLAIM_NOSCAN;

    /**
     *  回收锁定
     */
	if (test_and_set_bit(PGDAT_RECLAIM_LOCKED, &pgdat->flags))
		return NODE_RECLAIM_NOSCAN;

    /**
     *  上面的条件都满足，进行回收
     */
	ret = __node_reclaim(pgdat, gfp_mask, order);

    /**
     *  清理 回收 锁定 位
     */
	clear_bit(PGDAT_RECLAIM_LOCKED, &pgdat->flags);

	if (!ret)
		count_vm_event(PGSCAN_ZONE_RECLAIM_FAILED);

	return ret;
}
#endif

/**
 * check_move_unevictable_pages - check pages for evictability and move to
 * appropriate zone lru list
 * @pvec: pagevec with lru pages to check
 *
 * Checks pages for evictability, if an evictable page is in the unevictable
 * lru list, moves it to the appropriate evictable lru list. This function
 * should be only used for lru pages.
 */
void check_move_unevictable_pages(struct pagevec *pvec)
{
	struct lruvec *lruvec;
	struct pglist_data *pgdat = NULL;
	int pgscanned = 0;
	int pgrescued = 0;
	int i;

	for (i = 0; i < pvec->nr; i++) {
		struct page *page = pvec->pages[i];
		struct pglist_data *pagepgdat = page_pgdat(page);
		int nr_pages;

		if (PageTransTail(page))
			continue;

		nr_pages = thp_nr_pages(page);
		pgscanned += nr_pages;

		if (pagepgdat != pgdat) {
			if (pgdat)
				spin_unlock_irq(&pgdat->lru_lock);
			pgdat = pagepgdat;
			spin_lock_irq(&pgdat->lru_lock);
		}
		lruvec = mem_cgroup_page_lruvec(page, pgdat);

		if (!PageLRU(page) || !PageUnevictable(page))
			continue;

		if (page_evictable(page)) {
			enum lru_list lru = page_lru_base_type(page);

			VM_BUG_ON_PAGE(PageActive(page), page);
			ClearPageUnevictable(page);
			del_page_from_lru_list(page, lruvec, LRU_UNEVICTABLE);
			add_page_to_lru_list(page, lruvec, lru);
			pgrescued += nr_pages;
		}
	}

	if (pgdat) {
		__count_vm_events(UNEVICTABLE_PGRESCUED, pgrescued);
		__count_vm_events(UNEVICTABLE_PGSCANNED, pgscanned);
		spin_unlock_irq(&pgdat->lru_lock);
	}
}
EXPORT_SYMBOL_GPL(check_move_unevictable_pages);
