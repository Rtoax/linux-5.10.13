/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMERQUEUE_H
#define _LINUX_TIMERQUEUE_H

#include <linux/rbtree.h>
#include <linux/ktime.h>


struct timerqueue_node {    /* 定时器队列 节点 */
    /**
     *  is `struct hrtimer *`
     */
	struct rb_node node;    /* 红黑树节点 */
	ktime_t expires;        /* 过期时间 */
};

struct timerqueue_head {    /* 红黑树保存 */
	struct rb_root_cached rb_root;
};


extern bool timerqueue_add(struct timerqueue_head *head,
			   struct timerqueue_node *node);
extern bool timerqueue_del(struct timerqueue_head *head,
			   struct timerqueue_node *node);
extern struct timerqueue_node *timerqueue_iterate_next(
						struct timerqueue_node *node);

/**
 * timerqueue_getnext - Returns the timer with the earliest expiration time
 *
 * @head: head of timerqueue
 *
 * Returns a pointer to the timer node that has the earliest expiration time.
 */
static inline
struct timerqueue_node *timerqueue_getnext(struct timerqueue_head *head)    /* 获取下个定时器 */
{
	struct rb_node *leftmost = rb_first_cached(&head->rb_root);

	return rb_entry(leftmost, struct timerqueue_node, node);
}

static inline void timerqueue_init(struct timerqueue_node *node)
{
	RB_CLEAR_NODE(&node->node);
}

static inline bool timerqueue_node_queued(struct timerqueue_node *node)
{
	return !RB_EMPTY_NODE(&node->node);
}

static inline bool timerqueue_node_expires(struct timerqueue_node *node)
{
	return node->expires;
}

static inline void timerqueue_init_head(struct timerqueue_head *head)
{
	head->rb_root = RB_ROOT_CACHED;
}
#endif /* _LINUX_TIMERQUEUE_H */
