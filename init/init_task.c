// SPDX-License-Identifier: GPL-2.0
#include <linux/init_task.h>
#include <linux/export.h>
#include <linux/mqueue.h>
#include <linux/sched.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/rt.h>
#include <linux/sched/task.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/audit.h>
#include <linux/numa.h>
#include <linux/scs.h>

#include <linux/uaccess.h>

static struct signal_struct init_signals /* init_task.signal */= {
	.nr_threads	= 1,
	.thread_head	= LIST_HEAD_INIT(init_task.thread_node),
	.wait_chldexit	= __WAIT_QUEUE_HEAD_INITIALIZER(init_signals.wait_chldexit),
	.shared_pending	= {
		.list = LIST_HEAD_INIT(init_signals.shared_pending.list),
		.signal =  {{0}}
	},
	.multiprocess	= HLIST_HEAD_INIT,
	.rlim		= INIT_RLIMITS,
	.cred_guard_mutex = __MUTEX_INITIALIZER(init_signals.cred_guard_mutex),
	.exec_update_lock = __RWSEM_INITIALIZER(init_signals.exec_update_lock),
#ifdef CONFIG_POSIX_TIMERS
	.posix_timers = LIST_HEAD_INIT(init_signals.posix_timers),
	.cputimer	= {
		.cputime_atomic	= INIT_CPUTIME_ATOMIC,
	},
#endif
	INIT_CPU_TIMERS(init_signals)
	.pids = {
		[PIDTYPE_PID]	= &init_struct_pid,
		[PIDTYPE_TGID]	= &init_struct_pid,
		[PIDTYPE_PGID]	= &init_struct_pid,
		[PIDTYPE_SID]	= &init_struct_pid,
	},
	INIT_PREV_CPUTIME(init_signals)
};

static struct sighand_struct init_sighand = {
	.count		= REFCOUNT_INIT(1),
	.action		= { { { .sa_handler = SIG_DFL, } }, },
	.siglock	= __SPIN_LOCK_UNLOCKED(init_sighand.siglock),
	.signalfd_wqh	= __WAIT_QUEUE_HEAD_INITIALIZER(init_sighand.signalfd_wqh),
};

#ifdef CONFIG_SHADOW_CALL_STACK
unsigned long __init_task_data init_shadow_call_stack[SCS_SIZE / sizeof(long)]
		 = {
	[(SCS_SIZE / sizeof(long)) - 1] = SCS_END_MAGIC
};
#endif

/*
 * Set up the first task table, touch at your own risk!. Base=0,
 * limit=0x1fffff (=2MB)
 */
struct task_struct init_task
#ifdef CONFIG_ARCH_TASK_STRUCT_ON_STACK
	__init_task_data/* 设置 section */
#endif
	__aligned(L1_CACHE_BYTES)/* 一级缓存对齐 */
= {
#ifdef CONFIG_THREAD_INFO_IN_TASK
	.thread_info	= INIT_THREAD_INFO(init_task),/* flags=0, preempt_count=INIT_PREEMPT_COUNT */
	.stack_refcount	= REFCOUNT_INIT(1),/* 引用计数 refcount=1 */
#endif
	.state		= 0,/* 进程状态 */
	.stack		= init_stack,/* 进程栈， x86 下 为 16K 或 32K (假设 PAGE_SIZE = 4K) */
	.usage		= REFCOUNT_INIT(2),
	.flags		= PF_KTHREAD,   /* 进程标志， 内核进程 */
	.prio		= MAX_PRIO/* 140 */ - 20,/* 120 */
	.static_prio	= MAX_PRIO - 20,/* 120 */
	.normal_prio	= MAX_PRIO - 20,/* 120 */
	.policy		= SCHED_NORMAL,/* NORMAL - CFS?? */
	.cpus_ptr	= &init_task.cpus_mask,/* CPU 亲和性 */
	.cpus_mask	= CPU_MASK_ALL,/* 所有 CPU */
	.nr_cpus_allowed= NR_CPUS,/* 允许的 CPU */
	.mm		= NULL,/* 内存 */
	.active_mm	= &init_mm,/* 内存 */
	.restart_block	= {/* syscall 重新开始 block */
		.fn = do_no_restart_syscall,/* 啥也没做 */
	},
	.se	/* 调度实体 struct sched_entity */	= {
		.group_node 	= LIST_HEAD_INIT(init_task.se.group_node),/* 初始化链表 */
	},
	.rt	/* 实时调度实体 sched_rt_entity */	= {
		.run_list	= LIST_HEAD_INIT(init_task.rt.run_list),/* 链表 */
		.time_slice	= RR_TIMESLICE, /* 时间片 */
	},
	.tasks		= LIST_HEAD_INIT(init_task.tasks),/* tasks */
#ifdef CONFIG_SMP
	.pushable_tasks	= PLIST_NODE_INIT(init_task.pushable_tasks, MAX_PRIO/* 140 */),/* 优先级队列 */
#endif
#ifdef CONFIG_CGROUP_SCHED
	.sched_task_group = &root_task_group,/* cgroup 组调度 */
#endif
	.ptraced	= LIST_HEAD_INIT(init_task.ptraced),/* 使用了 ptrace() 的 task */
	.ptrace_entry	= LIST_HEAD_INIT(init_task.ptrace_entry),/* task_struct.parent.ptraced */
	.real_parent	= &init_task,/* 没办法，自己是自己的 parent，亚当和夏娃 */
	.parent		= &init_task,/* 同上 */
	.children	= LIST_HEAD_INIT(init_task.children),/* 子进程 */
	.sibling	= LIST_HEAD_INIT(init_task.sibling),/* 兄弟进程 */
	.group_leader	= &init_task,/* 组的组长 */
	RCU_POINTER_INITIALIZER(real_cred, &init_cred),/* 任务凭证 TODO */
	RCU_POINTER_INITIALIZER(cred, &init_cred),
	.comm		= INIT_TASK_COMM,/* 线程 名字， 这里是 swapper */
	.thread		= INIT_THREAD,/* CPU 相关(寄存器等) 信息，x86-64 这个为空*/
	.fs		= &init_fs,/* 文件系统 */
	.files		= &init_files,/* 打开的文件 */
#ifdef CONFIG_IO_URING
	.io_uring	= NULL,/* io_uring AIO */
#endif
	.signal		= &init_signals,/* 信号 */
	.sighand	= &init_sighand,/*  */
	.nsproxy	= &init_nsproxy,/* 命名空间 */
	.pending	= {/* 挂起 */
		.list = LIST_HEAD_INIT(init_task.pending.list),
		.signal = {{0}}
	},
	.blocked	= {{0}},
	.alloc_lock	= __SPIN_LOCK_UNLOCKED(init_task.alloc_lock),/* 保护一些 分配 的 变量 */
	.journal_info	= NULL,
	INIT_CPU_TIMERS(init_task)/* POSIX CPU 定时器，  */
	.pi_lock	= __RAW_SPIN_LOCK_UNLOCKED(init_task.pi_lock),/*  */
	.timer_slack_ns = 50000, /* 50 usec default slack松弛的 *//* 用于poll 和 select */
	.thread_pid	= &init_struct_pid,/* PID 的哈希表 */
	.thread_group	= LIST_HEAD_INIT(init_task.thread_group),
	.thread_node	= LIST_HEAD_INIT(init_signals.thread_head),
#ifdef CONFIG_AUDIT
	.loginuid	= INVALID_UID,/*  */
	.sessionid	= AUDIT_SID_UNSET,/*  */
#endif
#ifdef CONFIG_PERF_EVENTS/* perf_event */
	.perf_event_mutex = __MUTEX_INITIALIZER(init_task.perf_event_mutex),
	.perf_event_list = LIST_HEAD_INIT(init_task.perf_event_list),
#endif
#ifdef CONFIG_PREEMPT_RCU/*  */
	.rcu_read_lock_nesting = 0,
	.rcu_read_unlock_special.s = 0,
	.rcu_node_entry = LIST_HEAD_INIT(init_task.rcu_node_entry),
	.rcu_blocked_node = NULL,
#endif
#ifdef CONFIG_TASKS_RCU/*  */
	.rcu_tasks_holdout = false,
	.rcu_tasks_holdout_list = LIST_HEAD_INIT(init_task.rcu_tasks_holdout_list),
	.rcu_tasks_idle_cpu = -1,
#endif
#ifdef CONFIG_TASKS_TRACE_RCU/*  */
	.trc_reader_nesting = 0,
	.trc_reader_special.s = 0,
	.trc_holdout_list = LIST_HEAD_INIT(init_task.trc_holdout_list),
#endif
#ifdef CONFIG_CPUSETS/*  */
	.mems_allowed_seq = SEQCNT_SPINLOCK_ZERO(init_task.mems_allowed_seq,
						 &init_task.alloc_lock),
#endif
#ifdef CONFIG_RT_MUTEXES/*  */
	.pi_waiters	= RB_ROOT_CACHED,
	.pi_top_task	= NULL,
#endif
	INIT_PREV_CPUTIME(init_task)
#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
	.vtime.seqcount	= SEQCNT_ZERO(init_task.vtime_seqcount),
	.vtime.starttime = 0,
	.vtime.state	= VTIME_SYS,
#endif
#ifdef CONFIG_NUMA_BALANCING
	.numa_preferred_nid = NUMA_NO_NODE,
	.numa_group	= NULL,
	.numa_faults	= NULL,
#endif
#ifdef CONFIG_KASAN
	.kasan_depth	= 1,
#endif
#ifdef CONFIG_KCSAN
	.kcsan_ctx = {
		.disable_count		= 0,
		.atomic_next		= 0,
		.atomic_nest_count	= 0,
		.in_flat_atomic		= false,
		.access_mask		= 0,
		.scoped_accesses	= {LIST_POISON1, NULL},
	},
#endif
#ifdef CONFIG_TRACE_IRQFLAGS
	.softirqs_enabled = 1,
#endif
#ifdef CONFIG_LOCKDEP
	.lockdep_depth = 0, /* no locks held yet */
	.curr_chain_key = INITIAL_CHAIN_KEY,
	.lockdep_recursion = 0,
#endif
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.ret_stack	= NULL,
#endif
#if defined(CONFIG_TRACING) && defined(CONFIG_PREEMPTION)
	.trace_recursion = 0,
#endif
#ifdef CONFIG_LIVEPATCH
	.patch_state	= KLP_UNDEFINED,
#endif
#ifdef CONFIG_SECURITY
	.security	= NULL,
#endif
#ifdef CONFIG_SECCOMP
	.seccomp	= { .filter_count = ATOMIC_INIT(0) },
#endif
};
EXPORT_SYMBOL(init_task);/*  TODO MORE*/

/*
 * Initial thread structure. Alignment of this is handled by a special
 * linker map entry.
 */
#ifndef CONFIG_THREAD_INFO_IN_TASK
struct thread_info __init_thread_info init_thread_info  = INIT_THREAD_INFO(init_task);
#endif
