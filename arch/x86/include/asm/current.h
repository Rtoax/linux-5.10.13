/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CURRENT_H
#define _ASM_X86_CURRENT_H

#include <linux/compiler.h>
#include <asm/percpu.h>

#ifndef __ASSEMBLY__
struct task_struct;

DECLARE_PER_CPU(struct task_struct *, current_task);

static __always_inline struct task_struct *get_current(void)
{
	return this_cpu_read_stable(current_task/* per-cpu */);
}

//#define current get_current()   /* 当前进程 */
struct task_struct *current;    /* 当前进程， 我加的，实际上 get_current() */
#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_CURRENT_H */
