/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SECCOMP_H
#define _LINUX_SECCOMP_H

#include <uapi/linux/seccomp.h>

#define SECCOMP_FILTER_FLAG_MASK	(SECCOMP_FILTER_FLAG_TSYNC | \
					 SECCOMP_FILTER_FLAG_LOG | \
					 SECCOMP_FILTER_FLAG_SPEC_ALLOW | \
					 SECCOMP_FILTER_FLAG_NEW_LISTENER | \
					 SECCOMP_FILTER_FLAG_TSYNC_ESRCH)

/* sizeof() the first published struct seccomp_notif_addfd */
#define SECCOMP_NOTIFY_ADDFD_SIZE_VER0 24
#define SECCOMP_NOTIFY_ADDFD_SIZE_LATEST SECCOMP_NOTIFY_ADDFD_SIZE_VER0

#ifdef CONFIG_SECCOMP

#include <linux/thread_info.h>
#include <linux/atomic.h>
#include <asm/seccomp.h>

struct seccomp_filter;
/**
 * struct seccomp - the state of a seccomp'ed process
 *
 * @mode:  indicates one of the valid values above for controlled
 *         system calls available to a process.
 * @filter: must always point to a valid seccomp-filter or NULL as it is
 *          accessed without locking during system call entry.
 *
 *          @filter must only be accessed from the context of current as there
 *          is no read locking.
 *
 * 过滤特定的系统调用
 * 经常和 能力 一起使用
 *
 * 可以使用 `prctl(2)` 的 `PR_SET_SECCOMP` 操作加载 seccomp 过滤器(BPF程序)
 *  该程序将在每个 seccomp 数据包上执行(数据包`struct seccomp_data`结构表示)
 */
struct seccomp {    /* 限制系统调用 */
	int mode;
	atomic_t filter_count;
    /**
     *
     */
	struct seccomp_filter *filter;
};

#ifdef CONFIG_HAVE_ARCH_SECCOMP_FILTER
extern int __secure_computing(const struct seccomp_data *sd);
static inline int secure_computing(void)
{
	if (unlikely(test_thread_flag(TIF_SECCOMP)))
		return  __secure_computing(NULL);
	return 0;
}
#else
extern void secure_computing_strict(int this_syscall);
#endif

extern long prctl_get_seccomp(void);
extern long prctl_set_seccomp(unsigned long, void __user *);

static inline int seccomp_mode(struct seccomp *s)
{
	return s->mode;
}

#else /* CONFIG_SECCOMP */

#endif /* CONFIG_SECCOMP */

#ifdef CONFIG_SECCOMP_FILTER
extern void seccomp_filter_release(struct task_struct *tsk);
extern void get_seccomp_filter(struct task_struct *tsk);
#else  /* CONFIG_SECCOMP_FILTER */


#endif /* CONFIG_SECCOMP_FILTER */

#if defined(CONFIG_SECCOMP_FILTER) && defined(CONFIG_CHECKPOINT_RESTORE)
extern long seccomp_get_filter(struct task_struct *task,
			       unsigned long filter_off, void __user *data);
extern long seccomp_get_metadata(struct task_struct *task,
				 unsigned long filter_off, void __user *data);
#else


#endif /* CONFIG_SECCOMP_FILTER && CONFIG_CHECKPOINT_RESTORE */
#endif /* _LINUX_SECCOMP_H */
