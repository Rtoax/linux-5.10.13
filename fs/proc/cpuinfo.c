// SPDX-License-Identifier: GPL-2.0
#include <linux/cpufreq.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

__weak void arch_freq_prepare_all(void)
{
}

extern const struct seq_operations cpuinfo_op; /* /proc/cpuinfo */
static int cpuinfo_open(struct inode *inode, struct file *file)
{
	arch_freq_prepare_all();
	return seq_open(file, &cpuinfo_op);
}

/**
 * /proc/cpuinfo
 *
 * 访问 /proc/cpuinfo 时，会执行 CPUID 指令(见 test-linux/kvm/exit_reason.bt)
 */
static const struct proc_ops cpuinfo_proc_ops = {
	.proc_flags	= PROC_ENTRY_PERMANENT,
	.proc_open	= cpuinfo_open,
	.proc_read_iter	= seq_read_iter,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
};

/* /proc/cpuinfo */
static int __init proc_cpuinfo_init(void)
{
	proc_create("cpuinfo", 0, NULL, &cpuinfo_proc_ops);
	return 0;
}
fs_initcall(proc_cpuinfo_init); /* /proc/cpuinfo  */