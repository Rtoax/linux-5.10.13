// SPDX-License-Identifier: GPL-2.0
#include <linux/cpumask.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/stat.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/irqnr.h>
#include <linux/sched/cputime.h>
#include <linux/tick.h>

#ifndef arch_irq_stat_cpu
#define arch_irq_stat_cpu(cpu) 0
#endif
#ifndef arch_irq_stat
#define arch_irq_stat() 0
#endif

#ifdef arch_idle_time

static u64 get_idle_time(struct kernel_cpustat *kcs, int cpu)
{
	u64 idle;

	idle = kcs->cpustat[CPUTIME_IDLE];
	if (cpu_online(cpu) && !nr_iowait_cpu(cpu))
		idle += arch_idle_time(cpu);
	return idle;
}

static u64 get_iowait_time(struct kernel_cpustat *kcs, int cpu)
{
	u64 iowait;

	iowait = kcs->cpustat[CPUTIME_IOWAIT];
	if (cpu_online(cpu) && nr_iowait_cpu(cpu))
		iowait += arch_idle_time(cpu);
	return iowait;
}

#else

static u64 get_idle_time(struct kernel_cpustat *kcs, int cpu)
{
	u64 idle, idle_usecs = -1ULL;

	if (cpu_online(cpu))
		idle_usecs = get_cpu_idle_time_us(cpu, NULL);

	if (idle_usecs == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.idle */
		idle = kcs->cpustat[CPUTIME_IDLE];
	else
		idle = idle_usecs * NSEC_PER_USEC;

	return idle;
}

static u64 get_iowait_time(struct kernel_cpustat *kcs, int cpu)
{
	u64 iowait, iowait_usecs = -1ULL;

	if (cpu_online(cpu))
		iowait_usecs = get_cpu_iowait_time_us(cpu, NULL);

	if (iowait_usecs == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.iowait */
		iowait = kcs->cpustat[CPUTIME_IOWAIT];
	else
		iowait = iowait_usecs * NSEC_PER_USEC;

	return iowait;
}

#endif

static void show_irq_gap(struct seq_file *p, unsigned int gap)
{
	static const char zeros[] = " 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0";

	while (gap > 0) {
		unsigned int inc;

		inc = min_t(unsigned int, gap, ARRAY_SIZE(zeros) / 2);
		seq_write(p, zeros, 2 * inc);
		gap -= inc;
	}
}

static void show_all_irqs(struct seq_file *p)
{
	unsigned int i, next = 0;
    /**
     *
     */
	for_each_active_irq(i) {
		show_irq_gap(p, i - next);
		seq_put_decimal_ull(p, " ", kstat_irqs_usr(i));
		next = i + 1;
	}
	show_irq_gap(p, nr_irqs - next);
}
/**
 *
 * $ cat /proc/stat | sed 's/^/ * /g'
 * cpu  15691 821 58028 36979803 1407 11576 11927 0 0 0
 * cpu0 3004 375 8256 3075792 103 890 641 0 0 0
 * cpu1 1766 154 5253 3081507 33 766 497 0 0 0
 * cpu2 1507 3 5613 3081482 38 781 528 0 0 0
 * cpu3 1251 29 3959 3083403 33 763 530 0 0 0
 * cpu4 1007 11 5041 3081851 64 1434 1328 0 0 0
 * cpu5 850 1 4758 3082933 144 745 701 0 0 0
 * cpu6 2303 6 5438 3080576 300 960 520 0 0 0
 * cpu7 847 2 4313 3076065 129 2473 5336 0 0 0
 * cpu8 833 214 4309 3083573 161 692 393 0 0 0
 * cpu9 774 3 3775 3084242 126 714 479 0 0 0
 * cpu10 781 10 3295 3084742 144 682 413 0 0 0
 * cpu11 762 8 4013 3083633 127 671 555 0 0 0
 * intr 7828171 158 0 0 0 0 0 0 0 0 15 0 0 0 0 0 0 ......
 * ctxt 7282788
 * btime 1636676847
 * processes 13235
 * procs_running 2
 * procs_blocked 0
 * softirq 6796511 563 1243106 5801 753646 72 0 21906 2552119 22 2219276
 */
static int show_stat(struct seq_file *p, void *v)
{
	int i, j;
	u64 user, nice, system, idle, iowait, irq, softirq, steal;
	u64 guest, guest_nice;
	u64 sum = 0;
	u64 sum_softirq = 0;
	unsigned int per_softirq_sums[NR_SOFTIRQS] = {0};
	struct timespec64 boottime;

	user = nice = system = idle = iowait =
		irq = softirq = steal = 0;
	guest = guest_nice = 0;
	getboottime64(&boottime);
    /**
     *
     */
	for_each_possible_cpu(i) {
		struct kernel_cpustat kcpustat;
		u64 *cpustat = kcpustat.cpustat;
        /**
         *  获取 CPU 的 stat 统计
         */
		kcpustat_cpu_fetch(&kcpustat, i);

		user		+= cpustat[CPUTIME_USER];
		nice		+= cpustat[CPUTIME_NICE];
		system		+= cpustat[CPUTIME_SYSTEM];
		idle		+= get_idle_time(&kcpustat, i);
		iowait		+= get_iowait_time(&kcpustat, i);
		irq		+= cpustat[CPUTIME_IRQ];
		softirq		+= cpustat[CPUTIME_SOFTIRQ];
		/**
		 * 虚拟机环境被 Host 偷走的 Guest 时间,见 docs/steal-time.md
		 */
		steal		+= cpustat[CPUTIME_STEAL];
		guest		+= cpustat[CPUTIME_GUEST];
		guest_nice	+= cpustat[CPUTIME_GUEST_NICE];
		sum		+= kstat_cpu_irqs_sum(i);
		sum		+= arch_irq_stat_cpu(i);

		for (j = 0; j < NR_SOFTIRQS; j++) {
			unsigned int softirq_stat = kstat_softirqs_cpu(j, i);
            /**
             *  统计
             */
			per_softirq_sums[j] += softirq_stat;
			sum_softirq += softirq_stat;
		}
	}
	sum += arch_irq_stat();

    /**
     *  打印
     *
     * cpu  15691 821 58028 36979803 1407 11576 11927 0 0 0
     */
	seq_put_decimal_ull(p, "cpu  ", nsec_to_clock_t(user));
	seq_put_decimal_ull(p, " ", nsec_to_clock_t(nice));
	seq_put_decimal_ull(p, " ", nsec_to_clock_t(system));
	seq_put_decimal_ull(p, " ", nsec_to_clock_t(idle));
	seq_put_decimal_ull(p, " ", nsec_to_clock_t(iowait));
	seq_put_decimal_ull(p, " ", nsec_to_clock_t(irq));
	seq_put_decimal_ull(p, " ", nsec_to_clock_t(softirq));
	seq_put_decimal_ull(p, " ", nsec_to_clock_t(steal));
	seq_put_decimal_ull(p, " ", nsec_to_clock_t(guest));
	seq_put_decimal_ull(p, " ", nsec_to_clock_t(guest_nice));
	seq_putc(p, '\n');

    /**
     *  打印
     * cpu0 3004 375 8256 3075792 103 890 641 0 0 0
     *  ...
     */
	for_each_online_cpu(i) {
		struct kernel_cpustat kcpustat;
		u64 *cpustat = kcpustat.cpustat;

		kcpustat_cpu_fetch(&kcpustat, i);

		/* Copy values here to work around gcc-2.95.3, gcc-2.96 */
		user		= cpustat[CPUTIME_USER];
		nice		= cpustat[CPUTIME_NICE];
		system		= cpustat[CPUTIME_SYSTEM];
		idle		= get_idle_time(&kcpustat, i);
		iowait		= get_iowait_time(&kcpustat, i);
		irq		= cpustat[CPUTIME_IRQ];
		/**
		 * 每个 CPU 的 Softirq 统计
		 */
		softirq		= cpustat[CPUTIME_SOFTIRQ];
		/**
		 * Steal Time
		 */
		steal		= cpustat[CPUTIME_STEAL];
		guest		= cpustat[CPUTIME_GUEST];
		guest_nice	= cpustat[CPUTIME_GUEST_NICE];
		seq_printf(p, "cpu%d", i);
		seq_put_decimal_ull(p, " ", nsec_to_clock_t(user));
		seq_put_decimal_ull(p, " ", nsec_to_clock_t(nice));
		seq_put_decimal_ull(p, " ", nsec_to_clock_t(system));
		seq_put_decimal_ull(p, " ", nsec_to_clock_t(idle));
		seq_put_decimal_ull(p, " ", nsec_to_clock_t(iowait));
		seq_put_decimal_ull(p, " ", nsec_to_clock_t(irq));
		seq_put_decimal_ull(p, " ", nsec_to_clock_t(softirq));
		seq_put_decimal_ull(p, " ", nsec_to_clock_t(steal));
		seq_put_decimal_ull(p, " ", nsec_to_clock_t(guest));
		seq_put_decimal_ull(p, " ", nsec_to_clock_t(guest_nice));
		seq_putc(p, '\n');
	}

    /**
     *  显示所有 中断
     */
	seq_put_decimal_ull(p, "intr ", (unsigned long long)sum);
	show_all_irqs(p);

    /**
     *
     */
	seq_printf(p,
		"\nctxt %llu\n"
		"btime %llu\n"
		"processes %lu\n"
		"procs_running %lu\n"
		"procs_blocked %lu\n",
		nr_context_switches(),
		(unsigned long long)boottime.tv_sec,
		total_forks,
		nr_running(),
		nr_iowait());

    /**
     *  打印所有softirqs
     */
	seq_put_decimal_ull(p, "softirq ", (unsigned long long)sum_softirq);

	/**
	 * $ cat /proc/stat
	 * ...
	 * softirq 103851409 24567795 12300045 45 30237936 292567 0 232457 24280985 257 11939322
	 */
	for (i = 0; i < NR_SOFTIRQS; i++)
		seq_put_decimal_ull(p, " ", per_softirq_sums[i]);

	seq_putc(p, '\n');

	return 0;
}

static int stat_open(struct inode *inode, struct file *file)
{
	unsigned int size = 1024 + 128 * num_online_cpus();

	/* minimum size to display an interrupt count : 2 bytes */
	size += 2 * nr_irqs;
	return single_open_size(file, show_stat, NULL, size);
}

/**
 * /proc/stat
 *
 * 例如：
 * cpu  533615 2070 140979 125123804 44619 58816 64282 0 36050 0
 * cpu0 51468 1059 13737 10422265 3842 2675 1808 0 4222 0
 * cpu1 50157 201 11878 10426927 4264 2492 1710 0 3267 0
 * cpu2 46841 29 11380 10431036 4874 2364 1611 0 3552 0
 * cpu3 47467 232 10585 10432012 3751 2269 1490 0 2465 0
 * cpu4 54771 29 10481 10425588 3421 2249 1517 0 3775 0
 * cpu5 43408 12 10477 10436687 3698 2156 1491 0 2890 0
 * cpu6 32151 113 9236 10423915 4357 18356 9647 0 3065 0
 * cpu7 34054 127 9621 10404657 3104 9153 36735 0 3382 0
 * cpu8 48753 71 18425 10421097 2537 2703 2229 0 2775 0
 * cpu9 48333 97 11615 10431208 2926 2148 1325 0 2470 0
 * cpu10 34220 49 11191 10432896 4156 9978 3114 0 1182 0
 * cpu11 41986 44 12348 10435511 3684 2267 1599 0 3001 0
 * intr 109182100 0 0 0 ...
 * ctxt 207412605
 * btime 1680743657
 * processes 89545
 * procs_running 1
 * procs_blocked 0
 * softirq 103851409 24567795 12300045 45 30237936 292567 0 232457 24280985 257 11939322
 */
static const struct proc_ops stat_proc_ops = {
	.proc_flags	= PROC_ENTRY_PERMANENT,
	.proc_open	= stat_open,
	.proc_read_iter	= seq_read_iter,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

/**
 *  /proc/stat
 */
static int __init proc_stat_init(void)
{
	proc_create("stat", 0, NULL, &stat_proc_ops);
	return 0;
}
fs_initcall(proc_stat_init);
