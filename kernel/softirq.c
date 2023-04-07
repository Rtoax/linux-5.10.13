// SPDX-License-Identifier: GPL-2.0-only
/*
 *	linux/kernel/softirq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *
 *	Rewritten. Old one was good in 2.2, but in 2.3 it was immoral. --ANK (990903)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/notifier.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/ftrace.h>
#include <linux/smp.h>
#include <linux/smpboot.h>
#include <linux/tick.h>
#include <linux/irq.h>

#define CREATE_TRACE_POINTS
#include <trace/events/irq.h>

/*
   - No shared variables, all the data are CPU local.
   - If a softirq needs serialization, let it serialize itself
	 by its own spinlocks.
   - Even if softirq is serialized, only local cpu is marked for
	 execution. Hence, we get something sort of weak cpu binding.
	 Though it is still not clear, will it result in better locality
	 or will not.

   Examples:
   - NET RX softirq. It is multithreaded and does not require
	 any global serialization.
   - NET TX softirq. It kicks software netdevice queues, hence
	 it is logically serialized per device, but this serialization
	 is invisible to common code.
   - Tasklets: serialized wrt itself.
 */

#ifndef __ARCH_IRQ_STAT
DEFINE_PER_CPU_ALIGNED(irq_cpustat_t, irq_stat);
EXPORT_PER_CPU_SYMBOL(irq_stat);
#endif

/**
 *  处理函数 - 软中断描述符
 */
static struct softirq_action __cacheline_aligned_in_smp softirq_vec[NR_SOFTIRQS] ;

/**
 *  软中断 线程 - 每个 CPU 一个
 *  从 top 命令看到的 %Cpu0   40 %si 的利用率，应该要比看到的 ksoftirqd/0 看到的
 *  %CPU 要大。也就是说，软终端不一定是由 ksoftirqd 处理的。
 */
DEFINE_PER_CPU(struct task_struct *, ksoftirqd);
struct task_struct * ksoftirqd; //+++


//cat /proc/softirqs
//                    CPU0       CPU1       CPU2       CPU3       CPU4       CPU5       CPU6       CPU7
//          HI:          5          0          0          0          0          0          0          0
//       TIMER:     332519     310498     289555     272913     282535     279467     282895     270979
//      NET_TX:       2320          0          0          2          1          1          0          0
//      NET_RX:     270221        225        338        281        311        262        430        265
//       BLOCK:     134282         32         40         10         12          7          8          8
//BLOCK_IOPOLL:          0          0          0          0          0          0          0          0
//     TASKLET:     196835          2          3          0          0          0          0          0
//       SCHED:     161852     146745     129539     126064     127998     128014     120243     117391
//     HRTIMER:          0          0          0          0          0          0          0          0
//         RCU:     337707     289397     251874     239796     254377     254898     267497     256624
const char * const softirq_to_name[NR_SOFTIRQS] = {
	"HI",       /* 高优先级 */
	"TIMER",    /* 定时器 */
	"NET_TX",   /* 网络发包 */
	"NET_RX",   /* 网络收包 */
	"BLOCK",    /* 块 */
	"IRQ_POLL", /* 中断 poll */
	"TASKLET",  /* 小任务 tasklet */
	"SCHED",    /* 调度 */
	"HRTIMER",  /* 高精度定时器 */
	"RCU"       /* Read Copy Update */
};

/*
 * we cannot loop indefinitely here to avoid userspace starvation,
 * but we also don't want to introduce a worst case 1/HZ latency
 * to the pending events, so lets the scheduler to balance
 * the softirq load for us.
 *
 * ksoftirqd is a per-cpu kernel thread that runs when the machine is under
 * heavy soft-interrupt load. Soft interrupts are normally serviced on return
 * from a hard interrupt, but it’s possible for soft interrupts to be triggered
 * more quickly than they can be serviced. If a soft interrupt is triggered for
 * a second time while soft interrupts are being handled, the ksoftirq daemon is
 * triggered to handle the soft interrupts in process context. If ksoftirqd is
 * taking more than a tiny percentage of CPU time, this indicates the machine is
 * under heavy soft interrupt load.
 *
 * ksoftirqd was introduced during the 2.3 development series as part of the
 * softnet work by Alexey Kuznetsov and David Miller.
 *
 * https://man.cx/ksoftirqd(9)
 *
 * 唤醒 ksoftirqd 线程
 *
 * [rongtao@localhost src]$ ps -ef | grep soft
 * root          6      2  0 5月14 ?       00:02:02 [ksoftirqd/0]
 * root         12      2  0 5月14 ?       00:00:11 [ksoftirqd/1]
 * root         16      2  0 5月14 ?       00:00:00 [ksoftirqd/2]
 * root         20      2  0 5月14 ?       00:00:00 [ksoftirqd/3]
 */
static void wakeup_softirqd(void)
{
	/* Interrupts are disabled: no need to stop preemption */
	struct task_struct *tsk = __this_cpu_read(ksoftirqd);

	if (tsk && tsk->state != TASK_RUNNING) {
		/* 唤醒这个 CPU 的 ksoftirwd 线程 */
		wake_up_process(tsk);
	}
}

/*
 * If ksoftirqd is scheduled, we do not want to process pending softirqs
 * right now. Let ksoftirqd handle this at its own rate, to get fairness,
 * unless we're doing some of the synchronous softirqs.
 *
 * 有些软中断不希望被 挂起，除非同步 软终端
 */
#define SOFTIRQ_NOW_MASK ((1 << HI_SOFTIRQ) | (1 << TASKLET_SOFTIRQ))

/**
 *  ksoftirqd 在运行
 */
static bool ksoftirqd_running(unsigned long pending)
{
	struct task_struct *tsk = __this_cpu_read(ksoftirqd);

	if (pending & SOFTIRQ_NOW_MASK)
		return false;

	/**
	 *
	 */
	return tsk && (tsk->state == TASK_RUNNING) && !__kthread_should_park(tsk);
}

/*
 * preempt_count and SOFTIRQ_OFFSET usage:
 * - preempt_count is changed by SOFTIRQ_OFFSET on entering or leaving
 *   softirq processing.
 * - preempt_count is changed by SOFTIRQ_DISABLE_OFFSET (= 2 * SOFTIRQ_OFFSET)
 *   on local_bh_disable or local_bh_enable.
 * This lets us distinguish between whether we are currently processing
 * softirq and whether we just have bh disabled.
 */

/*
 * This one is for softirq.c-internal use,
 * where hardirqs are disabled legitimately:
 */
#ifdef CONFIG_TRACE_IRQFLAGS

DEFINE_PER_CPU(int, hardirqs_enabled);
DEFINE_PER_CPU(int, hardirq_context);
int hardirqs_enabled;//+++
int hardirq_context;//+++
EXPORT_PER_CPU_SYMBOL_GPL(hardirqs_enabled);
EXPORT_PER_CPU_SYMBOL_GPL(hardirq_context);


/**
 *
 */
void __local_bh_disable_ip(unsigned long ip, unsigned int cnt)
{
	unsigned long flags;

	WARN_ON_ONCE(in_irq());

	raw_local_irq_save(flags);
	/*
	 * The preempt tracer hooks into preempt_count_add and will break
	 * lockdep because it calls back into lockdep after SOFTIRQ_OFFSET
	 * is set and before current->softirq_enabled is cleared.
	 * We must manually increment preempt_count here and manually
	 * call the trace_preempt_off later.
	 */
	__preempt_count_add(cnt);
	/*
	 * Were softirqs turned off above:
	 */
	if (softirq_count() == (cnt & SOFTIRQ_MASK))
		lockdep_softirqs_off(ip);
	raw_local_irq_restore(flags);

	if (preempt_count() == cnt) {
#ifdef CONFIG_DEBUG_PREEMPT
		current->preempt_disable_ip = get_lock_parent_ip();
#endif
		trace_preempt_off(CALLER_ADDR0, get_lock_parent_ip());
	}
}
EXPORT_SYMBOL(__local_bh_disable_ip);
#endif /* CONFIG_TRACE_IRQFLAGS */

/**
 *  本地 bottom-half 使能
 */
static void __local_bh_enable(unsigned int cnt)
{
	lockdep_assert_irqs_disabled();

	/**
	 *
	 */
	if (preempt_count() == cnt)
		trace_preempt_on(CALLER_ADDR0, get_lock_parent_ip());

	if (softirq_count() == (cnt & SOFTIRQ_MASK))
		lockdep_softirqs_on(_RET_IP_);

	__preempt_count_sub(cnt);
}

/*
 * Special-case - softirqs can safely be enabled by __do_softirq(),
 * without processing still-pending softirqs:
 */
void _local_bh_enable(void)
{
	WARN_ON_ONCE(in_irq());
	__local_bh_enable(SOFTIRQ_DISABLE_OFFSET);
}
EXPORT_SYMBOL(_local_bh_enable);

/**
 *
 */
void __local_bh_enable_ip(unsigned long ip, unsigned int cnt)
{
	/**
	 *  警告条件
	 *  如果在硬件上下文中，处于关中断的情况，没有必要再调用关 BH
	 */
	WARN_ON_ONCE(in_irq());

	lockdep_assert_irqs_enabled();

#ifdef CONFIG_TRACE_IRQFLAGS
	local_irq_disable();
#endif
	/*
	 * Are softirqs going to be turned on now:
	 */
	if (softirq_count() == SOFTIRQ_DISABLE_OFFSET)
		lockdep_softirqs_on(ip);
	/*
	 * Keep preemption disabled until we are done with
	 * softirq processing:
	 *
	 * 为什么还留 1 呢？
	 * 表示关闭本地CPU的抢占，由于下面执行 do_softirq 函数时，
	 * 不希望其他高优先级任务抢占 CPU 或者当前任务呗迁移到其他
	 * CPU上。
	 */
	preempt_count_sub(cnt - 1);

	/**
	 *  在非中断上下文环境下执行软中断处理
	 */
	if (unlikely(!in_interrupt() && local_softirq_pending())) {
		/*
		 * Run softirq if any pending. And do it in its own stack
		 * as we may be calling this deep in a task call stack already.
		 */
		do_softirq();
	}

	/**
	 *  打开抢占，和上面的 留 1 呼应
	 */
	preempt_count_dec();

#ifdef CONFIG_TRACE_IRQFLAGS
	local_irq_enable();
#endif

	/**
	 *  之前执行软中断处理时，可能会漏掉一些高优先级任务的抢占需求，
	 *  这里重新检查。
	 */
	preempt_check_resched();
}
EXPORT_SYMBOL(__local_bh_enable_ip);

/*
 * We restart softirq processing for at most MAX_SOFTIRQ_RESTART times,
 * but break the loop if need_resched() is set or after 2 ms.
 * The MAX_SOFTIRQ_TIME provides a nice upper bound in most cases, but in
 * certain cases, such as stop_machine(), jiffies may cease to
 * increment and so we need the MAX_SOFTIRQ_RESTART limit as
 * well to make sure we eventually return from this method.
 *
 * These limits have been established via experimentation.
 * The two things to balance is latency against fairness -
 * we want to handle softirqs as soon as possible, but they
 * should not be able to lock up the box.
 */
#define MAX_SOFTIRQ_TIME  msecs_to_jiffies(2)
#define MAX_SOFTIRQ_RESTART 10

#ifdef CONFIG_TRACE_IRQFLAGS
/*
 * When we run softirqs from irq_exit() and thus on the hardirq stack we need
 * to keep the lockdep irq context tracking as tight as possible in order to
 * not miss-qualify lock contexts and miss possible deadlocks.
 */

static inline bool lockdep_softirq_start(void)
{
	bool in_hardirq = false;

	if (lockdep_hardirq_context()) {
		in_hardirq = true;
		lockdep_hardirq_exit();
	}

	lockdep_softirq_enter();

	return in_hardirq;
}

static inline void lockdep_softirq_end(bool in_hardirq)
{
	lockdep_softirq_exit();

	if (in_hardirq)
		lockdep_hardirq_enter();
}
#else
static inline bool lockdep_softirq_start(void) { return false; }
static inline void lockdep_softirq_end(bool in_hardirq) { }
#endif

/**
 * During execution of a deferred（延期的） function, new pending `softirqs` might
 * occur. The main problem here that execution of the userspace code can be
 * delayed for a long time while the `__do_softirq` function will handle
 * deferred interrupts. For this purpose, it has the limit of the time when it
 * must be finished.
 *
 *  处理软中断
 *  这是在中断上下文中发生的
 *
 * $ sudo bpftrace -e 'kprobe:__do_softirq {@[comm] = count();}'
 */
asmlinkage __visible void __softirq_entry __do_softirq(void)
{
	/**
	 *  由于下一次 softirq 可能随时到来，这里需要限定运行时间(2ms)
	 */
	unsigned long end = jiffies + MAX_SOFTIRQ_TIME;

	/**
	 *  保存 进程 flags
	 */
	unsigned long old_flags = current->flags;

	/**
	 *  如果在软中断处理过程，又接收到了新的软中断，可以restart，但限制次数为 10 次
	 */
	int max_restart = MAX_SOFTIRQ_RESTART/*10*/;
	struct softirq_action *h;
	bool in_hardirq;
	__u32 pending;
	int softirq_bit;

	/*
	 * Mask out PF_MEMALLOC as the current task context is borrowed for the
	 * softirq. A softirq handled, such as network RX, might set PF_MEMALLOC
	 * again if the socket is related to swapping.
	 *
	 * 期间不能使用 系统预留内存
	 */
	current->flags &= ~PF_MEMALLOC;

	/**
	 *  获取软中断，并存入 pending 中
	 *  这是按照 bitmap 使用的，每个软中断号对应 一位
	 */
	pending = local_softirq_pending();
	account_irq_enter_time(current);

	/**
	 *  增加 SOFTIRQ 计数
	 */
	__local_bh_disable_ip(_RET_IP_, SOFTIRQ_OFFSET);

	/**
	 *
	 */
	in_hardirq = lockdep_softirq_start();

restart:
	/* Reset the pending bitmask before enabling irqs */
	/**
	 *  清除 软中断
	 *  在上面已经将当前的软中断保存在 pending 局部变量中
	 */
	set_softirq_pending(0);

	/**
	 *  现在就可以打开本地中断了，因为局部变量 pending 中已经存了
	 */
	local_irq_enable();

	/**
	 *  获取 softirq_vec 处理 action() 回调数组
	 */
	h = softirq_vec;

	/**
	 *  从 软中断中获取中断，并依次处理。取出一位
	 */
	while ((softirq_bit = ffs(pending))) {
		unsigned int vec_nr;
		int prev_count;

		/**
		 *  获取 软中断 比特位对应的 sofirq_action 结构
		 */
		h += softirq_bit - 1;

		/**
		 *  第几个
		 */
		vec_nr = h - softirq_vec;

		/**
		 *
		 */
		prev_count = preempt_count();

		/**
		 *  统计
		 */
		kstat_incr_softirqs_this_cpu(vec_nr);

		/**
		 * @brief sudo bpftrace -lv tracepoint:irq:softirq_entry
		 *
		 */
		trace_softirq_entry(vec_nr);

		/**
		 *  执行 action
		 *
		 * open_softirq(HRTIMER_SOFTIRQ, hrtimer_run_softirq);
		 * open_softirq(TASKLET_SOFTIRQ, tasklet_action);
		 * open_softirq(HI_SOFTIRQ, tasklet_hi_action);
		 * open_softirq(SCHED_SOFTIRQ, run_rebalance_domains);
		 * open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);
		 * open_softirq(RCU_SOFTIRQ, rcu_core_si);
		 * open_softirq(IRQ_POLL_SOFTIRQ, irq_poll_softirq);
		 * open_softirq(TIMER_SOFTIRQ, run_timer_softirq);
		 * open_softirq(NET_TX_SOFTIRQ, net_tx_action);
		 * open_softirq(NET_RX_SOFTIRQ, net_rx_action);
		 * open_softirq(BLOCK_SOFTIRQ, blk_done_softirq);
		 */
		h->action(h);

		trace_softirq_exit(vec_nr);

		/**
		 *
		 */
		if (unlikely(prev_count != preempt_count())) {
			pr_err("huh, entered softirq %u %s %p with preempt_count %08x, exited with %08x?\n",
			       vec_nr, softirq_to_name[vec_nr], h->action,
			       prev_count, preempt_count());
			preempt_count_set(prev_count);
		}
		h++;
		pending >>= softirq_bit;
	}

	/**
	 *  当前进程为 ksoftirqd 进程
	 */
	if (__this_cpu_read(ksoftirqd) == current)
		rcu_softirq_qs();

	/**
	 *  关闭本地中断
	 */
	local_irq_disable();

	/**
	 *  检查是否又产生了软中断
	 */
	pending = local_softirq_pending();
	if (pending) {
		/**
		 *  继续开始的条件
		 *
		 *  1. time_before(jiffies, end) ===> jiffies < end
		 *      软中断处理时间没有超过 2ms(MAX_SOFTIRQ_TIME)
		 *
		 *  2. 不需要重新调度：
		 *  3. max_restart-1 > 0： 这种循环不能多于 10 次
		 */
		if (time_before(jiffies, end) && !need_resched() && --max_restart) {
			/**
			 *  如果上面要求都满足，那么可以再次处理在上次处理软中断期间
			 *  再次生成的软中断。
			 */
			goto restart;
		}

		/**
		 *  如果存在软中断未处理，但是又不满足上面的条件
		 *  将唤醒 当前 CPU 后台 softirqd 线程
		 *
		 * ksoftirqd is a per-cpu kernel thread that runs when the machine
		 * is under heavy soft-interrupt load. Soft interrupts are normally
		 * serviced on return from a hard interrupt, but it’s possible for
		 * soft interrupts to be triggered more quickly than they can be
		 * serviced. If a soft interrupt is triggered for a second time
		 * while soft interrupts are being handled, the ksoftirq daemon is
		 * triggered to handle the soft interrupts in process context. If
		 * ksoftirqd is taking more than a tiny percentage of CPU time, this
		 * indicates the machine is under heavy soft interrupt load.
		 */
		wakeup_softirqd();
	}

	/**
	 *
	 */
	lockdep_softirq_end(in_hardirq);

	/**
	 *
	 */
	account_irq_exit_time(current);

	/**
	 *  使能 本地 bottom-half
	 */
	__local_bh_enable(SOFTIRQ_OFFSET);

	WARN_ON_ONCE(in_interrupt());

	/**
	 *  恢复 进程 flags
	 */
	current_restore_flags(old_flags, PF_MEMALLOC);
}


/**
 *
 */
asmlinkage __visible void do_softirq(void)
{
	__u32 pending;
	unsigned long flags;

	/**
	 *  softirq 不能在中断上下文
	 */
	if (in_interrupt())
		return;

	local_irq_save(flags);

	/**
	 *
	 */
	pending = local_softirq_pending();

	/**
	 *  如果 有软中断发生，并且 ksoftirq 没在运行
	 */
	if (pending && !ksoftirqd_running(pending))
		/**
		 *
		 */
		do_softirq_own_stack();

	local_irq_restore(flags);
}

/**
 * irq_enter_rcu - Enter an interrupt context with RCU watching
 */
void irq_enter_rcu(void)
{
	/**
	 *  如果是 idle 线程，并且不在中断上下文中
	 */
	if (is_idle_task(current) && !in_interrupt()) {
		/*
		 * Prevent raise_softirq from needlessly waking up ksoftirqd
		 * here, as softirq will be serviced on return from interrupt.
		 *
		 * 关闭本地中断
		 * 阻止 raise_softirq 无用地唤醒 ksoftirqd 线程
		 */
		local_bh_disable();
		/**
		 *
		 */
		tick_irq_enter();

		_local_bh_enable();
	}
	/**
	 *
	 */
	__irq_enter();
}

/**
 * irq_enter - Enter an interrupt context including RCU update
 *
 * 显示告诉内核，现在需要 进入中断上下文了
 */
void irq_enter(void)
{
	rcu_irq_enter();

	/**
	 *
	 */
	irq_enter_rcu();
}


/**
 *  调用(invoke) 软中断
 */
static inline void invoke_softirq(void)
{
	/**
	 *  ksoftirqd 在运行，就算了
	 */
	if (ksoftirqd_running(local_softirq_pending()))
		return;

	/**
	 *  强制 中断线程化 没有开启
	 */
	if (!force_irqthreads) {
#ifdef CONFIG_HAVE_IRQ_EXIT_ON_IRQ_STACK
		/*
		 * We can safely execute softirq on the current stack if
		 * it is the irq stack, because it should be near empty
		 * at this stage.
		 */
		__do_softirq();
#else
		/*
		 * Otherwise, irq_exit() is called on the task stack that can
		 * be potentially deep already. So call softirq in its own stack
		 * to prevent from any overrun.
		 *
		 * 在自己的栈上运行
		 */
		do_softirq_own_stack();
#endif

	/**
	 *  强制中断线程化
	 */
	} else {
	    /**
		 *  唤醒 ksoftirqd 线程
		 */
		wakeup_softirqd();
	}
}

/**
 *
 */
static inline void tick_irq_exit(void)
{
#ifdef CONFIG_NO_HZ_COMMON
	int cpu = smp_processor_id();

	/* Make sure that timer wheel updates are propagated */
	if ((idle_cpu(cpu) && !need_resched()) || tick_nohz_full_cpu(cpu)) {
		if (!in_irq())
			tick_nohz_irq_exit();
	}
#endif
}

/**
 *  中断退出
 */
static inline void __irq_exit_rcu(void)
{
#ifndef __ARCH_IRQ_EXIT_IRQS_DISABLED
	local_irq_disable();
#else
	lockdep_assert_irqs_disabled();
#endif

	/**
	 *
	 */
	account_irq_exit_time(current);

	/**
	 *  减去 硬中断
	 */
	preempt_count_sub(HARDIRQ_OFFSET);

	/**
	 *  如果在进程上下文中，并且有等待的软中断
	 *
	 *  中断退出时，不能处于硬件中断上下文和软中断上下文中；
	 *  硬件中断处理过程一般都是关闭中断的
	 */
	if (!in_interrupt() && local_softirq_pending())
		/**
		 *  硬中断退出后，调用 软中断
		 */
		invoke_softirq();

	tick_irq_exit();
}

/**
 * irq_exit_rcu() - Exit an interrupt context without updating RCU
 *
 * Also processes softirqs if needed and possible.
 *
 * 退出中断上下文
 */
void irq_exit_rcu(void)
{
	__irq_exit_rcu();
	 /* must be last! */
	lockdep_hardirq_exit();
}

/**
 * irq_exit - Exit an interrupt context, update RCU and lockdep
 *
 * Also processes softirqs if needed and possible.
 */
void irq_exit(void)
{
	/**
	 *  preempt_count_sub(HARDIRQ_OFFSET);
	 */
	__irq_exit_rcu();

	/**
	 *
	 */
	rcu_irq_exit();
	 /* must be last! */
	lockdep_hardirq_exit();
}

/*
 * This function must run with irqs disabled!
 *  该函数运行期间关闭了中断，因此允许在进程上下文中调用
 */
inline void raise_softirq_irqoff(unsigned int nr)
{
	/**
	 *
	 */
	__raise_softirq_irqoff(nr);

	/*
	 * If we're in an interrupt or softirq, we're done
	 * (this also catches softirq-disabled code). We will
	 * actually run the softirq once we return from
	 * the irq or softirq.
	 *
	 * Otherwise we wake up ksoftirqd to make sure we
	 * schedule the softirq soon.
	 *
	 * 如果在进程上下文中
	 */
	if (!in_interrupt())
		/**
		 *  唤醒内核线程
		 */
		wakeup_softirqd();
}

/**
 *  主动触发一个软中断
 *  延期的中断由`open_softirq`打开，使用`raise_softirq`激活
 */
void raise_softirq(unsigned int nr)
{
	unsigned long flags;

	local_irq_save(flags);
	/**
	 *  该函数运行期间关闭了中断，因此允许在进程上下文中调用
	 */
	raise_softirq_irqoff(nr);
	local_irq_restore(flags);
}

/**
 *  主动触发一个软中断
 */
void __raise_softirq_irqoff(unsigned int nr)
{
	lockdep_assert_irqs_disabled();
	trace_softirq_raise(nr);

	/**
	 *
	 */
	or_softirq_pending(1UL << nr);
}

/**
 *  注册一个 softirq
 */
void open_softirq(int nr, void (*action)(struct softirq_action *))/* 赋值 */
{
	softirq_vec[nr].action = action;
}

/*
 * Tasklets
 *
 * 每个 CPU 有两个链表：
 *  1. 一个 普通优先级 `tasklet_vec` ->  TASKLET_SOFTIRQ(优先级=6)
 *  2. 一个 高优先级  `tasklet_hi_vec`-> HI_SOFTIRQ(优先级=0)
 */
struct tasklet_head {
	struct tasklet_struct *head;
	struct tasklet_struct **tail;
};

/**
 *  小任务机制 - 只能发生在中断上下文中
 *
 * 每个 CPU 有两个链表：
 *  1. 一个 普通优先级 `tasklet_vec` ->  TASKLET_SOFTIRQ(优先级=6)
 *  2. 一个 高优先级  `tasklet_hi_vec`-> HI_SOFTIRQ(优先级=0)
 *
 *  普通优先级调度 `tasklet_schedule()`
 *  普通优先级action `tasklet_action()`
 *  高优先级调度 `tasklet_hi_schedule()`
 *  高优先级action `tasklet_hi_action()`
 *
 * 两个链表的初始化见 `softirq_init()`
 */
static DEFINE_PER_CPU(struct tasklet_head, tasklet_vec);
static DEFINE_PER_CPU(struct tasklet_head, tasklet_hi_vec);
static struct tasklet_head __percpu tasklet_vec;    /* +++++ */
static struct tasklet_head __percpu tasklet_hi_vec; /* +++++ */

/**
 *  tasklet 调度
 */
static void __tasklet_schedule_common(struct tasklet_struct *t,
			        				      struct tasklet_head __percpu *headp,
			        				      unsigned int softirq_nr)
{
	struct tasklet_head *head;
	unsigned long flags;

	/**
	 *  关闭本地中断
	 */
	local_irq_save(flags);

	/**
	 *  添加到链表
	 */
	head = this_cpu_ptr(headp);
	t->next = NULL;
	*head->tail = t;
	head->tail = &(t->next);

	/**
	 *  触发软中断
	 */
	raise_softirq_irqoff(softirq_nr);

	/**
	 *  开启本地中断
	 */
	local_irq_restore(flags);
}

/**
 *  普通优先级 tasklet 调度执行
 */
void __tasklet_schedule(struct tasklet_struct *t)
{
	__tasklet_schedule_common(t, &tasklet_vec, TASKLET_SOFTIRQ);
}
EXPORT_SYMBOL(__tasklet_schedule);

/**
 *  高优先级 tasklet 调度执行
 */
void __tasklet_hi_schedule(struct tasklet_struct *t)
{
	__tasklet_schedule_common(t, &tasklet_hi_vec, HI_SOFTIRQ);
}
EXPORT_SYMBOL(__tasklet_hi_schedule);

/**
 *  tasklet 在 softirq 中的 action
 *  TASKLET_SOFTIRQ 和 HI_SOFTIRQ 都会调用这个函数
 */
static void tasklet_action_common(struct softirq_action *a,
			        				  struct tasklet_head *tl_head,
			        				  unsigned int softirq_nr)
{
	struct tasklet_struct *list;

	/**
	 *  禁止本地中断
	 *  因为 tl_head 为 percpu 变量，所以不用加锁，
	 *  但是需要 关闭 本地中断，防止再往这个链表添加
	 */
	local_irq_disable();    /* 禁止本地中断 */

	/**
	 *  获取链表头
	 */
	list = tl_head->head;

	/**
	 *  清空 cpu 的 tasklet 链表头
	 */
	tl_head->head = NULL;
	tl_head->tail = &tl_head->head;

	/**
	 *  使能中断
	 */
	local_irq_enable();

	/**
	 *  执行链表，此过程，没有关闭中断
	 */
	while (list) {

		struct tasklet_struct *t = list;

		list = list->next;

		/**
		 *  检测并设置 TASKLET_STATE_RUN 标志位
		 */
		if (tasklet_trylock(t)) {
			/**
			 *  是否为激活状态，如果为激活状态，进入 if 分支
			 */
			if (!atomic_read(&t->count)) {
			    /**
			     *  是否已经被调度，清除标记为
			     */
				if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
					BUG();

			    /**
			     *  执行回调函数
			     */
				if (t->use_callback)
					t->callback(t); /* 首先选用 callback */
				else
					t->func(t->data);   /* 回调函数 */

			    /**
			     *  执行完回调函数，直接解锁，继续下一个
			     */
				tasklet_unlock(t);
				continue;
			}
			/**
			 *  清除 TASKLET_STATE_RUN 标志位
			 */
			tasklet_unlock(t);
		}

		/**
		 *  如果
		 *  获取 tasklet 锁失败，
		 *  tasklet 未激活
		 *  那么再将 这个 tasklet 添加到 链表中，期间当然要关中断
		 */
		local_irq_disable();/* 禁止本地中断 */
		t->next = NULL;
		*tl_head->tail = t;
		tl_head->tail = &t->next;

		/**
		 *  触发这个软中断
		 */
		__raise_softirq_irqoff(softirq_nr);
		local_irq_enable();
	}
}

/**
 *  普通优先级 tasklet
 *  软中断 TASKLET_SOFTIRQ 触发 action 时候，对应此函数
 */
static __latent_entropy void tasklet_action(struct softirq_action *a)
{
	/**
	 *
	 */
	tasklet_action_common(a, this_cpu_ptr(&tasklet_vec), TASKLET_SOFTIRQ);
}

/**
 *  高优先级 tasklet
 *  软中断 HI_SOFTIRQ 触发 action 时候，对应此函数
 */
static __latent_entropy void tasklet_hi_action(struct softirq_action *a)
{
	tasklet_action_common(a, this_cpu_ptr(&tasklet_hi_vec), HI_SOFTIRQ);
}


/**
 *
 */
void tasklet_setup(struct tasklet_struct *t, void (*callback)(struct tasklet_struct *))
{
	t->next = NULL;
	t->state = 0;
	atomic_set(&t->count, 0);
	t->callback = callback;
	t->use_callback = true;
	t->data = 0;
}
EXPORT_SYMBOL(tasklet_setup);

/**
 *  初始化 一个激活的 tasklet
 */
void tasklet_init(struct tasklet_struct *t, void (*func)(unsigned long), unsigned long data)
{
	/**
	 *
	 */
	t->next = NULL;
	t->state = 0;
	/**
	 *  count=0 : 激活状态
	 */
	atomic_set(&t->count, 0);
	t->func = func;
	t->use_callback = false;
	t->data = data;
}
EXPORT_SYMBOL(tasklet_init);

/**
 *  该函数确保指定的 tasklet 不会再次运行
 *  当设备关闭或者移除时，通常调用这个函数
 */
void tasklet_kill(struct tasklet_struct *t)
{
	/**
	 *  如果在中断上下文中
	 */
	if (in_interrupt())
		pr_notice("Attempt to kill tasklet from interrupt\n");

	/**
	 *  如果正在调度，那么等待调度结束
	 */
	while (test_and_set_bit(TASKLET_STATE_SCHED, &t->state)) {
		do {
			yield();
		} while (test_bit(TASKLET_STATE_SCHED, &t->state));
	}
	/**
	 *  如果正在执行，等待执行结束
	 */
	tasklet_unlock_wait(t);

	/**
	 *  清除调度位
	 */
	clear_bit(TASKLET_STATE_SCHED, &t->state);
}
EXPORT_SYMBOL(tasklet_kill);

/**
 *  start_kernel()->softirq_init()
 *
 *  softirqs是静态分配的，这对内核模块来说是不可加载的，这就引出了tasklets；
 */
void __init softirq_init(void)
{
	int cpu;

	/**
	 *
	 * 每个 CPU 有两个链表：
	 *  1. 一个 普通优先级 `tasklet_vec` ->  TASKLET_SOFTIRQ(优先级=6)
	 *  2. 一个 高优先级  `tasklet_hi_vec`-> HI_SOFTIRQ(优先级=0)
	 */
	for_each_possible_cpu(cpu) { /* 初始化 tasklet 链表 */
		per_cpu(tasklet_vec, cpu).tail    = &per_cpu(tasklet_vec, cpu).head;
		per_cpu(tasklet_hi_vec, cpu).tail = &per_cpu(tasklet_hi_vec, cpu).head;
	}

	/**
	 *  为 tasklet 注册 softirq
	 */
	open_softirq(TASKLET_SOFTIRQ, tasklet_action);
	open_softirq(HI_SOFTIRQ, tasklet_hi_action);    /* high-priority tasklets 高优先级 tasklet */
}

/**
 *  ksoftirqd 是否该运行
 */
static int ksoftirqd_should_run(unsigned int cpu)
{
	/**
	 *  是否有软中断挂起
	 */
	return local_softirq_pending();
}

/**
 *  ksoftirqd 主任务
 *  这是在中断关闭状态下中运行的
 */
static void run_ksoftirqd(unsigned int cpu)
{
	/**
	 *  ksoftirqd
	 */
	local_irq_disable();    /* 关中断 */

	/**
	 *  有挂起 待处理的软中断
	 */
	if (local_softirq_pending()) {
		/*
		 * We can safely run softirq on inline stack, as we are not deep
		 * in the task stack here.
		 *
		 * 执行软中断
		 */
		__do_softirq();
		/**
		 *  执行结束后，开启本地中断
		 */
		local_irq_enable();

		/**
		 *
		 */
		cond_resched();
		return;
	}
	local_irq_enable();
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * tasklet_kill_immediate is called to remove a tasklet which can already be
 * scheduled for execution on @cpu.
 *
 * Unlike tasklet_kill, this function removes the tasklet
 * _immediately_, even if the tasklet is in TASKLET_STATE_SCHED state.
 *
 * When this function is called, @cpu must be in the CPU_DEAD state.
 */
void tasklet_kill_immediate(struct tasklet_struct *t, unsigned int cpu)
{
	struct tasklet_struct **i;

	BUG_ON(cpu_online(cpu));
	BUG_ON(test_bit(TASKLET_STATE_RUN, &t->state));

	if (!test_bit(TASKLET_STATE_SCHED, &t->state))
		return;

	/* CPU is dead, so no lock needed. */
	for (i = &per_cpu(tasklet_vec, cpu).head; *i; i = &(*i)->next) {
		if (*i == t) {
			*i = t->next;
			/* If this was the tail element, move the tail ptr */
			if (*i == NULL)
				per_cpu(tasklet_vec, cpu).tail = i;
			return;
		}
	}
	BUG();
}

static int takeover_tasklets(unsigned int cpu)
{
	/* CPU is dead, so no lock needed. */
	local_irq_disable();

	/* Find end, append list for that CPU. */
	if (&per_cpu(tasklet_vec, cpu).head != per_cpu(tasklet_vec, cpu).tail) {
		*__this_cpu_read(tasklet_vec.tail) = per_cpu(tasklet_vec, cpu).head;
		__this_cpu_write(tasklet_vec.tail, per_cpu(tasklet_vec, cpu).tail);
		per_cpu(tasklet_vec, cpu).head = NULL;
		per_cpu(tasklet_vec, cpu).tail = &per_cpu(tasklet_vec, cpu).head;
	}
	/**
	 *
	 */
	raise_softirq_irqoff(TASKLET_SOFTIRQ);

	if (&per_cpu(tasklet_hi_vec, cpu).head != per_cpu(tasklet_hi_vec, cpu).tail) {
		*__this_cpu_read(tasklet_hi_vec.tail) = per_cpu(tasklet_hi_vec, cpu).head;
		__this_cpu_write(tasklet_hi_vec.tail, per_cpu(tasklet_hi_vec, cpu).tail);
		per_cpu(tasklet_hi_vec, cpu).head = NULL;
		per_cpu(tasklet_hi_vec, cpu).tail = &per_cpu(tasklet_hi_vec, cpu).head;
	}
	raise_softirq_irqoff(HI_SOFTIRQ);

	local_irq_enable();
	return 0;
}
#else
//#define takeover_tasklets	NULL
#endif /* CONFIG_HOTPLUG_CPU */

/**
 *  ksoftirqd 线程
 *  软中断线程化
 */
static struct smp_hotplug_thread softirq_threads = {
	softirq_threads.store			= &ksoftirqd,
	/**
	 *
	 */
	softirq_threads.thread_should_run	= ksoftirqd_should_run,
	/**
	 *  回调函数
	 */
	softirq_threads.thread_fn		= run_ksoftirqd,
	/**
	 *  进程名
	 */
	softirq_threads.thread_comm		= "ksoftirqd/%u",
};

/**
 *  启动 ksoftirqd 进程，每个 CPU 一个
 */
static __init int spawn_ksoftirqd(void)
{
	cpuhp_setup_state_nocalls(CPUHP_SOFTIRQ_DEAD, "softirq:dead", NULL, takeover_tasklets);
	BUG_ON(smpboot_register_percpu_thread(&softirq_threads));

	return 0;
}
early_initcall(spawn_ksoftirqd);

/*
 * [ These __weak aliases are kept in a separate compilation unit, so that
 *   GCC does not inline them incorrectly. ]
 */

//int __init __weak early_irq_init(void)
//{
//	return 0;
//}
//
//int __init __weak arch_probe_nr_irqs(void)
//{
//	return NR_IRQS_LEGACY;
//}
//
//int __init __weak arch_early_irq_init(void)
//{
//	return 0;
//}
//
//unsigned int __weak arch_dynirq_lower_bound(unsigned int from)
//{
//	return from;
//}
