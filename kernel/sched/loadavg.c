// SPDX-License-Identifier: GPL-2.0
/*
 * kernel/sched/loadavg.c
 *
 * This file contains the magic bits required to compute the global loadavg
 * figure. Its a silly number but people think its important. We go through
 * great pains to make it work on big machines and tickless kernels.
 */
#include "sched.h"

/*
 * Global load-average calculations
 *
 * We take a distributed and async approach to calculating the global load-avg
 * in order to minimize overhead.
 *
 * The global load average is an exponentially decaying average of nr_running +
 * nr_uninterruptible.
 *
 * load average = nr_running + nr_uninterruptible
 *
 * Once every LOAD_FREQ:
 *
 *   nr_active = 0;
 *   for_each_possible_cpu(cpu)
 *	nr_active += cpu_of(cpu)->nr_running + cpu_of(cpu)->nr_uninterruptible;
 *
 *   avenrun[n] = avenrun[0] * exp_n + nr_active * (1 - exp_n)
 *
 * Due to a number of reasons the above turns in the mess below:
 *
 *  - for_each_possible_cpu() is prohibitively expensive on machines with
 *    serious number of CPUs, therefore we need to take a distributed approach
 *    to calculating nr_active.
 *
 *        \Sum_i x_i(t) = \Sum_i x_i(t) - x_i(t_0) | x_i(t_0) := 0
 *                      = \Sum_i { \Sum_j=1 x_i(t_j) - x_i(t_j-1) }
 *
 *    So assuming nr_active := 0 when we start out -- true per definition, we
 *    can simply take per-CPU deltas and fold those into a global accumulate
 *    to obtain the same result. See calc_load_fold_active().
 *
 *    Furthermore, in order to avoid synchronizing all per-CPU delta folding
 *    across the machine, we assume 10 ticks is sufficient time for every
 *    CPU to have completed this task.
 *
 *    This places an upper-bound on the IRQ-off latency of the machine. Then
 *    again, being late doesn't loose the delta, just wrecks the sample.
 *
 *  - cpu_rq()->nr_uninterruptible isn't accurately tracked per-CPU because
 *    this would add another cross-CPU cacheline miss and atomic operation
 *    to the wakeup path. Instead we increment on whatever CPU the task ran
 *    when it went into uninterruptible state and decrement on whatever CPU
 *    did the wakeup. This means that only the sum of nr_uninterruptible over
 *    all CPUs yields the correct result.
 *
 *  This covers the NO_HZ=n code, for extra head-aches, see the comment below.
 */

/* Variables and functions for calc_load */
/**
 * 新增的活动任务更新到全局变量 calc_load_tasks 中
 *
 * 见 calc_global_load_tick()
 */
atomic_long_t calc_load_tasks;
unsigned long calc_load_update;

/**
 * 存放最近1/5/15分钟的平均CPU负载
 *
 * 计算CPU负载，可以让调度器更好的进行负载均衡处理，以便提高系统的运行效率。
 *
 * --------------------------------------------------------------------------
 * * CPU的运行能力，就如大桥的通行能力，分别有满负荷，非满负荷，超负荷等状态，这几种状态对
 *    应不同的cpu load值；
 * * 单CPU满负荷运行时cpu_load为1，当多个CPU或多核时，相当于大桥有多个车道，满负荷运行时
 *    cpu_load值为CPU数或多核数；
 * * CPU负载的计算（以单CPU为例），假设一分钟内执行10个任务代表满负荷，当一分钟给出30个任
 *    务时，CPU只能处理10个，剩余20个不能处理，cpu_load=3；
 */
unsigned long avenrun[3];   /* 为啥没有加锁？ 一分钟，五分钟，十五分钟的平均负载 */
EXPORT_SYMBOL(avenrun); /* should be removed */

/**
 * get_avenrun - get the load average array
 * @loads:	pointer to dest load array
 * @offset:	offset to add
 * @shift:	shift count to shift the result left
 *
 * These values are estimates at best, so no need for locking.
 *
 * 1. /proc/loadavg loadavg_proc_show() 中的调用
 *
 *   get_avenrun(avnrun, FIXED_1/200, 0);
 */
void get_avenrun(unsigned long *loads, unsigned long offset, int shift)
{
	/**
	 *
	 */
	loads[0] = (avenrun[0] + offset) << shift;
	loads[1] = (avenrun[1] + offset) << shift;
	loads[2] = (avenrun[2] + offset) << shift;
}

/**
 * 统计新增的活动任务，包含 running 和 uninterruptible
 */
long calc_load_fold_active(struct rq *this_rq, long adjust)
{
	long nr_active, delta = 0;

	/**
	 * 正在运行进程数 - 调控
	 */
	nr_active = this_rq->nr_running - adjust;
	/**
	 * (unsigned long) -> (long) 说明可能是负数？
	 */
	nr_active += (long)this_rq->nr_uninterruptible;

	/**
	 *
	 */
	if (nr_active != this_rq->calc_load_active) {
		delta = nr_active - this_rq->calc_load_active;
		this_rq->calc_load_active = nr_active;
	}

	return delta;
}

/**
 * fixed_power_int - compute: x^n, in O(log n) time
 *
 * @x:         base of the power
 * @frac_bits: fractional bits of @x
 * @n:         power to raise @x to.
 *
 * By exploiting the relation between the definition of the natural power
 * function: x^n := x*x*...*x (x multiplied by itself for n times), and
 * the binary encoding of numbers used by computers: n := \Sum n_i * 2^i,
 * (where: n_i \elem {0, 1}, the binary vector representing n),
 * we find: x^n := x^(\Sum n_i * 2^i) := \Prod x^(n_i * 2^i), which is
 * of course trivially computable in O(log_2 n), the length of our binary
 * vector.
 */
static unsigned long
fixed_power_int(unsigned long x, unsigned int frac_bits, unsigned int n)
{
	unsigned long result = 1UL << frac_bits;

	if (n) {
		for (;;) {
			if (n & 1) {
				result *= x;
				result += 1UL << (frac_bits - 1);
				result >>= frac_bits;
			}
			n >>= 1;
			if (!n)
				break;
			x *= x;
			x += 1UL << (frac_bits - 1);
			x >>= frac_bits;
		}
	}

	return result;
}

/*
 * a1 = a0 * e + a * (1 - e)
 *
 * a2 = a1 * e + a * (1 - e)
 *    = (a0 * e + a * (1 - e)) * e + a * (1 - e)
 *    = a0 * e^2 + a * (1 - e) * (1 + e)
 *
 * a3 = a2 * e + a * (1 - e)
 *    = (a0 * e^2 + a * (1 - e) * (1 + e)) * e + a * (1 - e)
 *    = a0 * e^3 + a * (1 - e) * (1 + e + e^2)
 *
 *  ...
 *
 * an = a0 * e^n + a * (1 - e) * (1 + e + ... + e^n-1) [1]
 *    = a0 * e^n + a * (1 - e) * (1 - e^n)/(1 - e)
 *    = a0 * e^n + a * (1 - e^n)
 *
 * [1] application of the geometric series:
 *
 *              n         1 - x^(n+1)
 *     S_n := \Sum x^i = -------------
 *             i=0          1 - x
 */
unsigned long   /* 计算负载 */
calc_load_n(unsigned long load, unsigned long exp,
	    unsigned long active, unsigned int n)
{
	return calc_load(load, fixed_power_int(exp, FSHIFT, n), active);
}

#ifdef CONFIG_NO_HZ_COMMON
/*
 * Handle NO_HZ for the global load-average.
 *
 * Since the above described distributed algorithm to compute the global
 * load-average relies on per-CPU sampling from the tick, it is affected by
 * NO_HZ.
 *
 * The basic idea is to fold the nr_active delta into a global NO_HZ-delta upon
 * entering NO_HZ state such that we can include this as an 'extra' CPU delta
 * when we read the global state.
 *
 * Obviously reality has to ruin such a delightfully simple scheme:
 *
 *  - When we go NO_HZ idle during the window, we can negate our sample
 *    contribution, causing under-accounting.
 *
 *    We avoid this by keeping two NO_HZ-delta counters and flipping them
 *    when the window starts, thus separating old and new NO_HZ load.
 *
 *    The only trick is the slight shift in index flip for read vs write.
 *
 *        0s            5s            10s           15s
 *          +10           +10           +10           +10
 *        |-|-----------|-|-----------|-|-----------|-|
 *    r:0 0 1           1 0           0 1           1 0
 *    w:0 1 1           0 0           1 1           0 0
 *
 *    This ensures we'll fold the old NO_HZ contribution in this window while
 *    accumlating the new one.
 *
 *  - When we wake up from NO_HZ during the window, we push up our
 *    contribution, since we effectively move our sample point to a known
 *    busy state.
 *
 *    This is solved by pushing the window forward, and thus skipping the
 *    sample, for this CPU (effectively using the NO_HZ-delta for this CPU which
 *    was in effect at the time the window opened). This also solves the issue
 *    of having to deal with a CPU having been in NO_HZ for multiple LOAD_FREQ
 *    intervals.
 *
 * When making the ILB scale, we should try to pull this in as well.
 */
static atomic_long_t calc_load_nohz[2];
static int calc_load_idx;

/**
 * 由于 NO_HZ 空闲效应而更改的CPU活动任务数量，存放在全局变量 calc_load_nohz[2] 中，
 * 并且每 5 秒计算周期交替更换一次存储位置(calc_load_read_idx/calc_load_write_idx)，
 * 其他程序可以去读取最近 5 秒内的活动任务变化的增量值。
 */
static inline int calc_load_write_idx(void)
{
	int idx = calc_load_idx;

	/*
	 * See calc_global_nohz(), if we observe the new index, we also
	 * need to observe the new update time.
	 */
	smp_rmb();

	/*
	 * If the folding window started, make sure we start writing in the
	 * next NO_HZ-delta.
	 */
	if (!time_before(jiffies, READ_ONCE(calc_load_update)))
		idx++;

	return idx & 1;
}

/**
 * 由于 NO_HZ 空闲效应而更改的CPU活动任务数量，存放在全局变量 calc_load_nohz[2] 中，
 * 并且每 5 秒计算周期交替更换一次存储位置(calc_load_read_idx/calc_load_write_idx)，
 * 其他程序可以去读取最近 5 秒内的活动任务变化的增量值。
 */
static inline int calc_load_read_idx(void)
{
	return calc_load_idx & 1;
}

/**
 * Update loadavg from runqueue
 */
static void calc_load_nohz_fold(struct rq *rq)
{
	long delta;

	/**
	 *
	 */
	delta = calc_load_fold_active(rq, 0);
	if (delta) {
		int idx = calc_load_write_idx();

		/**
		 * + nohz 情况下保存 delta
		 */
		atomic_long_add(delta, &calc_load_nohz[idx]);
	}
}

/**
 * NO_HZ 调度开始
 * 见 tick_nohz_stop_sched_tick()
 */
void calc_load_nohz_start(void)
{
	/*
	 * We're going into NO_HZ mode, if there's any pending delta, fold it
	 * into the pending NO_HZ delta.
	 */
	calc_load_nohz_fold(this_rq());
}

/*
 * Keep track of the load for NOHZ_FULL, must be called between
 * calc_load_nohz_{start,stop}().
 */
void calc_load_nohz_remote(struct rq *rq)
{
	calc_load_nohz_fold(rq);
}

/**
 * NO_HZ 调度开始
 * 见 tick_nohz_restart_sched_tick()
 */
void calc_load_nohz_stop(void)
{
	struct rq *this_rq = this_rq();

	/*
	 * If we're still before the pending sample window, we're done.
	 */
	this_rq->calc_load_update = READ_ONCE(calc_load_update);
	if (time_before(jiffies, this_rq->calc_load_update))
		return;

	/*
	 * We woke inside or after the sample window, this means we're already
	 * accounted through the nohz accounting, so skip the entire deal and
	 * sync up for the next window.
	 */
	if (time_before(jiffies, this_rq->calc_load_update + 10))
		this_rq->calc_load_update += LOAD_FREQ;
}

/**
 * 读取 NO_HZ 的 CPU 活动任务数量
 *
 * NO_HZ 模式下活动任务数量更改的计算
 * --------------------------------------------------------------------------
 * 由于 NO_HZ 空闲效应而更改的CPU活动任务数量，存放在全局变量 calc_load_nohz[2] 中，
 * 并且每 5 秒计算周期交替更换一次存储位置(calc_load_read_idx/calc_load_write_idx)，
 * 其他程序可以去读取最近 5 秒内的活动任务变化的增量值。
 */
static long calc_load_nohz_read(void)
{
	int idx = calc_load_read_idx();
	long delta = 0;

	if (atomic_long_read(&calc_load_nohz[idx]))
		delta = atomic_long_xchg(&calc_load_nohz[idx], 0);

	return delta;
}

/*
 * NO_HZ can leave us missing all per-CPU ticks calling
 * calc_load_fold_active(), but since a NO_HZ CPU folds its delta into
 * calc_load_nohz per calc_load_nohz_start(), all we need to do is fold
 * in the pending NO_HZ delta if our NO_HZ period crossed a load cycle boundary.
 *
 * Once we've updated the global active value, we need to apply the exponential
 * weights adjusted to the number of cycles missed.
 */
static void calc_global_nohz(void)
{
	unsigned long sample_window;
	long delta, active, n;

	sample_window = READ_ONCE(calc_load_update);
	if (!time_before(jiffies, sample_window + 10)) {
		/*
		 * Catch-up, fold however many we are behind still
		 */
		delta = jiffies - sample_window - 10;
		n = 1 + (delta / LOAD_FREQ);

		active = atomic_long_read(&calc_load_tasks);
		active = active > 0 ? active * FIXED_1 : 0;

		avenrun[0] = calc_load_n(avenrun[0], EXP_1, active, n);
		avenrun[1] = calc_load_n(avenrun[1], EXP_5, active, n);
		avenrun[2] = calc_load_n(avenrun[2], EXP_15, active, n);

		WRITE_ONCE(calc_load_update, sample_window + n * LOAD_FREQ);
	}

	/*
	 * Flip the NO_HZ index...
	 *
	 * Make sure we first write the new time then flip the index, so that
	 * calc_load_write_idx() will see the new time when it reads the new
	 * index, this avoids a double flip messing things up.
	 */
	smp_wmb();
	calc_load_idx++;
}
#else /* !CONFIG_NO_HZ_COMMON */

#endif /* CONFIG_NO_HZ_COMMON */

/*
 * calc_load - update the avenrun load estimates 10 ticks after the
 * CPUs have updated calc_load_tasks.
 *
 * 确保在更新 calc_load_update 后的 10 个 tick 之后，再计算 CPU 负载
 *
 * Called from the global timer code.
 *
 * sudo bpftrace -e 'kprobe:calc_global_load {printf("---\n");}'
 */
void calc_global_load(void)
{
	unsigned long sample_window;
	long active, delta;

	/**
	 * 确保在更新 calc_load_update 后的 10 个 tick 之后，再计算 CPU 负载，否则返回。
	 *
	 * 系统默认每隔5秒钟会计算一次负载，如果由于 NO_HZ 空闲而错过了下一个CPU负载的计算周期，
	 * 则需要再次进行更新。比如 NO_HZ 空闲20秒而无法更新CPU负载，前5秒负载已经更新，需要计
	 * 算剩余的3个计算周期的负载来继续更新（见 LOAD_FREQ ）
	 */
	sample_window = READ_ONCE(calc_load_update);
	if (time_before(jiffies, sample_window + 10))
		return;

	/**
	 * 计算 NO_HZ 模式下新增的活动任务
	 * Fold the 'old' NO_HZ-delta to include all NO_HZ CPUs.
	 */
	delta = calc_load_nohz_read();
	if (delta)
		/**
		 * 将新增的活动任务更新到全局变量 calc_load_tasks
		 */
		atomic_long_add(delta, &calc_load_tasks);

	/**
	 * 读取活动任务数
	 *
	 * 活动任务数包括两部分：1）周期性调度中新增加的活动任务；2）在NO_HZ期间增加的活动任务数；
	 */
	active = atomic_long_read(&calc_load_tasks);
	/* FIXED_1 = 2048 */
	active = active > 0 ? active * FIXED_1 : 0;

	/**
	 * 根据 active 值来计算全局 CPU 负载：1,5,15 分钟
	 *
	 * 根据活动任务数值，再结合全局变量值avenrun[]中的old value，来计算新的CPU负载值，
	 * 并最终替换掉avenrun[]中的值；
	 */
	avenrun[0] = calc_load(avenrun[0], EXP_1, active);
	avenrun[1] = calc_load(avenrun[1], EXP_5, active);
	avenrun[2] = calc_load(avenrun[2], EXP_15, active);

	/**
	 * 每次计算完负载值后更新 calc_load_update 值，增加 5s 计算周期
	 * LOAD_FREQ = (5*HZ+1)  5 sec intervals
	 *
	 * 系统默认每隔5秒钟会计算一次负载，如果由于 NO_HZ 空闲而错过了下一个CPU负载的计算周期，
	 * 则需要再次进行更新。比如 NO_HZ 空闲20秒而无法更新CPU负载，前5秒负载已经更新，需要计
	 * 算剩余的3个计算周期的负载来继续更新；
	 */
	WRITE_ONCE(calc_load_update, sample_window + LOAD_FREQ);

	/**
	 * In case we went to NO_HZ for multiple LOAD_FREQ intervals
	 * catch up in bulk.
	 *
	 * 若 NO_HZ 模式空闲超过 5s 的计算周期，则需要进行折算并再次更新 CPU 负载
	 */
	calc_global_nohz();
}

/*
 * Called from scheduler_tick() to periodically update this CPU's
 * active count.
 *
 * 从 scheduler_tick（） 调用以定期更新此 CPU 的活动计数。
 */
void calc_global_load_tick(struct rq *this_rq)
{
	long delta;

	if (time_before(jiffies, this_rq->calc_load_update))
		return;

	/**
	 * 统计新增的活动任务，包含 running 和 uninterruptible
	 */
	delta  = calc_load_fold_active(this_rq, 0);
	if (delta)
		/**
		 * 将新增的活动任务更新到全局变量 calc_load_tasks 中
		 */
		atomic_long_add(delta, &calc_load_tasks);

	this_rq->calc_load_update += LOAD_FREQ;
}
