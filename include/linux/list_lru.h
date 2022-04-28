/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2013 Red Hat, Inc. and Parallels Inc. All rights reserved.
 * Authors: David Chinner and Glauber Costa
 *
 * Generic LRU infrastructure
 */
#ifndef _LRU_LIST_H
#define _LRU_LIST_H

#include <linux/list.h>
#include <linux/nodemask.h>
#include <linux/shrinker.h>

struct mem_cgroup;

/* list_lru_walk_cb has to always return one of those */
enum lru_status {
	/**
	 * item removed from list
	 */
	LRU_REMOVED,
	/**
	 * item removed, but lock has been dropped and reacquired
	 */
	LRU_REMOVED_RETRY,
	/* item referenced, give another pass */
	LRU_ROTATE,
	/* item cannot be locked, skip */
	LRU_SKIP,
	/**
	 * item not freeable. May drop the lock
	 * internally, but has to return locked.
	 */
	LRU_RETRY,
};

/**
 * @brief LRU 链表节点
 *
 */
struct list_lru_one {
	/**
	 * @brief LRU 链表头
	 *
	 * 1. dcache 中 对应 struct dentry.d_lru 链表头, 见：
	 * 	* __list_lru_walk_one(), dentry_lru_isolate()
	 *
	 */
	struct list_head	list;
	/* may become negative during memcg reparenting */
	long			nr_items;
};

struct list_lru_memcg {
	struct rcu_head		rcu;
	/* array of per cgroup lists, indexed by memcg_cache_id */
	struct list_lru_one	*lru[];
};

/**
 * @brief LRU 链表节点
 *
 */
struct list_lru_node {
	/**
	 * protects all lists on the node, including per cgroup
	 * 保护 list_lru_node.lru, 见 list_lru_add()
	 */
	spinlock_t		lock;
	/**
	 * global list, used for the root cgroup in cgroup aware lrus
	 */
	struct list_lru_one	lru;

#ifdef CONFIG_MEMCG_KMEM
	/* for cgroup aware lrus points to per cgroup lists, otherwise NULL */
	struct list_lru_memcg	__rcu *memcg_lrus;
#endif
	long nr_items;
} ____cacheline_aligned_in_smp;

/**
 * @brief Long time rarely use
 *
 * https://www.jianshu.com/p/1f8e36285539
 * -------------------------------------------------------------------
 * 用一个双向链表记录访问时间，因为链表插入删除高效，时间新的在前面，旧的在后面。
 * 用一个哈希表记录缓存(key, value)，哈希查找近似O(1)，发生哈希冲突时最坏O(n)，
 * 同时哈希表中得记录 (key, (value, key_ptr))，key_ptr 是key在链表中的地址，
 * 为了能在O(1)时间内找到该节点，并把节点提升到表头。
 * 链表中的key，能快速找到hash中的value，并删除。
 */
struct list_lru {
	/**
	 * 这是一个节点 NUMA 个数的数组，见 list_lru_add()
	 *
	 */
	struct list_lru_node	*node;

#ifdef CONFIG_MEMCG_KMEM
	struct list_head	list;
	int			shrinker_id;
	bool			memcg_aware;
#endif
};

void list_lru_destroy(struct list_lru *lru);
int __list_lru_init(struct list_lru *lru, bool memcg_aware,
		    struct lock_class_key *key, struct shrinker *shrinker);

#define list_lru_init(lru)				\
	__list_lru_init((lru), false, NULL, NULL)
#define list_lru_init_key(lru, key)			\
	__list_lru_init((lru), false, (key), NULL)
#define list_lru_init_memcg(lru, shrinker)		\
	__list_lru_init((lru), true, NULL, shrinker)

int memcg_update_all_list_lrus(int num_memcgs);
void memcg_drain_all_list_lrus(int src_idx, struct mem_cgroup *dst_memcg);

/**
 * list_lru_add: add an element to the lru list's tail
 * @list_lru: the lru pointer
 * @item: the item to be added.
 *
 * If the element is already part of a list, this function returns doing
 * nothing. Therefore the caller does not need to keep state about whether or
 * not the element already belongs in the list and is allowed to lazy update
 * it. Note however that this is valid for *a* list, not *this* list. If
 * the caller organize itself in a way that elements can be in more than
 * one type of list, it is up to the caller to fully remove the item from
 * the previous list (with list_lru_del() for instance) before moving it
 * to @list_lru
 *
 * Return value: true if the list was updated, false otherwise
 */
bool list_lru_add(struct list_lru *lru, struct list_head *item);

/**
 * list_lru_del: delete an element to the lru list
 * @list_lru: the lru pointer
 * @item: the item to be deleted.
 *
 * This function works analogously as list_lru_add in terms of list
 * manipulation. The comments about an element already pertaining to
 * a list are also valid for list_lru_del.
 *
 * Return value: true if the list was updated, false otherwise
 */
bool list_lru_del(struct list_lru *lru, struct list_head *item);

/**
 * list_lru_count_one: return the number of objects currently held by @lru
 * @lru: the lru pointer.
 * @nid: the node id to count from.
 * @memcg: the cgroup to count from.
 *
 * Always return a non-negative number, 0 for empty lists. There is no
 * guarantee that the list is not updated while the count is being computed.
 * Callers that want such a guarantee need to provide an outer lock.
 */
unsigned long list_lru_count_one(struct list_lru *lru,
				 int nid, struct mem_cgroup *memcg);
unsigned long list_lru_count_node(struct list_lru *lru, int nid);

static inline unsigned long list_lru_shrink_count(struct list_lru *lru,
						  struct shrink_control *sc)
{
	return list_lru_count_one(lru, sc->nid, sc->memcg);
}

static inline unsigned long list_lru_count(struct list_lru *lru)
{
	long count = 0;
	int nid;

	for_each_node_state(nid, N_NORMAL_MEMORY)
		count += list_lru_count_node(lru, nid);

	return count;
}

void list_lru_isolate(struct list_lru_one *list, struct list_head *item);
void list_lru_isolate_move(struct list_lru_one *list, struct list_head *item,
			   struct list_head *head);

typedef enum lru_status (*list_lru_walk_cb)(struct list_head *item,
		struct list_lru_one *list, spinlock_t *lock, void *cb_arg);

/**
 * list_lru_walk_one: walk a list_lru, isolating and disposing freeable items.
 * @lru: the lru pointer.
 * @nid: the node id to scan from.
 * @memcg: the cgroup to scan from.
 * @isolate: callback function that is resposible for deciding what to do with
 *  the item currently being scanned
 * @cb_arg: opaque type that will be passed to @isolate
 * @nr_to_walk: how many items to scan.
 *
 * This function will scan all elements in a particular list_lru, calling the
 * @isolate callback for each of those items, along with the current list
 * spinlock and a caller-provided opaque. The @isolate callback can choose to
 * drop the lock internally, but *must* return with the lock held. The callback
 * will return an enum lru_status telling the list_lru infrastructure what to
 * do with the object being scanned.
 *
 * Please note that nr_to_walk does not mean how many objects will be freed,
 * just how many objects will be scanned.
 *
 * Return value: the number of objects effectively removed from the LRU.
 */
unsigned long list_lru_walk_one(struct list_lru *lru,
				int nid, struct mem_cgroup *memcg,
				list_lru_walk_cb isolate, void *cb_arg,
				unsigned long *nr_to_walk);
/**
 * list_lru_walk_one_irq: walk a list_lru, isolating and disposing freeable items.
 * @lru: the lru pointer.
 * @nid: the node id to scan from.
 * @memcg: the cgroup to scan from.
 * @isolate: callback function that is resposible for deciding what to do with
 *  the item currently being scanned
 * @cb_arg: opaque type that will be passed to @isolate
 * @nr_to_walk: how many items to scan.
 *
 * Same as @list_lru_walk_one except that the spinlock is acquired with
 * spin_lock_irq().
 */
unsigned long list_lru_walk_one_irq(struct list_lru *lru,
				    int nid, struct mem_cgroup *memcg,
				    list_lru_walk_cb isolate, void *cb_arg,
				    unsigned long *nr_to_walk);
unsigned long list_lru_walk_node(struct list_lru *lru, int nid,
				 list_lru_walk_cb isolate, void *cb_arg,
				 unsigned long *nr_to_walk);

static inline unsigned long
list_lru_shrink_walk(struct list_lru *lru, struct shrink_control *sc,
		     list_lru_walk_cb isolate, void *cb_arg)
{
	return list_lru_walk_one(lru, sc->nid, sc->memcg, isolate, cb_arg,
				 &sc->nr_to_scan);
}

static inline unsigned long
list_lru_shrink_walk_irq(struct list_lru *lru, struct shrink_control *sc,
			 list_lru_walk_cb isolate, void *cb_arg)
{
	return list_lru_walk_one_irq(lru, sc->nid, sc->memcg, isolate, cb_arg,
				     &sc->nr_to_scan);
}

/**
 * @brief 遍历所有节点的 walk
 *
 * @param lru
 * @param isolate
 * @param cb_arg
 * @param nr_to_walk
 * @return unsigned long
 */
static inline unsigned long
list_lru_walk(struct list_lru *lru, list_lru_walk_cb isolate,
	      void *cb_arg, unsigned long nr_to_walk)
{
	long isolated = 0;
	int nid;

	/**
	 * @brief 遍历所有节点
	 *
	 */
	for_each_node_state(nid, N_NORMAL_MEMORY) {
		isolated += list_lru_walk_node(lru, nid, isolate,
					       cb_arg, &nr_to_walk);
		if (nr_to_walk <= 0)
			break;
	}
	return isolated;
}
#endif /* _LRU_LIST_H */
