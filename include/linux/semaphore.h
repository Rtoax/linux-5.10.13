/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2008 Intel Corporation
 * Author: Matthew Wilcox <willy@linux.intel.com>
 *
 * Please see kernel/locking/semaphore.c for documentation of these functions
 */
#ifndef __LINUX_SEMAPHORE_H
#define __LINUX_SEMAPHORE_H

#include <linux/list.h>
#include <linux/spinlock.h>

/* Please don't access any members of this structure directly */
/**
 *  信号量
 */
struct semaphore {
	raw_spinlock_t		lock;   //保护 `信号量` 的 `自旋锁`
	/**
     *  允许进入临界区的内核执行路径的个数
     */
	unsigned int		count;  //现有资源的数量

    /**
     *  用于管理所有在该信号量上睡眠的进程
     *  没有成功获取锁的进程会在这个链表上睡眠
     *
     *  添加到链表节点 见 `__down_common()`
     */
	struct list_head	wait_list;  //等待获取此锁的进程序列
};

#define __SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.lock		= __RAW_SPIN_LOCK_UNLOCKED((name).lock),	\
	.count		= n,						\
	.wait_list	= LIST_HEAD_INIT((name).wait_list),		\
}

#define DEFINE_SEMAPHORE(name)	\
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)

/**
 *  信号量初始化
 */
static inline void sema_init(struct semaphore *sem, int val)
{
	static struct lock_class_key __key;
    /**
     *  
     */
	*sem = (struct semaphore) __SEMAPHORE_INITIALIZER(*sem, val);
	lockdep_init_map(&sem->lock.dep_map, "semaphore->lock", &__key, 0);
}

/**
 *  P
 */
extern void down(struct semaphore *sem);
extern int __must_check down_interruptible(struct semaphore *sem);
extern int __must_check down_killable(struct semaphore *sem);
extern int __must_check down_trylock(struct semaphore *sem);
extern int __must_check down_timeout(struct semaphore *sem, long jiffies);

/**
 *  V
 */
extern void up(struct semaphore *sem);

#endif /* __LINUX_SEMAPHORE_H */
