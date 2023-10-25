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
 *  排队自旋锁
 *
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
           qspinlock.Val
+-------------------------------------+

               17 16
+---------------+--+-------+-+--------+
|               |  |       | |        |
+------+--------+-++-------+-+----+---+
 31    ^      18  ^ 15    9 8 7   ^  0
       |          |         ^     |
       |          +         |     +
       |       tail_idx     |  locked
       |                    |
       +                    +
    tail_cpu              pending
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

#define _Q_LOCKED_VAL		(1U << _Q_LOCKED_OFFSET)
#define _Q_PENDING_VAL		(1U << _Q_PENDING_OFFSET)

#endif /* __ASM_GENERIC_QSPINLOCK_TYPES_H */
