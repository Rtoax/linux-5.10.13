/* SPDX-License-Identifier: GPL-2.0 */

#ifdef CONFIG_SCHEDSTATS

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_arrive(struct rq *rq, unsigned long long delta)
{
	if (rq) {
		rq->rq_sched_info.run_delay += delta;
		rq->rq_sched_info.pcount++;
	}
}

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_depart(struct rq *rq, unsigned long long delta)
{
	if (rq)
		rq->rq_cpu_time += delta;
}

static inline void
rq_sched_info_dequeued(struct rq *rq, unsigned long long delta)
{
	if (rq)
		rq->rq_sched_info.run_delay += delta;
}
//echo 1 > /proc/sys/kernel/sched_schedstats
#define   schedstat_enabled()		static_branch_unlikely(&sched_schedstats)

#define __schedstat_inc(var)		do { var++; } while (0)
#define   schedstat_inc(var)		do { if (schedstat_enabled()) { var++; } } while (0)
#define __schedstat_add(var, amt)	do { var += (amt); } while (0)
#define   schedstat_add(var, amt)	do { if (schedstat_enabled()) { var += (amt); } } while (0)
#define __schedstat_set(var, val)	do { var = (val); } while (0)
#define   schedstat_set(var, val)	do { if (schedstat_enabled()) { var = (val); } } while (0)
#define   schedstat_val(var)		(var)
#define   schedstat_val_or_zero(var)	((schedstat_enabled()) ? (var) : 0)

#else /* !CONFIG_SCHEDSTATS: */
/**
     *  
     */
#endif /* CONFIG_SCHEDSTATS */

#ifdef CONFIG_PSI
/*
 * PSI tracks state that persists across sleeps, such as iowaits and
 * memory stalls. As a result, it has to distinguish between sleeps,
 * where a task's runnable state changes, and requeues, where a task
 * and its state are being moved between CPUs and runqueues.
 */
static inline void psi_enqueue(struct task_struct *p, bool wakeup)
{
	int clear = 0, set = TSK_RUNNING;

	if (static_branch_likely(&psi_disabled))
		return;

	if (!wakeup || p->sched_psi_wake_requeue) {
		if (p->in_memstall)
			set |= TSK_MEMSTALL;
		if (p->sched_psi_wake_requeue)
			p->sched_psi_wake_requeue = 0;
	} else {
		if (p->in_iowait)
			clear |= TSK_IOWAIT;
	}

	psi_task_change(p, clear, set);
}

static inline void psi_dequeue(struct task_struct *p, bool sleep)
{
	int clear = TSK_RUNNING, set = 0;

	if (static_branch_likely(&psi_disabled))
		return;

	if (!sleep) {
		if (p->in_memstall)
			clear |= TSK_MEMSTALL;
	} else {
		/*
		 * When a task sleeps, schedule() dequeues it before
		 * switching to the next one. Merge the clearing of
		 * TSK_RUNNING and TSK_ONCPU to save an unnecessary
		 * psi_task_change() call in psi_sched_switch().
		 */
		clear |= TSK_ONCPU;

		if (p->in_iowait)
			set |= TSK_IOWAIT;
	}

	psi_task_change(p, clear, set);
}

/**
 *
 */
static inline void psi_ttwu_dequeue(struct task_struct *p)
{
	/**
	 * Pressure Stall Information - PSI??
	 */
	if (static_branch_likely(&psi_disabled))
		return;
	/*
	 * Is the task being migrated during a wakeup? Make sure to
	 * deregister its sleep-persistent psi states from the old
	 * queue, and let psi_enqueue() know it has to requeue.
	 */
	if (unlikely(p->in_iowait || p->in_memstall)) {
		struct rq_flags rf;
		struct rq *rq;
		int clear = 0;

		if (p->in_iowait)
			clear |= TSK_IOWAIT;
		if (p->in_memstall)
			clear |= TSK_MEMSTALL;

		rq = __task_rq_lock(p, &rf);
		psi_task_change(p, clear, 0);
		p->sched_psi_wake_requeue = 1;
		__task_rq_unlock(rq, &rf);
	}
}

static inline void psi_sched_switch(struct task_struct *prev,
				    struct task_struct *next,
				    bool sleep)
{
	if (static_branch_likely(&psi_disabled))
		return;

	psi_task_switch(prev, next, sleep);
}

static inline void psi_task_tick(struct rq *rq)
{
	if (static_branch_likely(&psi_disabled))
		return;

	if (unlikely(rq->curr->in_memstall))
		psi_memstall_tick(rq->curr, cpu_of(rq));
}
#else /* CONFIG_PSI */
static inline void psi_enqueue(struct task_struct *p, bool wakeup) {}
static inline void psi_dequeue(struct task_struct *p, bool sleep) {}
static inline void psi_ttwu_dequeue(struct task_struct *p) {}
static inline void psi_sched_switch(struct task_struct *prev,
				    struct task_struct *next,
				    bool sleep) {}
static inline void psi_task_tick(struct rq *rq) {}
#endif /* CONFIG_PSI */

#ifdef CONFIG_SCHED_INFO
static inline void sched_info_reset_dequeued(struct task_struct *t)
{
	t->sched_info.last_queued = 0;
}

/*
 * We are interested in knowing how long it was from the *first* time a
 * task was queued to the time that it finally hit a CPU, we call this routine
 * from dequeue_task() to account for possible rq->clock skew across CPUs. The
 * delta taken on each CPU would annul the skew.
 */
static inline void sched_info_dequeued(struct rq *rq, struct task_struct *t)
{
	unsigned long long now = rq_clock(rq), delta = 0;

	if (sched_info_on()) {
		if (t->sched_info.last_queued)
			delta = now - t->sched_info.last_queued;
	}
	sched_info_reset_dequeued(t);
	t->sched_info.run_delay += delta;

	rq_sched_info_dequeued(rq, delta);
}

/*
 * Called when a task finally hits the CPU.  We can now calculate how
 * long it was waiting to run.  We also note when it began so that we
 * can keep stats on how long its timeslice is.
 */
static void sched_info_arrive(struct rq *rq, struct task_struct *t)
{
	unsigned long long now = rq_clock(rq), delta = 0;

	/**
	 * 当task被调度到的时候，因为记录了它的上一次被动切换的时间，即可计算出
	 * 来这段时间差，就算出来了这段run delay的时间。
	 */
	if (t->sched_info.last_queued)
		delta = now - t->sched_info.last_queued;
	sched_info_reset_dequeued(t);
	/**
	 * 内核把一个task被切换走，要么是task主动发起的，要么是被动的。如果task
	 * 依然是RUNNING状态的话，那么它必然是被动切换走的：即task还想继续运行，
	 * 但是没有得到调度。那么到达下一次执行的时间间隔就是run delay。
	 */
	t->sched_info.run_delay += delta;
	t->sched_info.last_arrival = now;
	t->sched_info.pcount++;

	rq_sched_info_arrive(rq, delta);
}

/*
 * This function is only called from enqueue_task(), but also only updates
 * the timestamp if it is already not set.  It's assumed that
 * sched_info_dequeued() will clear that stamp when appropriate.
 */
static inline void sched_info_queued(struct rq *rq, struct task_struct *t)
{
	if (sched_info_on()) {
		if (!t->sched_info.last_queued)
			/**
			 * 记录它的last_queued的时间戳。
			 */
			t->sched_info.last_queued = rq_clock(rq);
	}
}

/*
 * Called when a process ceases being the active-running process involuntarily
 * due, typically, to expiring its time slice (this may also be called when
 * switching to the idle task).  Now we can calculate how long we ran.
 * Also, if the process is still in the TASK_RUNNING state, call
 * sched_info_queued() to mark that it has now again started waiting on
 * the runqueue.
 */
static inline void sched_info_depart(struct rq *rq, struct task_struct *t)
{
	unsigned long long delta = rq_clock(rq) - t->sched_info.last_arrival;

	rq_sched_info_depart(rq, delta);

	/**
	 * 在一个task被切换走的时候，它依然是RUNNING状态的话，那么就会记录它的
	 * last_queued的时间戳。
	 *
	 * 内核把一个task被切换走，要么是task主动发起的，要么是被动的。如果task
	 * 依然是RUNNING状态的话，那么它必然是被动切换走的：即task还想继续运行，
	 * 但是没有得到调度。那么到达下一次执行的时间间隔就是run delay。
	 */
	if (t->state == TASK_RUNNING)
		sched_info_queued(rq, t);
}

/*
 * Called when tasks are switched involuntarily due, typically, to expiring
 * their time slice.  (This may also be called when switching to or from
 * the idle task.)  We are only called when prev != next.
 */
static inline void
__sched_info_switch(struct rq *rq, struct task_struct *prev, struct task_struct *next)
{
	/*
	 * prev now departs the CPU.  It's not interesting to record
	 * stats about how efficient we were at scheduling the idle
	 * process, however.
	 */
	if (prev != rq->idle)
		sched_info_depart(rq, prev);

	if (next != rq->idle)
		sched_info_arrive(rq, next);
}

static inline void
sched_info_switch(struct rq *rq, struct task_struct *prev, struct task_struct *next)
{
	if (sched_info_on())
		__sched_info_switch(rq, prev, next);
}

#else /* !CONFIG_SCHED_INFO: */
/**
     *  
     */
#endif /* CONFIG_SCHED_INFO */
