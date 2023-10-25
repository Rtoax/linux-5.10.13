/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_OSQ_LOCK_H
#define __LINUX_OSQ_LOCK_H

/*
                 +------+            +------+         +------+
                 | CPU0 |            | CPU1 |         | CPUN |
              +--+------+--+      +--+------+--+ ...+-+------+--+
       Init:  |  locked=0  |      |  locked=0  |    | locked=0  |
              |  cpu=1     |      |  cpu=2     |    | cpu=N+1   |
      tail=0  |  prev=NULL |      |  prev=NULL |    | prev=NULL |
              |  next=NULL |      |  next=NULL |    | next=NULL |
              +------------+      +------------+    +-----------+
+---------------------------------------------------------------+   +
  CPU0         old tail=0                                           |
 osq_lock:     tail=cpu=1                                           |
+---------------------------------------------------------------+   |
  CPU1                             old tail=1                       |
                                   tail=2                           |
 osq_lock:                         prev=CPU0                        |
               next=CPU1                                            |
                                   for (;;) {                       v
                                     ##atomic load##              time
                                     if (locked==1)
                                       return true;
                                     cpu_relax();
                                     ##update prev##
                                   }
+---------------------------------------------------------------+
  CPU0         ##atomic cas##
osq_unlock:    if (tail==1) {
                 tail=0;
                 return;
               }
              +if (next) {
              |  ##atomic store##
              |  next->locked=1
              |  next=NULL
              |                    for (;;) {
              |                      ##atomic load##
              |                      if (locked==1)
              |                        return true;##CPU1 GET Lock##
              |                    }
              +} else {
                 TODO
 +---------------------------------------------------------------+
   CPU1                            ##atomic cas##
 osq_unlock:                       if (tail==2) {
                                     tail=0;
                                     return;
                                   }

tool: https://asciiflow.cn/ (see test-linux too)
*/

/*
 * An MCS like lock especially tailored for optimistic spinning for sleeping
 * lock implementations (mutex, rwsem, etc).
 *
 * MSC 锁 - Mellor-Crummey 和 Scott 的名字来命名
 *
 * optimistic_spin_node 标识本地CPU 上的节点
 */
struct optimistic_spin_node {
	/**
	 *  本地 CPU 上的节点
	 */
	struct optimistic_spin_node *next, *prev;

	/**
	 *  加锁状态
	 */
	int locked; /* 1 if lock acquired */

	/**
	 *  重新编码CPU 编号，标识该节点在哪个 CPU 上
	 */
	int cpu; /* encoded CPU # + 1 value */
};

struct optimistic_spin_queue {
	/**
	 * optimistic spinning，乐观自旋
	 *
	 * 当发现锁被持有时，optimistic spinning相信持有者很快就能把锁释放，
	 * 因此它选择自旋等待，而不是睡眠等待，这样也就能减少进程切换带来的开销了。
	 *
	 * Stores an encoded value of the CPU # of the tail node in the queue.
	 * If the queue is empty, then it's set to OSQ_UNLOCKED_VAL.
	 */
	atomic_t tail;
};

#define OSQ_UNLOCKED_VAL (0)

/* Init macro and function. */
#define OSQ_LOCK_UNLOCKED { ATOMIC_INIT(OSQ_UNLOCKED_VAL) }

/* Spinner MCS lock 乐观自旋 */
static inline void osq_lock_init(struct optimistic_spin_queue *lock)
{
	atomic_set(&lock->tail, OSQ_UNLOCKED_VAL/*0*/);
}

extern bool osq_lock(struct optimistic_spin_queue *lock);
extern void osq_unlock(struct optimistic_spin_queue *lock);

static inline bool osq_is_locked(struct optimistic_spin_queue *lock)
{
	return atomic_read(&lock->tail) != OSQ_UNLOCKED_VAL;
}

#endif
