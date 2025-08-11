// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006, Thomas Gleixner, Russell King
 *
 * This file contains the core interrupt handling code. Detailed
 * information is available in Documentation/core-api/genericirq.rst
 *
 */

#include <linux/irq.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

#include <trace/events/irq.h>

#include "internals.h"

#ifdef CONFIG_GENERIC_IRQ_MULTI_HANDLER
/**
 *  在 set_handle_irq 中设置
 *  根据不同的 SOC 类型，会被设置成各种不同的回调函数。
 *
 *  以 GIC-V2 为例， 指向 `gic_handle_irq()`, set_handle_irq(gic_handle_irq);
 */
void __ro_after_init (*handle_arch_irq)(struct pt_regs *) ;
#endif

/**
 * handle_bad_irq - handle spurious and unhandled irqs
 * @desc:      description of the interrupt
 *
 * Handles spurious and unhandled IRQ's. It also prints a debugmessage.
 */
void handle_bad_irq(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);

	/**
	 *  打印
	 */
	print_irq_desc(irq, desc);
	/**
	 *  统计
	 */
	kstat_incr_irqs_this_cpu(desc);
	/**
	 *  ACK 就 tm 神奇
	 */
	ack_bad_irq(irq);
}
EXPORT_SYMBOL_GPL(handle_bad_irq);

/*
 * Special, empty irq handler:
 */
irqreturn_t no_action(int cpl, void *dev_id)
{
	return IRQ_NONE;
}
EXPORT_SYMBOL_GPL(no_action);

static void warn_no_thread(unsigned int irq, struct irqaction *action)
{
	if (test_and_set_bit(IRQTF_WARNED, &action->thread_flags))
		return;

	printk(KERN_WARNING "IRQ %d device %s returned IRQ_WAKE_THREAD "
	       "but no thread function available.", irq, action->name);
}

/**
 *  唤醒内核中断线程
 */
void __irq_wake_thread(struct irq_desc *desc, struct irqaction *action)
{
	/*
	 * In case the thread crashed and was killed we just pretend that
	 * we handled the interrupt. The hardirq handler has disabled the
	 * device interrupt, so no irq storm is lurking.
	 */
	if (action->thread->flags & PF_EXITING)
		return;

	/*
	 * Wake up the handler thread for this action. If the
	 * RUNTHREAD bit is already set, nothing to do.
	 *
	 * 表示已经唤醒了，直接返回
	 */
	if (test_and_set_bit(IRQTF_RUNTHREAD, &action->thread_flags))
		return;

	/*
	 * It's safe to OR the mask lockless here. We have only two
	 * places which write to threads_oneshot: This code and the
	 * irq thread.
	 *
	 * This code is the hard irq context and can never run on two
	 * cpus in parallel. If it ever does we have more serious
	 * problems than this bitmask.
	 *
	 * The irq threads of this irq which clear their "running" bit
	 * in threads_oneshot are serialized via desc->lock against
	 * each other and they are serialized against this code by
	 * IRQS_INPROGRESS.
	 *
	 * Hard irq handler: 硬中断处理函数
	 *
	 *	spin_lock(desc->lock);
	 *	desc->state |= IRQS_INPROGRESS;
	 *	spin_unlock(desc->lock);
	 *	set_bit(IRQTF_RUNTHREAD, &action->thread_flags);
	 *	desc->threads_oneshot |= mask;
	 *	spin_lock(desc->lock);
	 *	desc->state &= ~IRQS_INPROGRESS;
	 *	spin_unlock(desc->lock);
	 *
	 * irq thread: 中断线程
	 *
	 * again:
	 *	spin_lock(desc->lock);
	 *	if (desc->state & IRQS_INPROGRESS) {
	 *		spin_unlock(desc->lock);
	 *		while(desc->state & IRQS_INPROGRESS)
	 *			cpu_relax();
	 *		goto again;
	 *	}
	 *	if (!test_bit(IRQTF_RUNTHREAD, &action->thread_flags))
	 *		desc->threads_oneshot &= ~mask;
	 *	spin_unlock(desc->lock);
	 *
	 * So either the thread waits for us to clear IRQS_INPROGRESS
	 * or we are waiting in the flow handler for desc->lock to be
	 * released before we reach this point. The thread also checks
	 * IRQTF_RUNTHREAD under desc->lock. If set it leaves
	 * threads_oneshot untouched and runs the thread another time.
	 * 翻译：
	 * 所以要么线程等待我们清除 IRQS_INPROGRESS 要么我们在流处理程序
	 * 中等待 desc->lock 在我们到达这一点之前被释放。
	 * 该线程还在 desc->lock 下检查 IRQTF_RUNTHREAD。
	 * 如果设置，它会保持 threads_oneshot 不变并再次运行线程。
	 *
	 * -------------------------------------------------------------
	 * 有两种方式可能会并发的修改 threads_oneshot 此变量
	 * 一个是 硬件中断处理，另一个是 中断线程
	 */
	desc->threads_oneshot |= action->thread_mask;

	/*
	 * We increment the threads_active counter in case we wake up
	 * the irq thread. The irq thread decrements the counter when
	 * it returns from the handler or in the exit path and wakes
	 * up waiters which are stuck in synchronize_irq() when the
	 * active count becomes zero. synchronize_irq() is serialized
	 * against this code (hard irq handler) via IRQS_INPROGRESS
	 * like the finalize_oneshot() code. See comment above.
	 *
	 * 增加线程个数计数
	 */
	atomic_inc(&desc->threads_active);

	/**
	 *  唤醒中断线程
	 */
	wake_up_process(action->thread);
}

/**
 *
 */
irqreturn_t __handle_irq_event_percpu(struct irq_desc *desc, unsigned int *flags)
{
	irqreturn_t retval = IRQ_NONE;
	unsigned int irq = desc->irq_data.irq;
	struct irqaction *action;

	record_irq_time(desc);

	/**
	 *  遍历此中断的 action 链表
	 */
	for_each_action_of_desc(desc, action) {
		irqreturn_t res;

		/*
		 * If this IRQ would be threaded under force_irqthreads, mark it so.
		 */
		if (irq_settings_can_thread(desc) &&
		    !(action->flags & (IRQF_NO_THREAD | IRQF_PERCPU | IRQF_ONESHOT)))
			lockdep_hardirq_threaded();

		trace_irq_handler_entry(irq, action);
		/**
		 *  调用该中断所有的 action handler
		 */
		res = action->handler(irq, action->dev_id);
		trace_irq_handler_exit(irq, action, res);

		if (WARN_ONCE(!irqs_disabled(),"irq %u handler %pS enabled interrupts\n",
			      irq, action->handler))
			local_irq_disable();

		/**
		 *
		 */
		switch (res) {
		/**
		 *  需要唤醒内核的中断线程
		 */
		case IRQ_WAKE_THREAD:
			/*
			 * Catch drivers which return WAKE_THREAD but
			 * did not set up a thread function
			 */
			if (unlikely(!action->thread_fn)) {
				warn_no_thread(irq, action);
				break;
			}

			/**
			 *  唤醒
			 */
			__irq_wake_thread(desc, action);

			fallthrough;	/* to add to randomness */
        /**
         *  中断已经处理完毕
         */
        case IRQ_HANDLED:
			*flags |= action->flags;
			break;

		default:
			break;
		}

		retval |= res;
	}

	return retval;
}

/**
 *
 */
irqreturn_t handle_irq_event_percpu(struct irq_desc *desc)
{
	irqreturn_t retval;
	unsigned int flags = 0;

	/**
	 *
	 */
	retval = __handle_irq_event_percpu(desc, &flags);

	/**
	 *
	 */
	add_interrupt_randomness(desc->irq_data.irq, flags);

	if (!noirqdebug)
		note_interrupt(desc, retval);
	return retval;
}

/**
 *  中断处理的核心函数
 *
 *  如果设备有一个已注册的处理例程，并法身了中断，这个函数将被调用
 */
irqreturn_t handle_irq_event(struct irq_desc *desc)
{
	irqreturn_t ret;

	/**
	 *  pending 标志位清零
	 */
	desc->istate &= ~IRQS_PENDING;

	/**
	 *  设置标志位，表示该中断正在处理
	 */
	irqd_set(&desc->irq_data, IRQD_IRQ_INPROGRESS);
	raw_spin_unlock(&desc->lock);

	/**
	 *
	 */
	ret = handle_irq_event_percpu(desc);

	raw_spin_lock(&desc->lock);

	/**
	 *  清除标记位
	 */
	irqd_clear(&desc->irq_data, IRQD_IRQ_INPROGRESS);
	return ret;
}

#ifdef CONFIG_GENERIC_IRQ_MULTI_HANDLER
int __init set_handle_irq(void (*handle_irq)(struct pt_regs *))
{
	if (handle_arch_irq)
		return -EBUSY;

	handle_arch_irq = handle_irq;
	return 0;
}
#endif
