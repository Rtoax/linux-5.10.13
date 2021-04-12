/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CPU_H
#define _ASM_X86_CPU_H

#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/nodemask.h>
#include <linux/percpu.h>

#ifdef CONFIG_SMP

extern void prefill_possible_map(void);

#else /* CONFIG_SMP */
/*  */
#endif /* CONFIG_SMP */

struct x86_cpu {
	struct cpu cpu;
};

#ifdef CONFIG_HOTPLUG_CPU
extern int arch_register_cpu(int num);
extern void arch_unregister_cpu(int);
extern void start_cpu0(void);   /*  */
#ifdef CONFIG_DEBUG_HOTPLUG_CPU0
extern int _debug_hotplug_cpu(int cpu, int action);
#endif
#endif

int mwait_usable(const struct cpuinfo_x86 *);

unsigned int x86_family(unsigned int sig);
unsigned int x86_model(unsigned int sig);
unsigned int x86_stepping(unsigned int sig);
#ifdef CONFIG_CPU_SUP_INTEL
extern void __init cpu_set_core_cap_bits(struct cpuinfo_x86 *c);
extern void switch_to_sld(unsigned long tifn);
extern bool handle_user_split_lock(struct pt_regs *regs, long error_code);
extern bool handle_guest_split_lock(unsigned long ip);
#else
/*  */
#endif
#ifdef CONFIG_IA32_FEAT_CTL
void init_ia32_feat_ctl(struct cpuinfo_x86 *c);
#else
/*  */
#endif
#endif /* _ASM_X86_CPU_H */
