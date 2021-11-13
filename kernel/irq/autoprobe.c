// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 1992, 1998-2004 Linus Torvalds, Ingo Molnar
 *
 * This file contains the interrupt probing code and driver APIs.
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/async.h>

#include "internals.h"

/*
 * Autodetection depends on the fact that any interrupt that
 * comes in on to an unassigned handler will get stuck with
 * "IRQS_WAITING" cleared and the interrupt disabled.
 */
static DEFINE_MUTEX(probing_active);

/**
 *	probe_irq_on	- begin an interrupt autodetect
 *
 *	Commence probing for an interrupt. The interrupts are scanned
 *	and a mask of potential interrupt lines is returned.
 *
 *  返回一个 未分配中断 的位掩码
 *  驱动程序必须保存返回的位掩码，并将它传递给后面的`probe_irq_off()`函数
 *  调用该函数后，驱动程序要安排设备产生至少一次中断。
 *
 *  在调用`probe_irq_on()`函数后，要启用设备上的中断，
 *  并在调用`probe_irq_off()`函数前禁用中断。
 *  在`probe_irq_off()`之后，需要处理设备上待处理的中断。
 */
unsigned long probe_irq_on(void)
{
	struct irq_desc *desc;
	unsigned long mask = 0;
	int i;

	/*
	 * quiesce the kernel, or at least the asynchronous portion
	 */
	async_synchronize_full();
	mutex_lock(&probing_active);
	/*
	 * something may have generated an irq long ago and we want to
	 * flush such a longstanding irq before considering it as spurious.
	 */
	for_each_irq_desc_reverse(i, desc) {
		raw_spin_lock_irq(&desc->lock);
		if (!desc->action && irq_settings_can_probe(desc)) {
			/*
			 * Some chips need to know about probing in
			 * progress:
			 */
			if (desc->irq_data.chip->irq_set_type)
				desc->irq_data.chip->irq_set_type(&desc->irq_data,
							 IRQ_TYPE_PROBE);
			irq_activate_and_startup(desc, IRQ_NORESEND);
		}
		raw_spin_unlock_irq(&desc->lock);
	}

	/* Wait for longstanding interrupts to trigger. */
	msleep(20);

	/*
	 * enable any unassigned irqs
	 * (we must startup again here because if a longstanding irq
	 * happened in the previous stage, it may have masked itself)
	 */
	for_each_irq_desc_reverse(i, desc) {
		raw_spin_lock_irq(&desc->lock);
		if (!desc->action && irq_settings_can_probe(desc)) {
			desc->istate |= IRQS_AUTODETECT | IRQS_WAITING;
			if (irq_activate_and_startup(desc, IRQ_NORESEND))
				desc->istate |= IRQS_PENDING;
		}
		raw_spin_unlock_irq(&desc->lock);
	}

	/*
	 * Wait for spurious interrupts to trigger
	 */
	msleep(100);

	/*
	 * Now filter out any obviously spurious interrupts
	 */
	for_each_irq_desc(i, desc) {
		raw_spin_lock_irq(&desc->lock);

		if (desc->istate & IRQS_AUTODETECT) {
			/* It triggered already - consider it spurious. */
			if (!(desc->istate & IRQS_WAITING)) {
				desc->istate &= ~IRQS_AUTODETECT;
				irq_shutdown_and_deactivate(desc);
			} else
				if (i < 32)
					mask |= 1 << i;
		}
		raw_spin_unlock_irq(&desc->lock);
	}

	return mask;
}
EXPORT_SYMBOL(probe_irq_on);

/**
 *	probe_irq_mask - scan a bitmap of interrupt lines
 *	@val:	mask of interrupts to consider
 *
 *	Scan the interrupt lines and return a bitmap of active
 *	autodetect interrupts. The interrupt probe logic state
 *	is then returned to its previous value.
 *
 *	Note: we need to scan all the irq's even though we will
 *	only return autodetect irq numbers - just so that we reset
 *	them all to a known state.
 */
unsigned int probe_irq_mask(unsigned long val)
{
	unsigned int mask = 0;
	struct irq_desc *desc;
	int i;

	for_each_irq_desc(i, desc) {
		raw_spin_lock_irq(&desc->lock);
		if (desc->istate & IRQS_AUTODETECT) {
			if (i < 16 && !(desc->istate & IRQS_WAITING))
				mask |= 1 << i;

			desc->istate &= ~IRQS_AUTODETECT;
			irq_shutdown_and_deactivate(desc);
		}
		raw_spin_unlock_irq(&desc->lock);
	}
	mutex_unlock(&probing_active);

	return mask & val;
}
EXPORT_SYMBOL(probe_irq_mask);

/**
 *	probe_irq_off	- end an interrupt autodetect
 *	@val: mask of potential interrupts (unused)
 *
 *	Scans the unused interrupt lines and returns the line which
 *	appears to have triggered the interrupt. If no interrupt was
 *	found then zero is returned. If more than one interrupt is
 *	found then minus the first candidate is returned to indicate
 *	their is doubt.
 *
 *	The interrupt probe logic state is returned to its previous
 *	value.
 *
 *	BUGS: When used in a module (which arguably shouldn't happen)
 *	nothing prevents two IRQ probe callers from overlapping. The
 *	results of this are non-optimal.
 *
 *  在请求设备产生中断之后，驱动程序调用这个函数
 *  并将前面 probe_irq_on() 返回 的位掩码作为参数 传递给 该函数。
 *  probe_irq_off() 返回 probe_irq_on() 之后发生的中断编号。
 *
 *  如果没有中断发生，就返回0.
 *  如果产生了多次中断，就返回一个 负值。
 *
 *  在调用`probe_irq_on()`函数后，要启用设备上的中断，
 *  并在调用`probe_irq_off()`函数前禁用中断。
 *  在`probe_irq_off()`之后，需要处理设备上待处理的中断。
 */
int probe_irq_off(unsigned long val)
{
	int i, irq_found = 0, nr_of_irqs = 0;
	struct irq_desc *desc;

	for_each_irq_desc(i, desc) {
		raw_spin_lock_irq(&desc->lock);

		if (desc->istate & IRQS_AUTODETECT) {
            /**
             *  IRQS_WAITING 表示某个 irq_desc 处于等待状态 
             *  IRQ 探测是通过为每个缺少中断处理例程的 IRQ 设置 IRQS_WAITING 状态位 来完成的
             *  当中断产生时，因为没有注册处理例程， do_IRQ 清除该标志位后返回。
             *  当 probe_irq_off() 被一个驱动程序调用的时候，只需要搜索那些 没有设置 IRQS_WAITING 位的 IRQ
             */
			if (!(desc->istate & IRQS_WAITING)) {
				if (!nr_of_irqs)
					irq_found = i;
				nr_of_irqs++;
			}
			desc->istate &= ~IRQS_AUTODETECT;
			irq_shutdown_and_deactivate(desc);
		}
		raw_spin_unlock_irq(&desc->lock);
	}
	mutex_unlock(&probing_active);

	if (nr_of_irqs > 1)
		irq_found = -irq_found;

	return irq_found;
}
EXPORT_SYMBOL(probe_irq_off);

