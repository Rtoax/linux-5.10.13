/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Queued spinlock
 *
 * (C) Copyright 2013-2015 Hewlett-Packard Development Company, L.P.
 *
 * Authors: Waiman Long <waiman.long@hp.com>
 */
#ifndef __ASM_GENERIC_QSPINLOCK_TYPES_H
#define __ASM_GENERIC_QSPINLOCK_TYPES_H

#include <linux/types.h>

/**
 * 排队自旋锁
 *
 * @locked: 描述该spinlock的持锁状态，0 unlocked， 1 locked
 * @pending: 描述该spinlock是否有pending thread（没有在mcs队列中，等待在locked字段上），
 *           1 表示有 thread 正自旋在 spinlock 上（确切的说是自旋在locked这个域），
 *           0 表示没有 pending thread。
 * @tail: 指向 Mcs node 队列的尾部节点。这个队列中的 thread 有两种状态：
 *        1. 头部的节点对应的 thread 自旋在 pending+locked 域（我们称之自旋在qspinlock上），
 *        2. 其他节点自旋在其自己的 mcs lock 上（我们称之自旋在 mcs lock 上）
 *
 * spinlock的状态由 (tail, pending, locked) 三元组来表示
 *
 * (0,0,0) 空锁
 * (0,0,1) 仅有一个 thread 持锁
 * (0,1,1) 仅有一个 thread 持锁，有一个 pending thread
 * (n,1,1) 仅有一个 thread 持锁，有一个 pending thread, MCS队列有一个节点
 * (*,1,1) 仅有一个 thread 持锁，有一个 pending thread, MCS队列有多个节点
 */
typedef struct qspinlock {
	union {
		/**
		 * `0-7`   - 上锁字节(locked byte);
		 * `8`     - 未决位(pending bit);
		 * `9-15`  - 未使用
		 * `16-17` - 这两位代表了 `MCS` 锁的 `per_cpu` 数组；
		 * `18-31` - 包括表明队列尾部的处理器数。
		 */
		atomic_t val;

		/*
		 * By using the whole 2nd least significant byte for the
		 * pending bit, we can allow better optimization of the lock
		 * acquisition for the pending bit holder.
		 */
#ifdef __LITTLE_ENDIAN
		struct {
			/**
			 *  [0-7] 标识成功持有了锁
			 */
			u8	locked;
			/**
			 *  [8] 标识第一顺位继承者，自选等待锁释放
			 */
			u8	pending;
			/**
			 *  [9-15] 未使用
			 */
		};
		struct {
			/**
			 *  [0-15] locked+pending
			 *  [9-15] 未使用
			 */
			u16	locked_pending;
			/**
			 *  [16-17] tail_idx 域，用于获取 q_nodes ，
			 *          目前支持4中上下文的 ncs_nodes
			 *              进程上下文 - task
			 *              软中断上下文 - softirq
			 *              硬中断上下文 - hardirq
			 *              不可屏蔽中断上下文 - nmi
			 *  [18-31] tail_cpu 域，用来标识等待队列末尾的 CPU
			 */
			u16	tail;
		};
#else
		struct {
			u16	tail;
			u16	locked_pending;
		};
		struct {
			u8	reserved[2];
			u8	pending;
			u8	locked;
		};
#endif
	};
} arch_spinlock_t;

/*
 * Initializier
 */
#define	__ARCH_SPIN_LOCK_UNLOCKED	{ { .val = ATOMIC_INIT(0) } }

/*
 * Bitfields in the atomic value:
 *
 * When NR_CPUS < 16K
 *  0- 7: locked byte
 *     8: pending
 *  9-15: not used
 * 16-17: tail index
 * 18-31: tail cpu (+1)
 *
 * When NR_CPUS >= 16K
 *  0- 7: locked byte
 *     8: pending
 *  9-10: tail index
 * 11-31: tail cpu (+1)
 */
#define	_Q_SET_MASK(type)	(((1U << _Q_ ## type ## _BITS) - 1)\
				      << _Q_ ## type ## _OFFSET)

/*
 *               qspinlock
 * +-------------------------------------+
 * |                val                  |
 * +-------------------------------------+
 *
 * +------------------+------------------+
 * |      tail        |  locked_pending  |
 * +------------------+------------------+
 *
 *                17 16
 * +---------------+--+-------+-+--------+
 * |               |  |       | |        |
 * +------+--------+-++-------+-+----+---+
 *  31    ^      18  ^ 15    9 8 7   ^  0
 *        |          |         ^     |
 *        |          +         |     +
 *        |       tail_idx     |  locked
 *        |                    |
 *        +                    +
 *     tail_cpu              pending
 *
 * tool: https://asciiflow.cn/
 */

/**
 * _Q_LOCKED_MASK   : 0x000000000000ff 00000000000000000000000011111111
 * _Q_PENDING_MASK  : 0x0000000000ff00 00000000000000001111111100000000
 * _Q_TAIL_IDX_MASK : 0x00000000030000 00000000000000110000000000000000
 * _Q_TAIL_CPU_MASK : 0x000000fffc0000 11111111111111000000000000000000
 * _Q_TAIL_MASK     : 0x000000ffff0000 11111111111111110000000000000000
 */

/**
 * qspinlock.locked
 */
#define _Q_LOCKED_OFFSET	0
#define _Q_LOCKED_BITS		8
#define _Q_LOCKED_MASK		_Q_SET_MASK(LOCKED)

/**
 * qspinlock.pending
 */
#define _Q_PENDING_OFFSET	(_Q_LOCKED_OFFSET + _Q_LOCKED_BITS) /*8*/
#if CONFIG_NR_CPUS < (1U << 14)
#define _Q_PENDING_BITS		8
#else
#define _Q_PENDING_BITS		1 /* 这基本上不可能 */
#endif
#define _Q_PENDING_MASK		_Q_SET_MASK(PENDING)

/**
 * qspinlock.tail
 */
#define _Q_TAIL_IDX_OFFSET	(_Q_PENDING_OFFSET/*8*/ + _Q_PENDING_BITS/*8*/)/*16*/
#define _Q_TAIL_IDX_BITS	2
#define _Q_TAIL_IDX_MASK	_Q_SET_MASK(TAIL_IDX)

#define _Q_TAIL_CPU_OFFSET	(_Q_TAIL_IDX_OFFSET/*16*/ + _Q_TAIL_IDX_BITS/*2*/)
#define _Q_TAIL_CPU_BITS	(32 - _Q_TAIL_CPU_OFFSET/*18*/)
#define _Q_TAIL_CPU_MASK	_Q_SET_MASK(TAIL_CPU)

#define _Q_TAIL_OFFSET		_Q_TAIL_IDX_OFFSET
#define _Q_TAIL_MASK		(_Q_TAIL_IDX_MASK | _Q_TAIL_CPU_MASK)

#define _Q_LOCKED_VAL/*1*/		(1U << _Q_LOCKED_OFFSET/*0*/)
#define _Q_PENDING_VAL/*1<<8*/		(1U << _Q_PENDING_OFFSET/*8*/)

#endif /* __ASM_GENERIC_QSPINLOCK_TYPES_H */
