/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STOP_MACHINE
#define _LINUX_STOP_MACHINE

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/list.h>

/*
 * stop_cpu[s]() is simplistic per-cpu maximum priority cpu
 * monopolization mechanism.  The caller can specify a non-sleeping
 * function to be executed on a single or multiple cpus preempting all
 * other processes and monopolizing those cpus until it finishes.
 *
 * Resources for this mechanism are preallocated when a cpu is brought
 * up and requests are guaranteed to be served as long as the target
 * cpus are online.
 */
typedef int (*cpu_stop_fn_t)(void *arg);

#ifdef CONFIG_SMP

struct cpu_stop_work {
	struct list_head	list;		/* cpu_stopper->works */
	cpu_stop_fn_t		fn;
	void			*arg;
	struct cpu_stop_done	*done;
};

int stop_one_cpu(unsigned int cpu, cpu_stop_fn_t fn, void *arg);
int stop_two_cpus(unsigned int cpu1, unsigned int cpu2, cpu_stop_fn_t fn, void *arg);
bool stop_one_cpu_nowait(unsigned int cpu, cpu_stop_fn_t fn, void *arg,
			 struct cpu_stop_work *work_buf);
void stop_machine_park(int cpu);
void stop_machine_unpark(int cpu);
void stop_machine_yield(const struct cpumask *cpumask);

#else	/* CONFIG_SMP */
/**/
#endif	/* CONFIG_SMP */

/*
 * stop_machine "Bogolock": stop the entire machine, disable
 * interrupts.  This is a very heavy lock, which is equivalent to
 * grabbing every spinlock (and more).  So the "read" side to such a
 * lock is anything which disables preemption.
 */
#if defined(CONFIG_SMP) || defined(CONFIG_HOTPLUG_CPU)

/**
 * stop_machine: freeze the machine on all CPUs and run this function
 * @fn: the function to run
 * @data: the data ptr for the @fn()
 * @cpus: the cpus to run the @fn() on (NULL = any online cpu)
 *
 * Description: This causes a thread to be scheduled on every cpu,
 * each of which disables interrupts.  The result is that no one is
 * holding a spinlock or inside any other preempt-disabled region when
 * @fn() runs.
 *
 * This can be thought of as a very heavy write lock, equivalent to
 * grabbing every spinlock in the kernel.
 *
 * Protects against CPU hotplug.
 */
int stop_machine(cpu_stop_fn_t fn, void *data, const struct cpumask *cpus);

/**
 * stop_machine_cpuslocked: freeze the machine on all CPUs and run this function
 * @fn: the function to run
 * @data: the data ptr for the @fn()
 * @cpus: the cpus to run the @fn() on (NULL = any online cpu)
 *
 * Same as above. Must be called from with in a cpus_read_lock() protected
 * region. Avoids nested calls to cpus_read_lock().
 */
int stop_machine_cpuslocked(cpu_stop_fn_t fn, void *data, const struct cpumask *cpus);

int stop_machine_from_inactive_cpu(cpu_stop_fn_t fn, void *data,
				   const struct cpumask *cpus);
#else	/* CONFIG_SMP || CONFIG_HOTPLUG_CPU */
/**/
#endif	/* CONFIG_SMP || CONFIG_HOTPLUG_CPU */
#endif	/* _LINUX_STOP_MACHINE */
