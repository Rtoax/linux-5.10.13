/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * irq.h: in kernel interrupt controller related definitions
 * Copyright (c) 2007, Intel Corporation.
 *
 * Authors:
 *   Yaozu (Eddie) Dong <Eddie.dong@intel.com>
 */

#ifndef __IRQ_H
#define __IRQ_H

#include <linux/mm_types.h>
#include <linux/hrtimer.h>
#include <linux/kvm_host.h>
#include <linux/spinlock.h>

#include <kvm/iodev.h>
#include "lapic.h"

#define PIC_NUM_PINS 16
#define SELECT_PIC(irq) \
	((irq) < 8 ? KVM_IRQCHIP_PIC_MASTER : KVM_IRQCHIP_PIC_SLAVE)

struct kvm;
struct kvm_vcpu;

/**
 *  记录每个 8259A 的状态 - 8259A
 */
struct kvm_kpic_state {
    /**
	 *  边沿跳变 监测
	 */
	u8 last_irr;	/* edge detection */
    /**
	 *  中断请求寄存器
	 */
	u8 irr;		/* interrupt request register */
    /**
	 *  中断屏蔽寄存器
	 */
	u8 imr;		/* interrupt mask register */
    /**
	 *  中断服务寄存器 - 记录CPU 正在处理的寄存器
	 *  
	 */
	u8 isr;		/* interrupt service register */
    /**
	 *  
     * 典型的有两种优先级模式
     *  1. 固定优先级(fixed prio) - 优先级是固定的，从IR0到IR7一次降低
     *  2. 循环优先级(rotating prio) - 即当前处理完的IRn 优先级调整为最低
     *                      当前处理的下一个调整为最高。
     *
     * 该变量记录 当前最高优先级中断对应的管脚号
	 */
	u8 priority_add;	/* highest irq priority */
    /**
	 *  起始中断向量号
	 */
	u8 irq_base;
	u8 read_reg_select;
	u8 poll;
	u8 special_mask;
	u8 init_state;
    /**
     *  AEOI 模式
     *  如果 8259A 工作在此模式，那么会自动复位 ISR，见 `pic_intack()`
     */
	u8 auto_eoi;
	u8 rotate_on_auto_eoi;
	u8 special_fully_nested_mode;
	u8 init4;		/* true if 4 byte init */
    /**
	 *  触发模式
	 *  置位时 - 水平触发
	 *  没置位 - 边沿触发
	 */
	u8 elcr;		/* PIIX edge/trigger selection */
	u8 elcr_mask;
	u8 isr_ack;	/* interrupt ack detection */
	struct kvm_pic *pics_state;
};

/**
 *  
 */
struct kvm_pic {
	spinlock_t lock;
	bool wakeup_needed;
	unsigned pending_acks;
	struct kvm *kvm;
    /**
	 *  两片 8259A - master 和 slave
	 */
	struct kvm_kpic_state pics[2]; /* 0 is master pic, 1 is slave pic */

    /**
	 *  模拟 8259A 在将受到的中断请求记录到 IRR 后，将设置一个变量 output
	 *  后面在 切入 Guest 前 KVM 会查询这个变量。
	 *
	 *  非 0 表示外部中断等待处理
	 *
	 *  在 `pic_irq_request()` 中设置
	 *  在 `kvm_cpu_has_extint()` 中查询
	 */
	int output;		/* intr from master PIC */
	struct kvm_io_device dev_master;
	struct kvm_io_device dev_slave;
	struct kvm_io_device dev_eclr;
	void (*ack_notifier)(void *opaque, int irq);
	unsigned long irq_states[PIC_NUM_PINS];
};

int kvm_pic_init(struct kvm *kvm);
void kvm_pic_destroy(struct kvm *kvm);
int kvm_pic_read_irq(struct kvm *kvm);
void kvm_pic_update_irq(struct kvm_pic *s);

static inline int irqchip_split(struct kvm *kvm)
{
	int mode = kvm->arch.irqchip_mode;

	/* Matches smp_wmb() when setting irqchip_mode */
	smp_rmb();
	return mode == KVM_IRQCHIP_SPLIT;
}

static inline int irqchip_kernel(struct kvm *kvm)
{
	int mode = kvm->arch.irqchip_mode;

	/* Matches smp_wmb() when setting irqchip_mode */
	smp_rmb();
	return mode == KVM_IRQCHIP_KERNEL;
}

static inline int pic_in_kernel(struct kvm *kvm)
{
	return irqchip_kernel(kvm);
}

static inline int irqchip_in_kernel(struct kvm *kvm)
{
	int mode = kvm->arch.irqchip_mode;

	/* Matches smp_wmb() when setting irqchip_mode */
	smp_rmb();
	return mode != KVM_IRQCHIP_NONE;
}

void kvm_inject_pending_timer_irqs(struct kvm_vcpu *vcpu);
void kvm_inject_apic_timer_irqs(struct kvm_vcpu *vcpu);
void kvm_apic_nmi_wd_deliver(struct kvm_vcpu *vcpu);
void __kvm_migrate_apic_timer(struct kvm_vcpu *vcpu);
void __kvm_migrate_pit_timer(struct kvm_vcpu *vcpu);
void __kvm_migrate_timers(struct kvm_vcpu *vcpu);

int apic_has_pending_timer(struct kvm_vcpu *vcpu);

int kvm_setup_default_irq_routing(struct kvm *kvm);
int kvm_setup_empty_irq_routing(struct kvm *kvm);
int kvm_irq_delivery_to_apic(struct kvm *kvm, struct kvm_lapic *src,
			     struct kvm_lapic_irq *irq,
			     struct dest_map *dest_map);

#endif
