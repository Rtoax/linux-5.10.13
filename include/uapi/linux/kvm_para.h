/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__LINUX_KVM_PARA_H
#define _UAPI__LINUX_KVM_PARA_H

/*
 * This header file provides a method for making a hypercall to the host
 * Architectures should define:
 * - kvm_hypercall0, kvm_hypercall1...
 * - kvm_arch_para_features
 * - kvm_para_available
 */

/* Return values for hypercalls */
#define KVM_ENOSYS		1000
#define KVM_EFAULT		EFAULT
#define KVM_EINVAL		EINVAL
#define KVM_E2BIG		E2BIG
#define KVM_EPERM		EPERM
#define KVM_EOPNOTSUPP		95

/**
 * 触发VM客户机退出，以便主机host再重新进入时检查挂起的中断。
 */
#define KVM_HC_VAPIC_POLL_IRQ		1
#define KVM_HC_MMU_OP			2
#define KVM_HC_FEATURES			3
#define KVM_HC_PPC_MAP_MAGIC_PAGE	4
/**
 * 将vcpu从HLT状态唤醒。
 *
 * 使用举例：
 * 客户机中某个vcpu正由于等待某个资源（比如spinlock），一旦忙于等待超过时间阈值，
 * 则可以执行HLT指令。执行了HLT指令，VMM会将该vcpu睡眠继续等待。然后该VM客户机的
 * 另一个vcpu可以通过KVM_HC_KICK_CPU hypercall来唤醒指定APIC ID（a1参数）的
 * vcpu，附加参数a0供以后使用。
 */
#define KVM_HC_KICK_CPU			5
#define KVM_HC_MIPS_GET_CLOCK_FREQ	6
#define KVM_HC_MIPS_EXIT_VM		7
#define KVM_HC_MIPS_CONSOLE_OUTPUT	8
/**
 * 同步VMM与VM的时钟。
 *
 * a0：主机拷贝的struct kvm_clock_offset结构体在VM中的物理地址
 * a1: clock_type, ATM 只支持 KVM_CLOCK_PAIRING_WALLCLOCK (0) ，
 *     (对应于主机host的 CLOCK_REALTIME 时钟)
 */
#define KVM_HC_CLOCK_PAIRING		9
/**
 * 发送核间中断至多个vCPUs。返回成功传送IPI的vCPU数量。
 * hypercall允许客户机发送多播IPI，64位下最多128个目的地址，32位下最多64个目的地址。
 */
#define KVM_HC_SEND_IPI		10
/**
 * 用于yield如果IPI目标vcpu中有被preempted的。
 * 当正在发送多播IPI目标时，如果目标中有vCPU被抢占了，则yield让出。
 */
#define KVM_HC_SCHED_YIELD		11
/**
 * commit 0dbb11230437("KVM: X86: Introduce KVM_HC_MAP_GPA_RANGE hypercall")
 *
 * SEV 客户机使用此超级调用向虚拟机监控程序通知页面加密状态的更改。仅当加密属性从加密>解密
 * 更改为加密时，才应调用超级调用，反之亦然。默认情况下，所有来宾页面都被视为已加密。
 *
 * 超级调用退出到用户空间以管理来宾共享区域，并与用户空间 VMM 的迁移代码集成。
 */
#define KVM_HC_MAP_GPA_RANGE        12

/*
 * hypercalls use architecture specific
 */
#include <asm/kvm_para.h>

#endif /* _UAPI__LINUX_KVM_PARA_H */
