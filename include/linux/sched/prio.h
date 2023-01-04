/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_PRIO_H
#define _LINUX_SCHED_PRIO_H

#define MAX_NICE	19
#define MIN_NICE	-20
#define NICE_WIDTH	(MAX_NICE - MIN_NICE + 1)   /* 40 */

/*
 * Priority of a process goes from 0..MAX_PRIO-1, valid RT
 * priority is 0..MAX_RT_PRIO-1, and SCHED_NORMAL/SCHED_BATCH
 * tasks are in the range MAX_RT_PRIO..MAX_PRIO-1. Priority
 * values are inverted: lower p->prio value means higher priority.
 *
 * The MAX_USER_RT_PRIO value allows the actual maximum
 * RT priority to be separate from the value exported to
 * user-space.  This allows kernel threads to set their
 * priority to a value higher than any user task. Note:
 * MAX_RT_PRIO must not be smaller than MAX_USER_RT_PRIO.
 */

#define MAX_USER_RT_PRIO	100

/**
 *  100 个优先级队列，对应实时进程优先级
 */
#define MAX_RT_PRIO	/* 100 */	MAX_USER_RT_PRIO/* 100 */

#define MAX_PRIO /* 140 */		(MAX_RT_PRIO/* 100 */ + NICE_WIDTH/* 40 */)
#define DEFAULT_PRIO /* 120 */		(MAX_RT_PRIO/* 100 */ + NICE_WIDTH/* 40 */ / 2)

/**
 *                  SCHED_FIFO                        SCHED_NORMAL
 *                  SCHED_RR                          SCHED_BATCH
 *                                                    SCHED_IDLE
 *  +-------------------------------------------+---------------------+
 *  |                  0 - 99                   |       100 - 139     |
 *  |                                           |  nice(-20 ~ 19)     |
 *  +-------------------------------------------+---------------------+
 */

/*
 * Convert user-nice values [ -20 ... 0 ... 19 ]
 * to static priority [ MAX_RT_PRIO..MAX_PRIO-1 ],
 * and back.
 */
#define NICE_TO_PRIO(nice)	((nice) + DEFAULT_PRIO/* 120 */)
#define PRIO_TO_NICE(prio)	((prio) - DEFAULT_PRIO/* 120 */)

/*
 * 'User priority' is the nice value converted to something we
 * can work with better when scaling various scheduler parameters,
 * it's a [ 0 ... 39 ] range.
 */
#define USER_PRIO(p)		((p)-MAX_RT_PRIO)
#define TASK_USER_PRIO(p)	USER_PRIO((p)->static_prio)
#define MAX_USER_PRIO		(USER_PRIO(MAX_PRIO))

/*
 * Convert nice value [19,-20] to rlimit style value [1,40].
 */
static inline long nice_to_rlimit(long nice)
{
	return (MAX_NICE - nice + 1);
}

/*
 * Convert rlimit style value [1,40] to nice value [-20, 19].
 */
static inline long rlimit_to_nice(long prio)
{
	return (MAX_NICE - prio + 1);
}

#endif /* _LINUX_SCHED_PRIO_H */
