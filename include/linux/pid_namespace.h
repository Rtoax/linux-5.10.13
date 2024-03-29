/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_NS_H
#define _LINUX_PID_NS_H

#include <linux/sched.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/workqueue.h>
#include <linux/threads.h>
#include <linux/nsproxy.h>
#include <linux/kref.h>
#include <linux/ns_common.h>
#include <linux/idr.h>

/* MAX_PID_NS_LEVEL is needed for limiting size of 'struct pid' */
#define MAX_PID_NS_LEVEL 32

struct fs_pin;

/**
 *
 *  全局静态变量: `init_pid_ns`
 */
struct pid_namespace {  /* pid 隔离 namespace */
    /**
     *  引用计数
     */
	struct kref kref;
	struct idr idr; /* ID to Pointer */
    /**
     * @brief
     *
     */
	struct rcu_head rcu;
    /**
     * @brief
     *
     * 初始值= PIDNS_ADDING = 1000 0000 0000 0000 0000 0000 0000 0000
     *  (见create_pid_namespace())
     *
     */
	unsigned int pid_allocated;

    /**
     *  收割者
     */
	struct task_struct *child_reaper;
    /**
     *  created in `create_pid_namespace()`
     *  init_pid_ns 分配 `pid_idr_init()`
     */
	struct kmem_cache *pid_cachep;

    /**
     *  级别，在 `create_pid_namespace()` 中会用
     */
	unsigned int level;
    /**
     *  父亲
     */
	struct pid_namespace *parent;

#ifdef CONFIG_BSD_PROCESS_ACCT
	struct fs_pin *bacct;
#endif
    /**
     *
     */
	struct user_namespace *user_ns;
    /**
     *  计数
     */
	struct ucounts *ucounts;
	int reboot;	/* group exit code if this pidns was rebooted */
    /**
     *  超类
     */
	struct ns_common ns;
} __randomize_layout;

extern struct pid_namespace init_pid_ns;

/**
 * @brief   PIDNS_ADDING 标志位去掉则代表 去使能
 *
 * disable_pid_allocation
 *
 */
#define PIDNS_ADDING (1U << 31)

#ifdef CONFIG_PID_NS
static inline struct pid_namespace *get_pid_ns(struct pid_namespace *ns)
{   /*   */
	if (ns != &init_pid_ns)
		kref_get(&ns->kref);
	return ns;
}

extern struct pid_namespace *copy_pid_ns(unsigned long flags,
	struct user_namespace *user_ns, struct pid_namespace *ns);
extern void zap_pid_ns_processes(struct pid_namespace *pid_ns);
extern int reboot_pid_ns(struct pid_namespace *pid_ns, int cmd);
extern void put_pid_ns(struct pid_namespace *ns);

#else /* !CONFIG_PID_NS */

#endif /* CONFIG_PID_NS */

extern struct pid_namespace *task_active_pid_ns(struct task_struct *tsk);
void pidhash_init(void);
void pid_idr_init(void);

#endif /* _LINUX_PID_NS_H */
