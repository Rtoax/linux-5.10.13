// SPDX-License-Identifier: GPL-2.0
/*
 * Per Entity Load Tracking
 *
 *  Copyright (C) 2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 *  Interactivity improvements by Mike Galbraith
 *  (C) 2007 Mike Galbraith <efault@gmx.de>
 *
 *  Various enhancements by Dmitry Adamushko.
 *  (C) 2007 Dmitry Adamushko <dmitry.adamushko@gmail.com>
 *
 *  Group scheduling enhancements by Srivatsa Vaddagiri
 *  Copyright IBM Corporation, 2007
 *  Author: Srivatsa Vaddagiri <vatsa@linux.vnet.ibm.com>
 *
 *  Scaled math optimizations by Thomas Gleixner
 *  Copyright (C) 2007, Thomas Gleixner <tglx@linutronix.de>
 *
 *  Adaptive scheduling granularity, math enhancements by Peter Zijlstra
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra
 *
 *  Move PELT related code from fair.c into this pelt.c file
 *  Author: Vincent Guittot <vincent.guittot@linaro.org>
 *
 * 3.8版本之前的内核CFS调度器在计算CPU load的时候采用的是跟踪每个运行队列上的负载
 * （per-rq load tracking）。它并没有跟踪每一个任务的负载和利用率，只是关注整体CPU的负载。
 * 这种粗略的负载跟踪算法显然无法为调度算法提供足够的支撑。
 * 为了完美的满足上面的所有需求，Linux调度器在3.8版中引入了PELT（Per-entity load
 * tracking）算法。
 *
 * 只有当内核准确地推测出每个进程对系统的需求，它才能最佳地完成调度任务。
 * 进程对CPU的需求包括两个方面：
 *
 * 1. 任务的利用率（task utility）
 * 2. 任务的负载（task load）
 */

#include <linux/sched.h>
#include "sched.h"
#include "pelt.h"

/*
 * Approximate:
 *   val * y^n,    where y^32 ~= 0.5 (~1 scheduling period)
 *
 * 计算第 n 个周期的衰减值
 */
static u64 decay_load(u64 val, u64 n)
{
	unsigned int local_n;

	/**
	 *  如果时间 > 2016ms
	 */
	if (unlikely(n > LOAD_AVG_PERIOD * 63/* 63*32ms = 2016ms */))
		return 0;

	/* after bounds checking we can collapse to 32-bit */
	local_n = n;

	/*
	 * As y^PERIOD = 1/2, we can combine
	 *    y^n = 1/2^(n/PERIOD) * y^(n%PERIOD)
	 * With a look-up table which covers y^n (n<PERIOD)
	 *
	 * To achieve constant time decay_load.
	 *
	 * 如果在 32ms - 2016ms 之间，每增加 32ms 就要 衰减 1/2 (右移)
	 */
	if (unlikely(local_n >= LOAD_AVG_PERIOD/*32*/)) {
		val >>= local_n / LOAD_AVG_PERIOD;
		local_n %= LOAD_AVG_PERIOD;
	}

	/**
	 *  此时 val < 32
	 */
	/**
	 * 内核提供了一张表来避免浮点运算
	 *
	 *  val = (val*runnable_avg_yN_inv[local_n]>>32)
	 *
	 *  例
	 *  (100*runnable_avg_yN_inv[31]>>32) = 51
	 */
	val = mul_u64_u32_shr(val, runnable_avg_yN_inv[local_n], 32);
	return val;
}

/**
 *      d0   d1          d2           d3
 *       ^   ^           ^            ^
 *       |   |           |            |
 *     |<->|<->|<----------------->|<--->|
 * ... |---x---|------| ... |------|-----x (now)
 *
 *                           p-1
 * u' = (u + d1) y^p + 1024 \Sum y^n + d3 y^0
 *                           n=1
 *
 *    = u y^p +					(Step 1)
 *
 *                     p-1
 *      d1 y^p + 1024 \Sum y^n + d3 y^0		(Step 2)
 *                     n=1
 *
 * 计算工作负载
 */
static u32 __accumulate_pelt_segments(u64 periods, u32 d1, u32 d3)
{
	u32 c1, c2, c3 = d3; /* y^0 == 1 */

	/*
	 * c1 = d1 y^p
	 *
	 * 计算第 n 个周期的衰减值
	 */
	c1 = decay_load((u64)d1, periods);

	/*
	 *            p-1
	 * c2 = 1024 \Sum y^n
	 *            n=1
	 *
	 *              inf        inf
	 *    = 1024 ( \Sum y^n - \Sum y^n - y^0 )
	 *              n=0        n=p
	 */
	c2 = LOAD_AVG_MAX - decay_load(LOAD_AVG_MAX, periods) - 1024;

	return c1 + c2 + c3;
}

/**
 * PELT, Per-entity load tracking。在Linux引入PELT之前，CFS调度器在计算CPU
 * 负载时，通过跟踪每个运行队列上的负载来计算；在引入PELT之后，通过跟踪每个调度实体
 * 的负载贡献来计算。（其中，调度实体：指task或task_group）。
 *
 * 总体的计算思路：将调度实体的可运行状态时间（正在运行+等待CPU调度运行），按1024us
 * 划分成不同的周期，计算每个周期内该调度实体对系统负载的贡献，最后完成累加。其中，每
 * 个计算周期，随着时间的推移，需要乘以衰减因子y进行一次衰减操作。
 *
 *
 * Accumulate the three separate parts of the sum; d1 the remainder
 * of the last (incomplete) period, d2 the span of full periods and d3
 * the remainder of the (incomplete) current period.
 *
 *      d0   d1          d2           d3
 *       ^   ^           ^            ^
 *       |   |           |            |
 *     |<->|<->|<----------------->|<--->|
 * ... |---x---|------| ... |------|-----x (now)
 *
 *                           p-1
 * u' = (u + d1) y^p + 1024 \Sum y^n + d3 y^0
 *                           n=1
 *
 *    = u y^p +					(Step 1)
 *
 *                     p-1
 *      d1 y^p + 1024 \Sum y^n + d3 y^0		(Step 2)
 *                     n=1
 *
 * 计算工作负载
 *
 * * 当前时间点的负载贡献 = 当前时间点负载 + 上个周期负载贡献 * 衰减因子；
 * * 假设一个调度实体被调度运行，运行时间段可以分成三个段d1/d2/d3，这三个段是被1024us
 *   的计算周期分割而成，period_contrib是调度实体last_update_time时在计算周期间的
 *   贡献值
 * * 总体的贡献值，也是根据d1/d2/d3来分段计算，最终相加即可；
 * * y为衰减因子，每隔1024us就乘以y来衰减一次；
 */
static __always_inline u32
accumulate_sum(u64 delta, struct sched_avg *sa,
	       unsigned long load, unsigned long runnable, int running)
{
	u32 contrib = (u32)delta; /* p == 0 -> delta < 1024 */
	u64 periods;

	/**
	 * 累加上个调度实体上次更新到本次整数周期内的剩余贡献值，再计算是否跨 1024
	 * 的周期，用于分段处理。
	 *
	 *      d0   d1          d2           d3
	 *       ^   ^           ^            ^
	 *       |   |           |            |
	 *     |<->|<->|<----------------->|<--->|
	 * ... |---x---|------| ... |------|-----x (now)
	 *         |                             |
	 *         |<---------- delta ---------->|
	 *     |<-------------- delta + d0 ----->|
	 *
	 *     |<------------------------->|       periods 记录个数
	 *
	 *  d0: period_contrib 存放上一次周期总数不能凑成一个周期（1024us）的
	 *      剩余时间
	 */
	delta += sa->period_contrib;

	/**
	 *  periods 表示有多少个完整的周期
	 *  上图中 d2 所占的整周期数
	 *
	 *  这里是：纳秒 转 微秒
	 *
	 *  ? 这里为什么不用移位操作？
	 *  经过我的测试，移位和除法的耗时基本相同，甚至比除法还要多 1/1000
	 */
	periods = delta / 1024; /* A period is 1024us (~1ms) */

	/*
	 * Step 1: decay old *_sum if we crossed period boundaries.
	 */
	if (periods) {
		/**
		 * 计算第 n 个周期的衰减值
		 * load_sum = (load_sum * runnable_avg_yN_inv[31] >> 32)
		 */
		sa->load_sum = decay_load(sa->load_sum, periods);

		/**
		 *
		 */
		sa->runnable_sum = decay_load(sa->runnable_sum, periods);

		/**
		 * 总算力
		 */
		sa->util_sum = decay_load((u64)(sa->util_sum), periods);

		/*
		 * Step 2
		 *
		 *      d0   d1          d2           d3
		 *       ^   ^           ^            ^
		 *       |   |           |            |
		 *     |<->|<->|<----------------->|<--->|
		 * ... |---x---|------| ... |------|-----x (now)
		 *
		 *     |<-------------- delta ---------->|
		 *                                 |<--->| delta
		 *
		 *  d3: 这将用 period_contrib 记录，下次计算时当作“上个周期”的 d0 用
		 */
		delta %= 1024;
		if (load) {
			/*
			 * This relies on the:
			 *
			 * if (!load)
			 *	runnable = running = 0;
			 *
			 * clause from ___update_load_sum(); this results in
			 * the below usage of @contrib to dissapear entirely,
			 * so no point in calculating it.
			 *
			 * 将 d1/d2/d3 阶段的贡献值进行相加计算
			 *
			 * d1: 1024 - sa->period_contrib
			 * d2: periods
			 * d3: delta
			 */
			contrib = __accumulate_pelt_segments(periods, 1024 - sa->period_contrib, delta);
		}
	}
	/**
	 *      d0   d1          d2           d3
	 *       ^   ^           ^            ^
	 *       |   |           |            |
	 *     |<->|<->|<----------------->|<--->|
	 * ... |---x---|------| ... |------|-----x (now)
	 *
	 *                                 |<--->| delta
	 *
	 *  d3: period_contrib 存放上一次周期总数不能凑成一个周期（1024us）的
	 *      剩余时间
	 *  对于上个周期，对应 d0
	 */
	sa->period_contrib = delta;

	/**
	 *
	 */
	if (load)
		sa->load_sum += load * contrib;

	/**
	 *
	 */
	if (runnable)
		sa->runnable_sum += runnable * contrib << SCHED_CAPACITY_SHIFT;

	/**
	 *
	 */
	if (running)
		sa->util_sum += contrib << SCHED_CAPACITY_SHIFT;

	return periods;
}

/**
 * PELT 的核心函数
 *
 * We can represent the historical contribution to runnable average as the
 * coefficients of a geometric series.  To do this we sub-divide our runnable
 * history into segments of approximately 1ms (1024us); label the segment that
 * occurred N-ms ago p_N, with p_0 corresponding to the current period, e.g.
 *
 * [<- 1024us ->|<- 1024us ->|<- 1024us ->| ...
 *      p0            p1           p2
 *     (now)       (~1ms ago)  (~2ms ago)
 *
 * Let u_i denote the fraction of p_i that the entity was runnable.
 *
 * We then designate the fractions u_i as our co-efficients, yielding the
 * following representation of historical load:
 *   u_0 + u_1*y + u_2*y^2 + u_3*y^3 + ...
 *
 * We choose y based on the with of a reasonably scheduling period, fixing:
 *   y^32 = 0.5
 *
 * This means that the contribution to load ~32ms ago (u_32) will be weighted
 * approximately half as much as the contribution to load within the last ms
 * (u_0).
 *
 * When a period "rolls over" and we have new u_0`, multiplying the previous
 * sum again by y is sufficient to update:
 *   load_avg = u_0` + y*(u_0 + u_1*y + u_2*y^2 + ... )
 *            = u_0 + u_1*y + u_2*y^2 + ... [re-labeling u_i --> u_{i+1}]
 *
 * 计算 工作 负载 之和
 *
 * 实例：
 * $ sudo bpftrace -e 'kprobe:__update_load_avg_se {printf("now = %ld\n", arg0);}'
 * now = 6210331474673
 * now = 6210331478245
 * ...
 */
static __always_inline int
___update_load_sum(u64 now, struct sched_avg *sa,
		  unsigned long load, unsigned long runnable, int running)
{
	u64 delta;

	/**
	 * 计算当前点到上次更新的时间差
	 *
	 *      d0   d1          d2           d3
	 *       ^   ^           ^            ^
	 *       |   |           |            |
	 *     |<->|<->|<----------------->|<--->|
	 * ... |---x---|------| ... |------|-----x (now)
	 *
	 *     |<---------- delta -------------->|
	 */
	delta = now - sa->last_update_time;

	/*
	 * This should only happen when time goes backwards, which it
	 * unfortunately does during sched clock init when we swap over to TSC.
	 *
	 * 判断 TSC 时钟发生溢出
	 */
	if ((s64)delta < 0) {
		sa->last_update_time = now;
		return 0;
	}

	/*
	 * Use 1024ns as the unit of measurement since it's a reasonable
	 * approximation of 1us and fast to compute.
	 *
	 * 计算有多少个 1024ns 周期 ，
	 * 用 1024ns 近似为 1us ，使用 移位操作加快计算
	 *
	 *      d0   d1          d2           d3
	 *       ^   ^           ^            ^
	 *       |   |           |            |
	 *     |<->|<->|<----------------->|<--->|
	 * ... |---x---|------| ... |------|-----x (now)
	 *
	 *     |<---------- delta -------->|
	 *
	 * 这里是：纳秒 转 微秒
	 */
	delta >>= 10;
	if (!delta)
		return 0;

	/**
	 *      d0   d1          d2           d3
	 *       ^   ^           ^            ^
	 *       |   |           |            |
	 *     |<->|<->|<----------------->|<--->|
	 * ... |---x---|------| ... |------|-----x (now)
	 *
	 *     |<---------- delta -------->|
	 *     |-------------------------->|
	 *     ^                           ^
	 *                          last_update_time
	 */
	sa->last_update_time += delta << 10;

	/*
	 * running is a subset of runnable (weight) so running can't be set if
	 * runnable is clear. But there are some corner cases where the current
	 * se has been already dequeued but cfs_rq->curr still points to it.
	 * This means that weight will be 0 but not running for a sched_entity
	 * but also for a cfs_rq if the latter becomes idle. As an example,
	 * this happens during idle_balance() which calls
	 * update_blocked_averages().
	 *
	 * Also see the comment in accumulate_sum().
	 */
	if (!load)
		runnable = running = 0;

	/*
	 * Now we know we crossed measurement unit boundaries. The *_avg
	 * accrues by two steps:
	 *
	 * 现在我们知道我们跨越了测量单位的界限。*_avg 通过两个步骤累积：
	 *
	 * Step 1: accumulate *_sum since last_update_time. If we haven't
	 * crossed period boundaries, finish.
	 *
	 * 第 1 步：自 last_update_time 以来累积 *_sum。如果我们没有跨越周期界限，
	 * 直接完成。
	 *
	 * 计算 工作负载
	 */
	if (!accumulate_sum(delta, sa, load, runnable, running))
		return 0;

	return 1;
}

/*
 * When syncing *_avg with *_sum, we must take into account the current
 * position in the PELT segment otherwise the remaining part of the segment
 * will be considered as idle time whereas it's not yet elapsed and this will
 * generate unwanted oscillation in the range [1002..1024[.
 *
 * The max value of *_sum varies with the position in the time segment and is
 * equals to :
 *
 *   LOAD_AVG_MAX*y + sa->period_contrib
 *
 * which can be simplified into:
 *
 *   LOAD_AVG_MAX - 1024 + sa->period_contrib
 *
 * because LOAD_AVG_MAX*y == LOAD_AVG_MAX-1024
 *
 * The same care must be taken when a sched entity is added, updated or
 * removed from a cfs_rq and we need to update sched_avg. Scheduler entities
 * and the cfs rq, to which they are attached, have the same position in the
 * time segment because they use the same clock. This means that we can use
 * the period_contrib of cfs_rq when updating the sched_avg of a sched_entity
 * if it's more convenient.
 *
 * 计算工作负载 之和
 */
static __always_inline void
___update_load_avg(struct sched_avg *sa, unsigned long load)
{
	/**
	 * LOAD_AVG_MAX=47742
	 * divider = LOAD_AVG_MAX - 1024 + avg->period_contrib;
	 */
	u32 divider = get_pelt_divider(sa);

	/*
	 * Step 2: update *_avg.
	 *
	 */
	sa->load_avg = div_u64(load * sa->load_sum, divider);
	sa->runnable_avg = div_u64(sa->runnable_sum, divider);
	WRITE_ONCE(sa->util_avg, sa->util_sum / divider);
}

/*
 * sched_entity:
 *
 *   task:
 *     se_weight()   = se->load.weight
 *     se_runnable() = !!on_rq
 *
 *   group: [ see update_cfs_group() ]
 *     se_weight()   = tg->weight * grq->load_avg / tg->load_avg
 *     se_runnable() = grq->h_nr_running
 *
 *   runnable_sum = se_runnable() * runnable = grq->runnable_sum
 *   runnable_avg = runnable_sum
 *
 *   load_sum := runnable
 *   load_avg = se_weight(se) * load_sum
 *
 * cfq_rq:
 *
 *   runnable_sum = \Sum se->avg.runnable_sum
 *   runnable_avg = \Sum se->avg.runnable_avg
 *
 *   load_sum = \Sum se_weight(se) * se->avg.load_sum
 *   load_avg = \Sum se->avg.load_avg
 */

/**
 *  更新 一个调度实体在阻塞状态下 的负载信息
 */
int __update_load_avg_blocked_se(u64 now, struct sched_entity *se)
{
	if (___update_load_sum(now, &se->avg, 0, 0, 0)) {
		___update_load_avg(&se->avg, se_weight(se));
		trace_pelt_se_tp(se);
		return 1;
	}

	return 0;
}

/**
 *  更新调度实体 se 的负载信息
 *
 * $ sudo bpftrace -e 'kprobe:__update_load_avg_se {printf("now = %ld\n", arg0);}'
 * now = 6210331474673
 * now = 6210331478245
 * ...
 */
int __update_load_avg_se(u64 now, struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (___update_load_sum(now, &se->avg, !!se->on_rq, se_runnable(se),
				cfs_rq->curr == se)) {

		___update_load_avg(&se->avg, se_weight(se));
		cfs_se_util_change(&se->avg);
		trace_pelt_se_tp(se);
		return 1;
	}

	return 0;
}

/**
 *  更新 CFS 就绪队列负载信息
 */
int __update_load_avg_cfs_rq(u64 now, struct cfs_rq *cfs_rq)
{
	if (___update_load_sum(now, &cfs_rq->avg,
				scale_load_down(cfs_rq->load.weight),
				cfs_rq->h_nr_running,
				cfs_rq->curr != NULL)) {

		___update_load_avg(&cfs_rq->avg, 1);
		trace_pelt_cfs_tp(cfs_rq);
		return 1;
	}

	return 0;
}

/*
 * rt_rq:
 *
 *   util_sum = \Sum se->avg.util_sum but se->avg.util_sum is not tracked
 *   util_sum = cpu_scale * load_sum
 *   runnable_sum = util_sum
 *
 *   load_avg and runnable_avg are not supported and meaningless.
 *
 */

int update_rt_rq_load_avg(u64 now, struct rq *rq, int running)
{
	if (___update_load_sum(now, &rq->avg_rt,
				running,
				running,
				running)) {

		___update_load_avg(&rq->avg_rt, 1);
		trace_pelt_rt_tp(rq);
		return 1;
	}

	return 0;
}

/*
 * dl_rq:
 *
 *   util_sum = \Sum se->avg.util_sum but se->avg.util_sum is not tracked
 *   util_sum = cpu_scale * load_sum
 *   runnable_sum = util_sum
 *
 *   load_avg and runnable_avg are not supported and meaningless.
 *
 */

int update_dl_rq_load_avg(u64 now, struct rq *rq, int running)
{
	if (___update_load_sum(now, &rq->avg_dl,
				running,
				running,
				running)) {

		___update_load_avg(&rq->avg_dl, 1);
		trace_pelt_dl_tp(rq);
		return 1;
	}

	return 0;
}

#ifdef CONFIG_SCHED_THERMAL_PRESSURE
/*
 * thermal:
 *
 *   load_sum = \Sum se->avg.load_sum but se->avg.load_sum is not tracked
 *
 *   util_avg and runnable_load_avg are not supported and meaningless.
 *
 * Unlike rt/dl utilization tracking that track time spent by a cpu
 * running a rt/dl task through util_avg, the average thermal pressure is
 * tracked through load_avg. This is because thermal pressure signal is
 * time weighted "delta" capacity unlike util_avg which is binary.
 * "delta capacity" =  actual capacity  -
 *			capped capacity a cpu due to a thermal event.
 */

int update_thermal_load_avg(u64 now, struct rq *rq, u64 capacity)
{
	if (___update_load_sum(now, &rq->avg_thermal,
			       capacity,
			       capacity,
			       capacity)) {
		___update_load_avg(&rq->avg_thermal, 1);
		trace_pelt_thermal_tp(rq);
		return 1;
	}

	return 0;
}
#endif

#ifdef CONFIG_HAVE_SCHED_AVG_IRQ
/*
 * irq:
 *
 *   util_sum = \Sum se->avg.util_sum but se->avg.util_sum is not tracked
 *   util_sum = cpu_scale * load_sum
 *   runnable_sum = util_sum
 *
 *   load_avg and runnable_avg are not supported and meaningless.
 *
 */

int update_irq_load_avg(struct rq *rq, u64 running)
{
	int ret = 0;

	/*
	 * We can't use clock_pelt because irq time is not accounted in
	 * clock_task. Instead we directly scale the running time to
	 * reflect the real amount of computation
	 */
	running = cap_scale(running, arch_scale_freq_capacity(cpu_of(rq)));
	running = cap_scale(running, arch_scale_cpu_capacity(cpu_of(rq)));

	/*
	 * We know the time that has been used by interrupt since last update
	 * but we don't when. Let be pessimistic and assume that interrupt has
	 * happened just before the update. This is not so far from reality
	 * because interrupt will most probably wake up task and trig an update
	 * of rq clock during which the metric is updated.
	 * We start to decay with normal context time and then we add the
	 * interrupt context time.
	 * We can safely remove running from rq->clock because
	 * rq->clock += delta with delta >= running
	 */
	ret = ___update_load_sum(rq->clock - running, &rq->avg_irq,
				0,
				0,
				0);
	ret += ___update_load_sum(rq->clock, &rq->avg_irq,
				1,
				1,
				1);

	if (ret) {
		___update_load_avg(&rq->avg_irq, 1);
		trace_pelt_irq_tp(rq);
	}

	return ret;
}
#endif
