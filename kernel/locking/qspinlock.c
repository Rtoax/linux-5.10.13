// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Queued spinlock
 *
 * (C) Copyright 2013-2015 Hewlett-Packard Development Company, L.P.
 * (C) Copyright 2013-2014,2018 Red Hat, Inc.
 * (C) Copyright 2015 Intel Corp.
 * (C) Copyright 2015 Hewlett-Packard Enterprise Development LP
 *
 * Authors: Waiman Long <longman@redhat.com>
 *          Peter Zijlstra <peterz@infradead.org>
 */

#ifndef _GEN_PV_LOCK_SLOWPATH

#include <linux/smp.h>
#include <linux/bug.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/mutex.h>
#include <linux/prefetch.h>
#include <asm/byteorder.h>
#include <asm/qspinlock.h>
#include <trace/events/lock.h>

/*
 * Include queued spinlock statistics code
 */
#include "qspinlock_stat.h"

/*
 * The basic principle of a queue-based spinlock can best be understood
 * by studying a classic queue-based spinlock implementation called the
 * MCS lock. A copy of the original MCS lock paper ("Algorithms for Scalable
 * Synchronization on Shared-Memory Multiprocessors by Mellor-Crummey and
 * Scott") is available at
 *
 * https://bugzilla.kernel.org/show_bug.cgi?id=206115
 *
 * This queued spinlock implementation is based on the MCS lock, however to
 * make it fit the 4 bytes we assume spinlock_t to be, and preserve its
 * existing API, we must modify it somehow.
 *
 * In particular; where the traditional MCS lock consists of a tail pointer
 * (8 bytes) and needs the next pointer (another 8 bytes) of its own node to
 * unlock the next pending (next->locked), we compress both these: {tail,
 * next->locked} into a single u32 value.
 *
 * Since a spinlock disables recursion of its own context and there is a limit
 * to the contexts that can nest; namely: task, softirq, hardirq, nmi. As there
 * are at most 4 nesting levels, it can be encoded by a 2-bit number. Now
 * we can encode the tail by combining the 2-bit nesting level with the cpu
 * number. With one byte for the lock value and 3 bytes for the tail, only a
 * 32-bit word is now needed. Even though we only need 1 bit for the lock,
 * we extend it to a full byte to achieve better performance for architectures
 * that support atomic byte write.
 *
 * We also change the first spinner to spin on the lock bit instead of its
 * node; whereby avoiding the need to carry a node from lock to unlock, and
 * preserving existing lock API. This also makes the unlock code simpler and
 * faster.
 *
 * N.B. The current implementation only supports architectures that allow
 *      atomic operations on smaller 8-bit and 16-bit data types.
 *
 */

#include "mcs_spinlock.h"
/**
 * 4 分别对应四个上下文：线程上下文、软中断上下文、硬中断上下文 和 NMI 上下文
 */
#define MAX_NODES	4

/*
 * On 64-bit architectures, the mcs_spinlock structure will be 16 bytes in
 * size and four of them will fit nicely in one 64-byte cacheline. For
 * pvqspinlock, however, we need more space for extra data. To accommodate
 * that, we insert two more long words to pad it up to 32 bytes. IOW, only
 * two of them can fit in a cacheline in this case. That is OK as it is rare
 * to have more than 2 levels of slowpath nesting in actual use. We don't
 * want to penalize pvqspinlocks to optimize for a rare case in native
 * qspinlocks.
 */
struct qnode {
	struct mcs_spinlock mcs;
#ifdef CONFIG_PARAVIRT_SPINLOCKS
	long reserved[2];
#endif
};

/*
 * The pending bit spinning loop count.
 * This heuristic is used to limit the number of lockword accesses
 * made by atomic_cond_read_relaxed when waiting for the lock to
 * transition out of the "== _Q_PENDING_VAL" state. We don't spin
 * indefinitely because there's no guarantee that we'll make forward
 * progress.
 */
#ifndef _Q_PENDING_LOOPS
#define _Q_PENDING_LOOPS	1
#endif

/*
 * Per-CPU queue node structures; we can never have more than 4 nested
 * contexts: task, softirq, hardirq, nmi.
 *
 * Exactly fits one 64-byte cacheline on a 64-bit architecture.
 *
 * PV doubles the storage and uses the second cacheline for PV state.
 *
 * 4 分别对应四个上下文：线程上下文、软中断上下文、硬中断上下文 和 NMI 上下文
 */
static DEFINE_PER_CPU_ALIGNED(struct qnode, qnodes[MAX_NODES]);

/*
 * We must be able to distinguish between no-tail and the tail at 0:0,
 * therefore increment the cpu number by one.
 */

static inline __pure u32 encode_tail(int cpu, int idx)
{
	u32 tail;

	/**
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
	 * _Q_TAIL_IDX_MASK: 0x00000000030000 00000000000000110000000000000000
	 * _Q_TAIL_CPU_MASK: 0x000000fffc0000 11111111111111000000000000000000
	 */
	tail  = (cpu + 1) << _Q_TAIL_CPU_OFFSET/*18*/;
	tail |= idx << _Q_TAIL_IDX_OFFSET/*16*/; /* assume < 4 */

	return tail;
}

static inline __pure struct mcs_spinlock *decode_tail(u32 tail)
{
	/**
	 * _Q_TAIL_IDX_MASK: 0x00000000030000 00000000000000110000000000000000
	 *
	 */
	int cpu = (tail >> _Q_TAIL_CPU_OFFSET/*18*/) - 1;
	int idx = (tail &  _Q_TAIL_IDX_MASK) >> _Q_TAIL_IDX_OFFSET;

	return per_cpu_ptr(&qnodes[idx].mcs, cpu);
}

static inline __pure
struct mcs_spinlock *grab_mcs_node(struct mcs_spinlock *base, int idx)
{
	return &((struct qnode *)base + idx)->mcs;
}

/**
 * _Q_LOCKED_MASK         : 0x000000000000ff 00000000000000000000000011111111
 * _Q_PENDING_MASK        : 0x0000000000ff00 00000000000000001111111100000000
 * _Q_LOCKED_PENDING_MASK : 0x0000000000ffff 00000000000000001111111111111111
 * (pending mask + locked mask)
 */
#define _Q_LOCKED_PENDING_MASK (_Q_LOCKED_MASK | _Q_PENDING_MASK)

#if _Q_PENDING_BITS == 8
/**
 * clear_pending - clear the pending bit.
 * @lock: Pointer to queued spinlock structure
 *
 * *,1,* -> *,0,*
 */
static __always_inline void clear_pending(struct qspinlock *lock)
{
	/**
	 * *,1,* -> *,0,*
	 */
	WRITE_ONCE(lock->pending, 0);
}

/**
 * clear_pending_set_locked - take ownership and clear the pending bit.
 * @lock: Pointer to queued spinlock structure
 *
 * *,1,0 -> *,0,1
 *
 * Lock stealing is not allowed if this function is used.
 */
static __always_inline void clear_pending_set_locked(struct qspinlock *lock)
{
	WRITE_ONCE(lock->locked_pending, _Q_LOCKED_VAL/*1*/);
}

/*
 * xchg_tail - Put in the new queue tail code word & retrieve previous one
 * @lock : Pointer to queued spinlock structure
 * @tail : The new queue tail code word
 * Return: The previous queue tail code word
 *
 * xchg(lock, tail), which heads an address dependency
 *
 * p,*,* -> n,*,* ; prev = xchg(lock, node)
 */
static __always_inline u32 xchg_tail(struct qspinlock *lock, u32 tail)
{
	/*
	 * We can use relaxed semantics since the caller ensures that the
	 * MCS node is properly initialized before updating the tail.
	 */
	return (u32)xchg_relaxed(&lock->tail,
				 tail >> _Q_TAIL_OFFSET) << _Q_TAIL_OFFSET;
}

#else /* _Q_PENDING_BITS == 8 */
/**
 * 以下这部分代码基本上不可能，因为，CONFIG_NR_CPUS >= (1U << 14) 才可能使用，这几乎
 * 是不可能的。
 */

/**
 * clear_pending - clear the pending bit.
 * @lock: Pointer to queued spinlock structure
 *
 * *,1,* -> *,0,*
 */
static __always_inline void clear_pending(struct qspinlock *lock)
{
	atomic_andnot(_Q_PENDING_VAL/*1<<8*/, &lock->val);
}

/**
 * clear_pending_set_locked - take ownership and clear the pending bit.
 * @lock: Pointer to queued spinlock structure
 *
 * *,1,0 -> *,0,1
 */
static __always_inline void clear_pending_set_locked(struct qspinlock *lock)
{
	atomic_add(-_Q_PENDING_VAL/*1<<8*/ + _Q_LOCKED_VAL/*1*/, &lock->val);
}

/**
 * xchg_tail - Put in the new queue tail code word & retrieve previous one
 * @lock : Pointer to queued spinlock structure
 * @tail : The new queue tail code word
 * Return: The previous queue tail code word
 *
 * xchg(lock, tail)
 *
 * p,*,* -> n,*,* ; prev = xchg(lock, node)
 */
static __always_inline u32 xchg_tail(struct qspinlock *lock, u32 tail)
{
	u32 old, new, val = atomic_read(&lock->val);

	for (;;) {
		new = (val & _Q_LOCKED_PENDING_MASK) | tail;
		/*
		 * We can use relaxed semantics since the caller ensures that
		 * the MCS node is properly initialized before updating the
		 * tail.
		 */
		old = atomic_cmpxchg_relaxed(&lock->val, val, new);
		if (old == val)
			break;

		val = old;
	}
	return old;
}
#endif /* _Q_PENDING_BITS == 8 */

/**
 * queued_fetch_set_pending_acquire - fetch the whole lock value and set pending
 * @lock : Pointer to queued spinlock structure
 * Return: The previous lock value
 *
 * *,*,* -> *,1,*
 */
#ifndef queued_fetch_set_pending_acquire
static __always_inline u32 queued_fetch_set_pending_acquire(struct qspinlock *lock)
{
	/**
	 * lock->val | _Q_PENDING_VAL: *,*,* -> *,1,*
	 */
	return atomic_fetch_or_acquire(_Q_PENDING_VAL/*1<<8*/, &lock->val);
}
#endif

/**
 * set_locked - Set the lock bit and own the lock
 * @lock: Pointer to queued spinlock structure
 *
 * *,*,0 -> *,0,1
 */
static __always_inline void set_locked(struct qspinlock *lock)
{
	WRITE_ONCE(lock->locked, _Q_LOCKED_VAL/*1*/);
}


/*
 * Generate the native code for queued_spin_unlock_slowpath(); provide NOPs for
 * all the PV callbacks.
 */

static __always_inline void __pv_init_node(struct mcs_spinlock *node) { }
static __always_inline void __pv_wait_node(struct mcs_spinlock *node,
					   struct mcs_spinlock *prev) { }
static __always_inline void __pv_kick_node(struct qspinlock *lock,
					   struct mcs_spinlock *node) { }
static __always_inline u32  __pv_wait_head_or_lock(struct qspinlock *lock,
						   struct mcs_spinlock *node)
						   { return 0; }

#define pv_enabled()		false

#define pv_init_node		__pv_init_node
#define pv_wait_node		__pv_wait_node
#define pv_kick_node		__pv_kick_node
#define pv_wait_head_or_lock	__pv_wait_head_or_lock

#ifdef CONFIG_PARAVIRT_SPINLOCKS
#define queued_spin_lock_slowpath	native_queued_spin_lock_slowpath
#endif

#endif /* _GEN_PV_LOCK_SLOWPATH */

/**
 * queued_spin_lock_slowpath - acquire the queued spinlock
 * @lock: Pointer to queued spinlock structure
 * @val: Current value of the queued spinlock 32-bit word
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
 *
 * uncontended: 无争议的
 * contended: 有争议的
 * (tail, pending, locked) 三元组
 * (queue tail, pending bit, lock value)
 *
 *              fast     :    slow                                  :    unlock
 *                       :                                          :
 * uncontended  (0,0,0) -:--> (0,0,1) ------------------------------:--> (*,*,0)
 *                       :       | ^--------.------.             /  :
 *                       :       v           \      \            |  :
 * pending               :    (0,1,1) +--> (0,1,0)   \           |  :
 *                       :       | ^--'              |           |  :
 *                       :       v                   |           |  :
 * uncontended           :    (n,x,y) +--> (n,0,0) --'           |  :
 *   queue               :       | ^--'                          |  :
 *                       :       v                               |  :
 * contended             :    (*,x,y) +--> (*,0,0) ---> (*,0,1) -'  :
 *   queue               :         ^--'                             :
 *
 * 这个名字通常因为 CONFIG_PARAVIRT_SPINLOCKS=y 被替换为
 *  native_queued_spin_lock_slowpath()
 *
 * refs:
 * 1. https://www.wowotech.net/kernel_synchronization/queued_spinlock.html
 */
void queued_spin_lock_slowpath(struct qspinlock *lock, u32 val)
{
	struct mcs_spinlock *prev, *next, *node;
	u32 old, tail;
	int idx;

	BUILD_BUG_ON(CONFIG_NR_CPUS >= (1U << _Q_TAIL_CPU_BITS));

	/**
	 * TODO
	 */
	if (pv_enabled())
		goto pv_queue;

	/**
	 * TODO
	 */
	if (virt_spin_lock(lock))
		return;

	/*
	 * Wait for in-progress pending->locked hand-overs with a bounded
	 * number of spins so that we guarantee forward progress.
	 *
	 * 慢速路径的第一段代码是处理 Pending 到 locked 的迁移。
	 * 如果当前 spinlock 的值只有 pending 比特被设定，那么说明该 spinlock 正处于
	 * owner 把锁转交给 pending owner 的过程中（即owner释放了锁，但是pending
	 * owner还没有拾取该锁）
	 *
	 * (tail, pending, locked)
	 *
	 * 0,1,0 -> 0,0,1
	 */
	if (val == _Q_PENDING_VAL/*1<<8*/) {
		int cnt = _Q_PENDING_LOOPS;
		/**
		 * 在这种情况下，我们需要重读 spinlock 的值。
		 * 当然，如果持续重读的结果仍然是仅 pending 比特被设定，那么在
		 * _Q_PENDING_LOOPS 次循环读之后放弃。
		 *
		 * for (;;) {
		 *     ## atomic load ##
		 *     if ((lock->val != _Q_PENDING_VAL) || !cnt--)
		 *         break;
		 * }
		 */
		val = atomic_cond_read_relaxed(&lock->val,
					       (VAL != _Q_PENDING_VAL) || !cnt--);
	}

	/**
	 * If we observe any contention; queue.
	 *
	 * val = (tail, pending, locked)
	 *
	 * _Q_LOCKED_MASK = 0x000000000000ff 00000000000000000000000011111111
	 *
	 * 如果有其他的线程已经自旋等待该 spinlock（pending域被设置为1）或者 挂入 MCS
	 * 队列（设置了 tail 域），那么当前线程需要挂入 MCS 等待队列。
	 * 否则，说明该线程是第一个等待持锁的，那么不需要排队，只要 pending 在自旋锁上就OK了。
	 *
	 * 即 (tail, pending, locked) tail 或 pending 域有值
	 */
	if (val & ~_Q_LOCKED_MASK)
		goto queue;

	/**
	 * #### 中速路径 ####
	 */

	/**
	 * 执行至此 tail+pending 都是0，看起来我们应该是第一个 pending 线程，通过
	 * queued_fetch_set_pending_acquire 函数读取了 spinlock 的旧值，同时
	 * 设置 pending 比特标记状态，表示自己是第一顺位继承者
	 *
	 * ----------------------------------------------------------------
	 * 设置 pending 位，展开该函数：
	 *
	 * 1. val = lock->val
	 * 2. lock->val |= _Q_PENDING_VAL, 也就是 (0,0,* -> 0,1,*)
	 *
	 * ----------------------------------------------------------------
	 * trylock || pending
	 *
	 * 0,0,* -> 0,1,* -> 0,0,1 pending, trylock
	 *
	 */
	val = queued_fetch_set_pending_acquire(lock);

	/**
	 * If we observe contention, there is a concurrent locker.
	 *
	 * Undo and queue; our setting of PENDING might have made the
	 * n,0,0 -> 0,0,0 transition fail and it will now be waiting
	 * on @next to become !NULL.
	 *
	 * _Q_LOCKED_MASK = 0x000000000000ff 00000000000000000000000011111111
	 * FYI: (tail, pending, locked)
	 *
	 * 在设置 pending 标记位之后，我们需要再次检查设置 pending 比特的时候，其他
	 * 的竞争者是否也修改了 pending 或者 tail 域。
	 *
	 * 也就是在上面设置 pending 期间，有人设置了 tail 或 pending，那么 中速路径走不
	 * 了，清理上面设置的 pending，表示自己不是第一顺位继承者
	 */
	if (unlikely(val & ~_Q_LOCKED_MASK)) {

		/**
		 * 如果其他线程已经抢先修改，那么本线程不能再 pending 在自旋锁上了，
		 * 而是需要回退 pending 设置（如果需要的话），并且挂入自旋等待队列。
		 *
		 * _Q_PENDING_MASK : 0x0000000000ff00
		 *
		 * TODO: 为什么 val 没有设置 pending 时候，才清理 lock pending？
		 */
		/* Undo PENDING if we set it. */
		if (!(val & _Q_PENDING_MASK))
			clear_pending(lock);

		/**
		 * 去插队
		 */
		goto queue;
	}

	/**
	 * 如果没有其他线程插入，那么当前线程可以开始自旋在 spinlock 上，等待 owner
	 * 释放锁了（我们称这种状态的线程被称为 pending owner）
	 */

	/**
	 * We're pending, wait for the owner to go away.
	 *
	 * 0,1,1 -> 0,1,0
	 *
	 * commit 4282494a20cd ("locking/qspinlock: Micro-optimize pending
	 * state waitingfor unlock") 中进行了如下修改
	 *
	 * - 0,1,1 -> 0,1,0
	 * + 0,1,1 -> *,1,0
	 *
	 * this wait loop must be a load-acquire such that we match the
	 * store-release that clears the locked bit and create lock
	 * sequentiality; this is because not all
	 * clear_pending_set_locked() implementations imply full
	 * barriers.
	 *
	 * FYI: _Q_LOCKED_MASK = 0x000000000000ff 00000000000000000000000011111111
	 * FYI: (tail, pending, locked)
	 *
	 * 至此，我们已经成为合法的 pending owner，距离获取 spinlock 仅一步之遥，属于
	 * 是一人之下，万人之上（对比 pending 在 mcs lock 的线程而言）
	 */
	if (val & _Q_LOCKED_MASK)
		/**
		 * ########## qspinlock 自旋在此 ##########
		 *
		 * pending owner 通过 atomic_cond_read_acquire 函数自旋在 spinlock
		 * 的 locked 域，直到 owner 释放 spinlock。
		 *
		 * 这里自旋并不是轮询，而是通过 WFE 指令让 CPU 停下来，降低功耗。当 owner
		 * 释放 spinlock 的时候会发送事件唤醒该CPU。
		 *
		 * FYI: _Q_LOCKED_MASK = 0x000000000000ff
		 *
		 *  for (;;) {
		 *    // atomic-load lock->val
		 *    if (!(lock->val & _Q_LOCKED_MASK))
		 *       break;
		 *  }
		 *
		 * queued_spin_unlock() 中将 lock->locked 置 0
		 */
/**
 * commit 4282494a20cd ("locking/qspinlock: Micro-optimize pending state waiting
 * for unlock") 中进行了如下修改
 */
#if 0
		atomic_cond_read_acquire(&lock->val, !(VAL & _Q_LOCKED_MASK));
#elif 4282494a20cd /* commit 4282494a20cd */
		smp_cond_load_acquire(&lock->locked, !VAL);
#endif

	/**
	 * 发现 owner 已经释放了锁，那么 pending owner 解除自旋状态继续前行。清除
	 * pending 标记，同时设定 locked 标记，持锁成功，进入临界区。
	 *
	 * take ownership and clear the pending bit.
	 *
	 * 0,1,0 -> 0,0,1
	 */
	clear_pending_set_locked(lock);
	lockevent_inc(lock_pending);

	/**
	 * #### 中速路径持锁成功 ####
	 */
	return;

	/*
	 * End of pending bit optimistic spinning and beginning of MCS
	 * queuing.
	 */
queue:
	lockevent_inc(lock_slowpath);
pv_queue:
	/**
	 * 获取 当前 CPU 的 struct mcs_spinlock 结构
	 *
	 * 当不能 pending 在 spinlock 的时候，当前执行线索需要挂入 MCS 队列。
	 *
	 */
	node = this_cpu_ptr(&qnodes[0].mcs);

	/**
	 * 由于 spin_lock 可能会嵌套（在不同的自旋锁上嵌套，如果同一个那么就是死锁了）
	 * 因此我们构建了多个 mcs node，每次递进一层。顺便一提的是：当 index 大于阀值
	 * 的时候，会取消 qspinlock 机制，恢复原始自旋机制。
	 */
	idx = node->count++;

	/**
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
	 * 将 context index 和 cpu id 组合成 tail
	 *
	 * FYI: tail = tail_cpu & tail_idx;
	 */
	tail = encode_tail(smp_processor_id(), idx);

	/**
	 * commit ee042be16cb4("locking: Apply contention tracepoints in the
	 * slow path") v5.18-rc1-9-gee042be16cb4
	 */
	trace_contention_begin(lock, LCB_F_SPIN);

	/*
	 * 4 nodes are allocated based on the assumption that there will
	 * not be nested NMIs taking spinlocks. That may not be true in
	 * some architectures even though the chance of needing more than
	 * 4 nodes will still be extremely unlikely. When that happens,
	 * we fall back to spinning on the lock directly without using
	 * any MCS node. This is not the most elegant solution, but is
	 * simple enough.
	 *
	 * 4 个节点是根据假设不会有嵌套的 NMI 采用自旋锁来分配的。在某些架构中，这
	 * 可能并非如此，即使需要超过 4 个节点的可能性仍然极小。发生这种情况时，我们
	 * 回退到直接在锁上旋转，而不使用任何 MCS 节点。这不是最优雅的解决方案，但足
	 * 够简单。
	 *
	 * 当 index 大于阀值的时候，会取消 qspinlock 机制，恢复原始自旋机制。
	 */
	if (unlikely(idx >= MAX_NODES)) {
		lockevent_inc(lock_no_node);
		while (!queued_spin_trylock(lock))
			cpu_relax();
		goto release;
	}

	/**
	 * 根据 mcs node 基地址和 index 找到对应的 mcs node
	 */
	node = grab_mcs_node(node, idx);

	/*
	 * Keep counts of non-zero index values:
	 */
	lockevent_cond_inc(lock_use_node2 + idx - 1, idx);

	/*
	 * Ensure that we increment the head node->count before initialising
	 * the actual node. If the compiler is kind enough to reorder these
	 * stores, then an IRQ could overwrite our assignments.
	 */
	barrier();

	/**
	 * 找到 mcs node 之后，我们需要挂入队列
	 */

	/**
	 * 初始化 MCS lock 为未持锁状态（指mcs锁，注意和spinlock区分开），考虑到我们
	 * 是尾部节点，next 设置为 NULL
	 */
	node->locked = 0;
	node->next = NULL;
	pv_init_node(node);

	/*
	 * We touched a (possibly) cold cacheline in the per-cpu queue node;
	 * attempt the trylock once more in the hope someone let go while we
	 * weren't watching.
	 *
	 * 试图获取锁，很可能在上面的过程中，pending thread 和 owner thread 都已经
	 * 离开了临界区，这时候如果持锁成功，那么就可以长驱直入，进入临界区，无需排队。
	 */
	if (queued_spin_trylock(lock))
		goto release;

	/*
	 * Ensure that the initialisation of @node is complete before we
	 * publish the updated tail via xchg_tail() and potentially link
	 * @node into the waitqueue via WRITE_ONCE(prev->next, node) below.
	 */
	smp_wmb();

	/**
	 * Publish the updated tail.
	 * We have already touched the queueing cacheline; don't bother with
	 * pending stuff.
	 *
	 * p,*,* -> n,*,*
	 *
	 * 修改 qspinlock 的 tail 域，old 保存了旧值
	 *
	 * FYI: (tail, pending, locked)
	 * FYI: tail = tail_cpu & tail_idx;
	 * FYI: _Q_TAIL_MASK : 0x000000ffff0000 11111111111111110000000000000000
	 */
	old = xchg_tail(lock, tail);
	next = NULL;

	/*
	 * if there was a previous node; link it and wait until reaching the
	 * head of the waitqueue.
	 *
	 * 如果在本节点挂入队列之前，等待队列中已经有了 waiter，那么我们需要把 tail 指向
	 * 的尾部节点和旧的 MCS 队列串联起来。
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
	 * _Q_TAIL_MASK     : 0x000000ffff0000 11111111111111110000000000000000
	 */
	if (old & _Q_TAIL_MASK) {
		/**
		 * 获取 mcs_spinlock 结构
		 */
		prev = decode_tail(old);

		/**
		 * Link @node into the waitqueue.
		 *
		 * 建立新 node 和旧的等待队列的关系。
		 */
		WRITE_ONCE(prev->next, node);

		pv_wait_node(node, prev);

		/**
		 * ########### 自旋 ###########
		 *
		 * 至此，我们已经是处于 mcs queue 的队列尾部，自旋在自己的 mcs lock上，
		 * 等待 locked 状态（是 mcs lock，不是 spinlock 的）变成1。
		 *
		 *  for (;;) {
		 *    // atomic-load node->locked
		 *    if (node->locked)
		 *      break;
		 *  }
		 */
		arch_mcs_spin_lock_contended(&node->locked);

		/*
		 * While waiting for the MCS lock, the next pointer may have
		 * been set by another lock waiter. We optimistically load
		 * the next pointer & prefetch the cacheline for writing
		 * to reduce latency in the upcoming MCS unlock operation.
		 *
		 * 执行至此，我们已经获得了 MCS lock，也就是说我们成为了队首。在我们自
		 * 旋等待的时候，可能其他的竞争者也加入到链表了，next 不再是 null 了
		 * （即我们不再是队尾了）。因此这里需要更新 next 变量，以便我们把 mcs
		 * 锁禅让给下一个 node。
		 */
		next = READ_ONCE(node->next);
		if (next)
			prefetchw(next);
	}

	/*
	 * we're at the head of the waitqueue, wait for the owner & pending to
	 * go away.
	 *
	 * *,x,y -> *,0,0
	 *
	 * this wait loop must use a load-acquire such that we match the
	 * store-release that clears the locked bit and create lock
	 * sequentiality; this is because the set_locked() function below
	 * does not imply a full barrier.
	 *
	 * The PV pv_wait_head_or_lock function, if active, will acquire
	 * the lock and return a non-zero value. So we have to skip the
	 * atomic_cond_read_acquire() call. As the next PV queue head hasn't
	 * been designated yet, there is no way for the locked value to become
	 * _Q_SLOW_VAL. So both the set_locked() and the
	 * atomic_cmpxchg_relaxed() calls will be safe.
	 *
	 * If PV isn't active, 0 will be returned instead.
	 *
	 */
	if ((val = pv_wait_head_or_lock(lock, node)))
		goto locked;

	/**
	 * 在获取了 MCS lock 之后（排到了 mcs node queue 的头部），我们获准了在
	 * spinlock 上自旋。这里等待 pending 和 owner 离开临界区。
	 *
	 * for (;;) {
	 *     // atomic-load lock->val
	 *     if (!(VAL & _Q_LOCKED_PENDING_MASK))
	 *         break;
	 * }
	 *
	 * _Q_LOCKED_PENDING_MASK : 0x0000000000ffff (pending + locked)
	 *                          00000000000000001111111111111111
	 *
	 * (tail, pending, locked) = (?, 0, 0)
	 */
	val = atomic_cond_read_acquire(&lock->val, !(VAL & _Q_LOCKED_PENDING_MASK));

locked:
	/*
	 * claim the lock:
	 *
	 * n,0,0 -> 0,0,1 : lock, uncontended
	 * *,*,0 -> *,*,1 : lock, contended
	 *
	 * If the queue head is the only one in the queue (lock value == tail)
	 * and nobody is pending, clear the tail code and grab the lock.
	 * Otherwise, we only need to grab the lock.
	 */

	/*
	 * In the PV case we might already have _Q_LOCKED_VAL set, because
	 * of lock stealing; therefore we must also allow:
	 *
	 * n,0,1 -> 0,0,1
	 *
	 * Note: at this point: (val & _Q_PENDING_MASK) == 0, because of the
	 *       above wait condition, therefore any concurrent setting of
	 *       PENDING will make the uncontended transition fail.
	 *
	 * 如果本 mcs node 是队列中的最后一个节点
	 */
	if ((val & _Q_TAIL_MASK) == tail) {
		/**
		 * 如果本 mcs node 是队列中的最后一个节点，我们不需要处理 mcs lock
		 * 传递，直接试图持锁
		 */
		if (atomic_try_cmpxchg_relaxed(&lock->val, &val, _Q_LOCKED_VAL/*1*/))
			/**
			 * 如果成功，完成持锁，进入临界区。
			 */
			goto release; /* No contention */
	}

	/*
	 * Either somebody is queued behind us or _Q_PENDING_VAL got set
	 * which will then detect the remaining tail and queue behind us
	 * ensuring we'll see a @next.
	 *
	 * 如果本 mcs node 不是队列尾部，那么不需要考虑竞争，直接持 spinlock
	 */
	set_locked(lock);

	/**
	 * contended path; wait for next if not observed yet, release.
	 *
	 * 如果 next 为空，说明不存在下一个节点。不过也许在我们等自旋锁的时候，新的节点
	 * 又挂入了，所以这里重新读一下 next 节点。
	 */
	if (!next)
		next = smp_cond_load_relaxed(&node->next, (VAL));

	/**
	 * 把 mcs lock 传递给下一个节点，让其自旋在 spinlock 上。
	 */
	arch_mcs_spin_unlock_contended(&next->locked);
	pv_kick_node(lock, next);

release:
	/**
	 * commit ee042be16cb4("locking: Apply contention tracepoints in the
	 * slow path") v5.18-rc1-9-gee042be16cb4
	 */
	trace_contention_end(lock, 0);

	/*
	 * release the node
	 */
	__this_cpu_dec(qnodes[0].mcs.count);
}
EXPORT_SYMBOL(queued_spin_lock_slowpath);

/*
 * Generate the paravirt code for queued_spin_unlock_slowpath().
 */
#if !defined(_GEN_PV_LOCK_SLOWPATH) && defined(CONFIG_PARAVIRT_SPINLOCKS)
#define _GEN_PV_LOCK_SLOWPATH

#undef  pv_enabled
#define pv_enabled()	true

#undef pv_init_node
#undef pv_wait_node
#undef pv_kick_node
#undef pv_wait_head_or_lock

#undef  queued_spin_lock_slowpath
#define queued_spin_lock_slowpath	__pv_queued_spin_lock_slowpath

#include "qspinlock_paravirt.h"
#include "qspinlock.c"

bool __initdata nopvspin ;
static __init int parse_nopvspin(char *arg)
{
	nopvspin = true;
	return 0;
}
early_param("nopvspin", parse_nopvspin);
#endif
