/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_POSTED_INTR_H
#define __KVM_X86_VMX_POSTED_INTR_H

#define POSTED_INTR_ON  0
#define POSTED_INTR_SN  1

/**
 *  Posted-Interrupt Descriptor
 *
 * VT-d Interrupt Posting是基于Interrupt Remapping的一种扩展的中断处理方式，
 * 其主要用途是在虚拟化场景下， 可以大幅提升VMM处理直通设备中断的效率。硬件通过
 * Capability Register(CAP_REG)的PI位来报告interrupt posting capability。
 *
 * 所有的Remapping格式中断请求都需要通过中断重映射表来投递， IRTE中的Mode域(IM)
 * 用来指定这个remappable中断请求是interrupt-remapping方式还是interrupt-posting
 * 方式。
 *
 *  1. IRTE的IM位为0表示中断按照remappable方式处理；
 *  2. IRTE的IM位为1表示中断按照posted方式来处理。
 *
 * 在Interrupt Posting模式下，新增了一个与VCPU相关的内存数据结构叫做 Posted
 * Interrupt Descriptor(PD)， 这是一个64-Byte对齐的数据结构并且直接被硬件用来
 * 记录将要post的中断请求。
 *
 *  格式
 *  0-255 bits - posted-interrupt request
 *              每1bit对应一个中断向量，如果对应为设为1，表示有中断请求
 *  256 bit - outstanding notification
 *              是否有中断需要通知
 *  511-257 - reserved for software and other agents
 *              保留位 - 但从 5.10.13代码看，显然不是全都保留
 *
 */
struct pi_desc {
	/**
	 * Posted Interrupt Request (PIR)域，提供记录需要 post 的中断，占256bit，每个
	 * bit代表一个中断号。
	 */
	u32 pir[8];
	union {
		struct {
				/**
				 * bit 256 - Outstanding Notification
				 *
				 * Outstanding Notification (ON)域，由硬件来自动更新，用来表示是否有
				 * 中断请求pending。当此位为0时，硬件通过修改其为1来产生一个通知事件告知
				 * 中断请求到来。接收这个通知事件的实体(处理器或者软件)在处理这个posted
				 * interrupt时后必须将其清零。
				 */
			u16	on	: 1,
				/**
				 * bit 257 - Suppress Notification
				 *
				 * Suppress Notification (SN)域，表示 non-urgent 中断请求的通知事件
				 * 是否要被supressed(抑制)。
				 */
				sn	: 1,
				/* bit 271:258 - Reserved */
				rsvd_1	: 14;
				/**
				 * bit 279:272 - Notification Vector
				 *
				 * Notification Vector (NV)域，用来指定产生posted-interrupt
				 * “通知事件”(notification event)的vector号。
				 */
			u8	nv;
				/* bit 287:280 - Reserved */
			u8	rsvd_2;
				/**
				 * bit 319:288 - Notification Destination
				 *
				 * Notification Destination (NDST)域，用来指定此中断要投递的 vCPU
				 * 所运行物理CPU的APIC-ID。
				 */
			u32	ndst;
		};
		u64 control;
	};
	u32 rsvd[6];
} __aligned(64);

static inline bool pi_test_and_set_on(struct pi_desc *pi_desc)
{
	return test_and_set_bit(POSTED_INTR_ON,
			(unsigned long *)&pi_desc->control);
}

static inline bool pi_test_and_clear_on(struct pi_desc *pi_desc)
{
	return test_and_clear_bit(POSTED_INTR_ON,
			(unsigned long *)&pi_desc->control);
}

static inline int pi_test_and_set_pir(int vector, struct pi_desc *pi_desc)
{
	return test_and_set_bit(vector, (unsigned long *)pi_desc->pir);
}

static inline bool pi_is_pir_empty(struct pi_desc *pi_desc)
{
	return bitmap_empty((unsigned long *)pi_desc->pir, NR_VECTORS);
}

static inline void pi_set_sn(struct pi_desc *pi_desc)
{
	set_bit(POSTED_INTR_SN,
		(unsigned long *)&pi_desc->control);
}

static inline void pi_set_on(struct pi_desc *pi_desc)
{
	set_bit(POSTED_INTR_ON,
		(unsigned long *)&pi_desc->control);
}

static inline void pi_clear_on(struct pi_desc *pi_desc)
{
	clear_bit(POSTED_INTR_ON,
		(unsigned long *)&pi_desc->control);
}

static inline void pi_clear_sn(struct pi_desc *pi_desc)
{
	clear_bit(POSTED_INTR_SN,
		(unsigned long *)&pi_desc->control);
}

static inline int pi_test_on(struct pi_desc *pi_desc)
{
	return test_bit(POSTED_INTR_ON,
			(unsigned long *)&pi_desc->control);
}

static inline int pi_test_sn(struct pi_desc *pi_desc)
{
	return test_bit(POSTED_INTR_SN,
			(unsigned long *)&pi_desc->control);
}

void vmx_vcpu_pi_load(struct kvm_vcpu *vcpu, int cpu);
void vmx_vcpu_pi_put(struct kvm_vcpu *vcpu);
int pi_pre_block(struct kvm_vcpu *vcpu);
void pi_post_block(struct kvm_vcpu *vcpu);
void pi_wakeup_handler(void);
void __init pi_init_cpu(int cpu);
bool pi_has_pending_interrupt(struct kvm_vcpu *vcpu);
int pi_update_irte(struct kvm *kvm, unsigned int host_irq, uint32_t guest_irq,
		   bool set);

#endif /* __KVM_X86_VMX_POSTED_INTR_H */
