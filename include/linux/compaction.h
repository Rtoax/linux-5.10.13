/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_COMPACTION_H
#define _LINUX_COMPACTION_H

/*
 * Determines how hard direct compaction should try to succeed.
 * Lower value means higher priority, analogically to reclaim priority.
 */
enum compact_priority {
	COMPACT_PRIO_SYNC_FULL,
	MIN_COMPACT_PRIORITY = COMPACT_PRIO_SYNC_FULL,
	COMPACT_PRIO_SYNC_LIGHT,
	MIN_COMPACT_COSTLY_PRIORITY = COMPACT_PRIO_SYNC_LIGHT,
	DEF_COMPACT_PRIORITY = COMPACT_PRIO_SYNC_LIGHT,
	COMPACT_PRIO_ASYNC,
	INIT_COMPACT_PRIORITY = COMPACT_PRIO_ASYNC
};

/**
 *  内存规整结果
 */
/* Return values for compact_zone() and try_to_compact_pages() */
/* When adding new states, please adjust include/trace/events/compaction.h */
enum compact_result {
	/* For more detailed tracepoint output - internal to compaction */
	COMPACT_NOT_SUITABLE_ZONE,
	/*
	 * compaction didn't start as it was not possible or direct reclaim
	 * was more suitable
	 *
	 * 内存规整不满足条件，退出
	 */
	COMPACT_SKIPPED,

    /**
     *  因为过去的一些错误导致内存规整退出
     */
	/* compaction didn't start as it was deferred due to past failures */
	COMPACT_DEFERRED,

    /**
     *  没有合适的 规整页
     */
	/* For more detailed tracepoint output - internal to compaction */
	COMPACT_NO_SUITABLE_PAGE,

    /**
     *  标识可以再下一个页块中进行内存规整
     */
	/* compaction should continue to another pageblock */
	COMPACT_CONTINUE,

	/*
	 * The full zone was compacted scanned but wasn't successfull to compact
	 * suitable pages.
	 *
	 * 标识已经完成一轮 的页面扫描，但是没能满足页面分配请求的需求
	 */
	COMPACT_COMPLETE,

	/*
	 * direct compaction has scanned part of the zone but wasn't successfull
	 * to compact suitable pages.
	 *
	 * 标识根据直接页面回收机制，已经扫描了ZONE中部分的页面，但是没有找到可以进行内存规整的页面
	 */
	COMPACT_PARTIAL_SKIPPED,

    /**
     *  处于某些锁竞争的原因，退出内存规整
     */
	/* compaction terminated prematurely due to lock contentions */
	COMPACT_CONTENDED,

	/*
	 * direct compaction terminated after concluding that the allocation
	 * should now succeed
	 *
	 * 标识已经满足页面分配请求的续期，从而退出这次直接内存规整
	 */
	COMPACT_SUCCESS,
};

struct alloc_context; /* in mm/internal.h */

/*
 * Number of free order-0 pages that should be available above given watermark
 * to make sure compaction has reasonable chance of not running out of free
 * pages that it needs to isolate as migration target during its work.
 */
static inline unsigned long compact_gap(unsigned int order)
{
	/*
	 * Although all the isolations for migration are temporary, compaction
	 * free scanner may have up to 1 << order pages on its list and then
	 * try to split an (order - 1) free page. At that point, a gap of
	 * 1 << order might not be enough, so it's safer to require twice that
	 * amount. Note that the number of pages on the list is also
	 * effectively limited by COMPACT_CLUSTER_MAX, as that's the maximum
	 * that the migrate scanner can have isolated on migrate list, and free
	 * scanner is only invoked when the number of isolated free pages is
	 * lower than that. But it's not worth to complicate the formula here
	 * as a bigger gap for higher orders than strictly necessary can also
	 * improve chances of compaction success.
	 */
	return 2UL << order;
}

#ifdef CONFIG_COMPACTION
extern int sysctl_compact_memory;
extern unsigned int sysctl_compaction_proactiveness;
extern int sysctl_compaction_handler(struct ctl_table *table, int write,
			void *buffer, size_t *length, loff_t *ppos);
extern int sysctl_extfrag_threshold;
extern int sysctl_compact_unevictable_allowed;

extern unsigned int extfrag_for_order(struct zone *zone, unsigned int order);
extern int fragmentation_index(struct zone *zone, unsigned int order);
extern enum compact_result try_to_compact_pages(gfp_t gfp_mask,
		unsigned int order, unsigned int alloc_flags,
		const struct alloc_context *ac, enum compact_priority prio,
		struct page **page);
extern void reset_isolation_suitable(pg_data_t *pgdat);
extern enum compact_result compaction_suitable(struct zone *zone, int order,
		unsigned int alloc_flags, int highest_zoneidx);

extern void defer_compaction(struct zone *zone, int order);
extern bool compaction_deferred(struct zone *zone, int order);
extern void compaction_defer_reset(struct zone *zone, int order,
				bool alloc_success);
extern bool compaction_restarting(struct zone *zone, int order);

/* Compaction has made some progress and retrying makes sense */
static inline bool compaction_made_progress(enum compact_result result)
{
	/*
	 * Even though this might sound confusing this in fact tells us
	 * that the compaction successfully isolated and migrated some
	 * pageblocks.
	 */
	if (result == COMPACT_SUCCESS)
		return true;

	return false;
}

/* Compaction has failed and it doesn't make much sense to keep retrying. */
static inline bool compaction_failed(enum compact_result result)
{
	/* All zones were scanned completely and still not result. */
	if (result == COMPACT_COMPLETE)
		return true;

	return false;
}

/* Compaction needs reclaim to be performed first, so it can continue. */
static inline bool compaction_needs_reclaim(enum compact_result result)
{
	/*
	 * Compaction backed off due to watermark checks for order-0
	 * so the regular reclaim has to try harder and reclaim something.
	 */
	if (result == COMPACT_SKIPPED)
		return true;

	return false;
}

/*
 * Compaction has backed off for some reason after doing some work or none
 * at all. It might be throttling or lock contention. Retrying might be still
 * worthwhile, but with a higher priority if allowed.
 */
static inline bool compaction_withdrawn(enum compact_result result)
{
	/*
	 * If compaction is deferred for high-order allocations, it is
	 * because sync compaction recently failed. If this is the case
	 * and the caller requested a THP allocation, we do not want
	 * to heavily disrupt the system, so we fail the allocation
	 * instead of entering direct reclaim.
	 */
	if (result == COMPACT_DEFERRED)
		return true;

	/*
	 * If compaction in async mode encounters contention or blocks higher
	 * priority task we back off early rather than cause stalls.
	 */
	if (result == COMPACT_CONTENDED)
		return true;

	/*
	 * Page scanners have met but we haven't scanned full zones so this
	 * is a back off in fact.
	 */
	if (result == COMPACT_PARTIAL_SKIPPED)
		return true;

	return false;
}


bool compaction_zonelist_suitable(struct alloc_context *ac, int order,
					int alloc_flags);

extern int kcompactd_run(int nid);
extern void kcompactd_stop(int nid);
extern void wakeup_kcompactd(pg_data_t *pgdat, int order, int highest_zoneidx);

#else

#endif /* CONFIG_COMPACTION */

struct node;
#if defined(CONFIG_COMPACTION) && defined(CONFIG_SYSFS) && defined(CONFIG_NUMA)
extern int compaction_register_node(struct node *node);
extern void compaction_unregister_node(struct node *node);

#else

#endif /* CONFIG_COMPACTION && CONFIG_SYSFS && CONFIG_NUMA */

#endif /* _LINUX_COMPACTION_H */
