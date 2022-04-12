/* SPDX-License-Identifier: GPL-2.0 */
/*
 * tick internal variable and functions used by low/high res code
 */
#include <linux/hrtimer.h>
#include <linux/tick.h>

#include "timekeeping.h"
#include "tick-sched.h"

#ifdef CONFIG_GENERIC_CLOCKEVENTS

# define TICK_DO_TIMER_NONE	-1
# define TICK_DO_TIMER_BOOT	-2

DECLARE_PER_CPU(struct tick_device, tick_cpu_device);
extern ktime_t tick_next_period;
extern ktime_t tick_period;
extern int __read_mostly tick_do_timer_cpu ;

extern void tick_setup_periodic(struct clock_event_device *dev, int broadcast);
extern void tick_handle_periodic(struct clock_event_device *dev);
extern void tick_check_new_device(struct clock_event_device *dev);
extern void tick_shutdown(unsigned int cpu);
extern void tick_suspend(void);
extern void tick_resume(void);
extern bool tick_check_replacement(struct clock_event_device *curdev,
				   struct clock_event_device *newdev);
extern void tick_install_replacement(struct clock_event_device *dev);
extern int tick_is_oneshot_available(void);
extern struct tick_device *tick_get_device(int cpu);

extern int clockevents_tick_resume(struct clock_event_device *dev);
/* Check, if the device is functional or a dummy for broadcast */
static inline int tick_device_is_functional(struct clock_event_device *dev)
{
	return !(dev->features & CLOCK_EVT_FEAT_DUMMY);
}

static inline enum clock_event_state clockevent_get_state(struct clock_event_device *dev)
{
	return dev->state_use_accessors;
}

static inline void clockevent_set_state(struct clock_event_device *dev,
					enum clock_event_state state)
{
	dev->state_use_accessors = state;
}

extern void clockevents_shutdown(struct clock_event_device *dev);
extern void clockevents_exchange_device(struct clock_event_device *old,
					struct clock_event_device *new);
extern void clockevents_switch_state(struct clock_event_device *dev,
				     enum clock_event_state state);
extern int clockevents_program_event(struct clock_event_device *dev,
				     ktime_t expires, bool force);
extern void clockevents_handle_noop(struct clock_event_device *dev);
extern int __clockevents_update_freq(struct clock_event_device *dev, u32 freq);
extern ssize_t sysfs_get_uname(const char *buf, char *dst, size_t cnt);

/* Broadcasting support */
# ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
extern int tick_device_uses_broadcast(struct clock_event_device *dev, int cpu);
extern void tick_install_broadcast_device(struct clock_event_device *dev);
extern int tick_is_broadcast_device(struct clock_event_device *dev);
extern void tick_suspend_broadcast(void);
extern void tick_resume_broadcast(void);
extern bool tick_resume_check_broadcast(void);
extern void tick_broadcast_init(void);
extern void tick_set_periodic_handler(struct clock_event_device *dev, int broadcast);
extern int tick_broadcast_update_freq(struct clock_event_device *dev, u32 freq);
extern struct tick_device *tick_get_broadcast_device(void);
extern struct cpumask *tick_get_broadcast_mask(void);
# else /* !CONFIG_GENERIC_CLOCKEVENTS_BROADCAST: */

# endif /* !CONFIG_GENERIC_CLOCKEVENTS_BROADCAST */

#else /* !GENERIC_CLOCKEVENTS: */

#endif /* !GENERIC_CLOCKEVENTS */

/* Oneshot related functions */
#ifdef CONFIG_TICK_ONESHOT
extern void tick_setup_oneshot(struct clock_event_device *newdev,
			       void (*handler)(struct clock_event_device *),
			       ktime_t nextevt);
extern int tick_program_event(ktime_t expires, int force);
extern void tick_oneshot_notify(void);
extern int tick_switch_to_oneshot(void (*handler)(struct clock_event_device *));
extern void tick_resume_oneshot(void);
static inline bool tick_oneshot_possible(void) { return true; }
extern int tick_oneshot_mode_active(void);
extern void tick_clock_notify(void);
extern int tick_check_oneshot_change(int allow_nohz);
extern int tick_init_highres(void);
#else /* !CONFIG_TICK_ONESHOT: */

#endif /* !CONFIG_TICK_ONESHOT */

/* Functions related to oneshot broadcasting */
#if defined(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST) && defined(CONFIG_TICK_ONESHOT)
extern void tick_broadcast_switch_to_oneshot(void);
extern int tick_broadcast_oneshot_active(void);
extern void tick_check_oneshot_broadcast_this_cpu(void);
bool tick_broadcast_oneshot_available(void);
extern struct cpumask *tick_get_broadcast_oneshot_mask(void);
#else /* !(BROADCAST && ONESHOT): */

#endif /* !(BROADCAST && ONESHOT) */

#if defined(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST) && defined(CONFIG_HOTPLUG_CPU)
extern void tick_broadcast_offline(unsigned int cpu);
#else

#endif

/* NO_HZ_FULL internal */
#ifdef CONFIG_NO_HZ_FULL    /* no-hz 减少调度时钟中断 */
extern void tick_nohz_init(void);
# else

#endif

#ifdef CONFIG_NO_HZ_COMMON
extern unsigned long tick_nohz_active;
extern void timers_update_nohz(void);
# ifdef CONFIG_SMP
extern struct static_key_false timers_migration_enabled;
# endif
#else /* CONFIG_NO_HZ_COMMON */

#endif

DECLARE_PER_CPU(struct hrtimer_cpu_base, hrtimer_bases);

extern u64 get_next_timer_interrupt(unsigned long basej, u64 basem);
void timer_clear_idle(void);
