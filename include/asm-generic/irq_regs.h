/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Fallback per-CPU frame pointer holder
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _ASM_GENERIC_IRQ_REGS_H
#define _ASM_GENERIC_IRQ_REGS_H

#include <linux/percpu.h>

/*
 * Per-cpu current frame pointer - the location of the last exception frame on
 * the stack
 *
 * 注意，这里是指针，不是每个CPU一个栈框，而是指针
 */
DECLARE_PER_CPU(struct pt_regs *, __irq_regs);
struct pt_regs * __percpu __irq_regs;//+++


static inline struct pt_regs *get_irq_regs(void)
{
	return __this_cpu_read(__irq_regs);
}

/**
 *  
 */
static inline struct pt_regs *set_irq_regs(struct pt_regs *new_regs)
{
	struct pt_regs *old_regs;

    /**
     *  每个CPU 一个 栈框 的指针
     */
	old_regs = __this_cpu_read(__irq_regs);
	__this_cpu_write(__irq_regs, new_regs);
	return old_regs;
}

#endif /* _ASM_GENERIC_IRQ_REGS_H */
