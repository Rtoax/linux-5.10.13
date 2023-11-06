/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CURRENT_H
#define _ASM_X86_CURRENT_H

#include <linux/compiler.h>
#include <asm/percpu.h>

#ifndef __ASSEMBLY__
struct task_struct;

DECLARE_PER_CPU(struct task_struct *, current_task);
struct task_struct __percpu * current_task;/* +++ */


static __always_inline struct task_struct *get_current(void)
{
    /**
     *  该结构将在 __switch_to() 中进行当前 CPU 交换
     */
	return this_cpu_read_stable(current_task/* per-cpu */);
}

#define current get_current()   /* 当前进程 */
struct task_struct *current;    /* +++ */
#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_CURRENT_H */
