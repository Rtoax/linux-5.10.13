/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  pm_wakeup.h - Power management wakeup interface
 *
 *  Copyright (C) 2008 Alan Stern
 *  Copyright (C) 2010 Rafael J. Wysocki, Novell Inc.
 */

#ifndef _LINUX_PM_WAKEUP_H
#define _LINUX_PM_WAKEUP_H

#ifndef _DEVICE_H_
# error "please don't include this file directly"
#endif

#include <linux/types.h>

struct wake_irq;

/**
 * struct wakeup_source - Representation of wakeup sources
 *
 * @name: Name of the wakeup source
 * @id: Wakeup source id
 * @entry: Wakeup source list entry
 * @lock: Wakeup source lock
 * @wakeirq: Optional device specific wakeirq
 * @timer: Wakeup timer list
 * @timer_expires: Wakeup timer expiration期满
 * @total_time: Total time this wakeup source has been active.
 * @max_time: Maximum time this wakeup source has been continuously active.
 * @last_time: Monotonic clock when the wakeup source's was touched last time.
 * @prevent_sleep_time: Total time this source has been preventing autosleep.
 * @event_count: Number of signaled wakeup events.
 * @active_count: Number of times the wakeup source was activated.
 * @relax_count: Number of times the wakeup source was deactivated.
 * @expire_count: Number of times the wakeup source's timeout has expired.
 * @wakeup_count: Number of times the wakeup source might abort suspend.
 * @dev: Struct device for sysfs statistics about the wakeup source.
 * @active: Status of the wakeup source.
 * @autosleep_enabled: Autosleep is active, so update @prevent_sleep_time.
 */
struct wakeup_source {
	const char 		*name;  /* 唤醒源名称 */
	int			id;         /* 唤醒源ID */
	struct list_head	entry;
	spinlock_t		lock;
	struct wake_irq		*wakeirq;/* 可选的设备 wakeirq */
	struct timer_list	timer;  /* 定时器链表 hlist_node ， 包含定时器回调函数 */
	unsigned long		timer_expires;/* 期满 */
	ktime_t total_time;     /* 纳秒级的时间 */
	ktime_t max_time;
	ktime_t last_time;
	ktime_t start_prevent_time;
	ktime_t prevent_sleep_time; /* 阻止时间 */
	unsigned long		event_count;
	unsigned long		active_count;
	unsigned long		relax_count;
	unsigned long		expire_count;
	unsigned long		wakeup_count;
	struct device		*dev;
	bool			active:1;
	bool			autosleep_enabled:1;
};

#define for_each_wakeup_source(ws) \
	for ((ws) = wakeup_sources_walk_start();	\
	     (ws);					\
	     (ws) = wakeup_sources_walk_next((ws)))

#ifdef CONFIG_PM_SLEEP

/*
 * Changes to device_may_wakeup take effect on the next pm state change.
 */

static inline bool device_can_wakeup(struct device *dev)
{
	return dev->power.can_wakeup;
}

static inline bool device_may_wakeup(struct device *dev)
{
	return dev->power.can_wakeup && !!dev->power.wakeup;
}

static inline void device_set_wakeup_path(struct device *dev)
{
	dev->power.wakeup_path = true;
}

/* drivers/base/power/wakeup.c */
extern struct wakeup_source *wakeup_source_create(const char *name);
extern void wakeup_source_destroy(struct wakeup_source *ws);
extern void wakeup_source_add(struct wakeup_source *ws);
extern void wakeup_source_remove(struct wakeup_source *ws);
extern struct wakeup_source *wakeup_source_register(struct device *dev,
						    const char *name);
extern void wakeup_source_unregister(struct wakeup_source *ws);
extern int wakeup_sources_read_lock(void);
extern void wakeup_sources_read_unlock(int idx);
extern struct wakeup_source *wakeup_sources_walk_start(void);
extern struct wakeup_source *wakeup_sources_walk_next(struct wakeup_source *ws);
extern int device_wakeup_enable(struct device *dev);
extern int device_wakeup_disable(struct device *dev);
extern void device_set_wakeup_capable(struct device *dev, bool capable);
extern int device_init_wakeup(struct device *dev, bool val);
extern int device_set_wakeup_enable(struct device *dev, bool enable);
extern void __pm_stay_awake(struct wakeup_source *ws);
extern void pm_stay_awake(struct device *dev);
extern void __pm_relax(struct wakeup_source *ws);
extern void pm_relax(struct device *dev);
extern void pm_wakeup_ws_event(struct wakeup_source *ws, unsigned int msec, bool hard);
extern void pm_wakeup_dev_event(struct device *dev, unsigned int msec, bool hard);

#else /* !CONFIG_PM_SLEEP */

#endif /* !CONFIG_PM_SLEEP */

static inline void __pm_wakeup_event(struct wakeup_source *ws, unsigned int msec)
{
	return pm_wakeup_ws_event(ws, msec, false);
}

static inline void pm_wakeup_event(struct device *dev, unsigned int msec)
{
	return pm_wakeup_dev_event(dev, msec, false);
}

static inline void pm_wakeup_hard_event(struct device *dev)
{
	return pm_wakeup_dev_event(dev, 0, true);
}

#endif /* _LINUX_PM_WAKEUP_H */
