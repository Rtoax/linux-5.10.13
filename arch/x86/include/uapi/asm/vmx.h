/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * vmx.h: VMX Architecture related definitions
 * Copyright (c) 2004, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * A few random additions are:
 * Copyright (C) 2006 Qumranet
 *    Avi Kivity <avi@qumranet.com>
 *    Yaniv Kamay <yaniv@qumranet.com>
 *
 */
#ifndef _UAPIVMX_H
#define _UAPIVMX_H


#define VMX_EXIT_REASONS_FAILED_VMENTRY         0x80000000


/**
 *  鉴于 LAPIC 寄存器的访问非常频繁，所以 Intel 从硬件层面做出了很多支持
 *  比如 为 访问 LAPIC 寄存器增加了专门退出的原因，这样就不必首先进入缺页
 *  异常函数来尝试处理，当缺页异常函数无法处理后再进入指令模拟函数，而是
 *  直接进入 LAPIC 的处理函数
 */

/**
 * 当发生退出事件时要调用的函数：
 *
 * HOST_RIP-->VMMEntryPoint(VM-Exit处理程序) => kvm_vmx_exit_handlers[]
 */

/**
 * (EXCEPTION_BITMAP)//异常信息可以拦截int3等异常
 */
#define EXIT_REASON_EXCEPTION_NMI       0
#define EXIT_REASON_EXTERNAL_INTERRUPT  1
#define EXIT_REASON_TRIPLE_FAULT        2
#define EXIT_REASON_INIT_SIGNAL			3

#define EXIT_REASON_INTERRUPT_WINDOW    7
#define EXIT_REASON_NMI_WINDOW          8
#define EXIT_REASON_TASK_SWITCH         9
/**
 * 物理机执行 CPUID 和 Guest 中执行的 CPUID 是子集关系，所以，这是必须处理的。
 * 见 《深度探索 Linux 系统虚拟化》P25
 */
#define EXIT_REASON_CPUID               10
/**
 * 处理器执行 hlt 后，将处于停机状态（halt）。对于开启了超线程的处理器，hlt指令
 * 是停止的逻辑核，之后，如果收到 NMI, SMI 中断，或者 reset 信号，则恢复运行。
 * 但是，对于虚拟机而言，如果任由 Guest 执行 hlt，将导致物理 CPU 停止运行，然而
 * 我们需要停止的只是 Host 中用于模拟 vCPU 的线程。所以，Guest 执行 hlt 指令
 * 时，需要陷入 KVM 中，由 KVM 挂起 vCPU 对应的线程，而不是停止物理 CPU。
 */
#define EXIT_REASON_HLT                 12
/* 必须处理的 INVD — Invalidate Internal Caches */
#define EXIT_REASON_INVD                13
#define EXIT_REASON_INVLPG              14
#define EXIT_REASON_RDPMC               15
#define EXIT_REASON_RDTSC               16
/**
 * 在Linux中，大家应该对syscall非常的了解和熟悉，其是用户态进入内核态的一种途径或者说是一种
 * 方式，完成了两个模式之间的切换；
 *
 * 而在虚拟环境中，有没有一种类似于syscall这种方式，能够从no root模式切换到root模式呢？
 * 答案是肯定的，KVM提供了Hypercall机制， x86体系架构也有相关的指令支持。
 *
 * VMCALL： 必须处理，因为可能存在多个VT
 */
#define EXIT_REASON_VMCALL              18
#define EXIT_REASON_VMCLEAR             19
#define EXIT_REASON_VMLAUNCH            20
#define EXIT_REASON_VMPTRLD             21
#define EXIT_REASON_VMPTRST             22
#define EXIT_REASON_VMREAD              23
#define EXIT_REASON_VMRESUME            24
#define EXIT_REASON_VMWRITE             25
#define EXIT_REASON_VMOFF               26
#define EXIT_REASON_VMON                27
/* 必须处理的只要处理cr3寄存器 */
#define EXIT_REASON_CR_ACCESS           28
/* 可以监控硬件断点 */
#define EXIT_REASON_DR_ACCESS           29
/* 可以监控键盘鼠标输入输出 */
#define EXIT_REASON_IO_INSTRUCTION      30
/* 必须处理的 */
#define EXIT_REASON_MSR_READ            31
/* 必须处理的 */
#define EXIT_REASON_MSR_WRITE           32
#define EXIT_REASON_INVALID_STATE       33
#define EXIT_REASON_MSR_LOAD_FAIL       34
#define EXIT_REASON_MWAIT_INSTRUCTION   36
#define EXIT_REASON_MONITOR_TRAP_FLAG   37
#define EXIT_REASON_MONITOR_INSTRUCTION 39
#define EXIT_REASON_PAUSE_INSTRUCTION   40
#define EXIT_REASON_MCE_DURING_VMENTRY  41
#define EXIT_REASON_TPR_BELOW_THRESHOLD 43
/**
 * 为了提高效率和简化实现， Intel VMX 增加了 APIC ACCESS VMX EXIT
 *
 * 因为 LAPIC 访问非常频繁，所以 Intel 从硬件层面作了很多支持，比如为访问 LAPIC 的寄存器
 * 增加了退出原因 EXIT_REASON_APIC_ACCESS，这样就不必首先进入缺页异常尝试处理，而是直接
 * 进入 LAPIC 处理函数。
 */
#define EXIT_REASON_APIC_ACCESS         44
#define EXIT_REASON_EOI_INDUCED         45
/**
 * VM 访问 GDTR 和 IDTR
 *
 * 疑问： 2022-10-21 19:17 荣涛
 * VMM 可以根据 GDTR 获取 Guest OS 的段的特权等级、权限等，从而判定这个地址范围
 * 是不是 Guest OS 的内核代码段，和数据段？
 */
#define EXIT_REASON_GDTR_IDTR           46
#define EXIT_REASON_LDTR_TR             47
/**
 * EXIT_REASON_EPT_VIOLATION is similar to a "page not present" pagefault.
 * EXIT_REASON_EPT_MISCONFIG is similar to a "reserved bit set" pagefault.
 *
 * EPT_VIOLATION 表示的是对应的物理页不存在
 */
#define EXIT_REASON_EPT_VIOLATION       48
/**
 * EPT_MISCONFIG 表示EPT页表中有非法的域
 */
#define EXIT_REASON_EPT_MISCONFIG       49
#define EXIT_REASON_INVEPT              50
#define EXIT_REASON_RDTSCP              51
#define EXIT_REASON_PREEMPTION_TIMER    52
#define EXIT_REASON_INVVPID             53
#define EXIT_REASON_WBINVD              54
#define EXIT_REASON_XSETBV              55
#define EXIT_REASON_APIC_WRITE          56
#define EXIT_REASON_RDRAND              57
#define EXIT_REASON_INVPCID             58
#define EXIT_REASON_VMFUNC              59
#define EXIT_REASON_ENCLS               60
#define EXIT_REASON_RDSEED              61
#define EXIT_REASON_PML_FULL            62
#define EXIT_REASON_XSAVES              63
#define EXIT_REASON_XRSTORS             64
#define EXIT_REASON_UMWAIT              67
#define EXIT_REASON_TPAUSE              68
#define EXIT_REASON_BUS_LOCK            74
#define EXIT_REASON_NOTIFY              75

#define VMX_EXIT_REASONS \
	{ EXIT_REASON_EXCEPTION_NMI,         "EXCEPTION_NMI" }, \
	{ EXIT_REASON_EXTERNAL_INTERRUPT,    "EXTERNAL_INTERRUPT" }, \
	{ EXIT_REASON_TRIPLE_FAULT,          "TRIPLE_FAULT" }, \
	{ EXIT_REASON_INIT_SIGNAL,           "INIT_SIGNAL" }, \
	{ EXIT_REASON_INTERRUPT_WINDOW,      "INTERRUPT_WINDOW" }, \
	{ EXIT_REASON_NMI_WINDOW,            "NMI_WINDOW" }, \
	{ EXIT_REASON_TASK_SWITCH,           "TASK_SWITCH" }, \
	{ EXIT_REASON_CPUID,                 "CPUID" }, \
	{ EXIT_REASON_HLT,                   "HLT" }, \
	{ EXIT_REASON_INVD,                  "INVD" }, \
	{ EXIT_REASON_INVLPG,                "INVLPG" }, \
	{ EXIT_REASON_RDPMC,                 "RDPMC" }, \
	{ EXIT_REASON_RDTSC,                 "RDTSC" }, \
	{ EXIT_REASON_VMCALL,                "VMCALL" }, \
	{ EXIT_REASON_VMCLEAR,               "VMCLEAR" }, \
	{ EXIT_REASON_VMLAUNCH,              "VMLAUNCH" }, \
	{ EXIT_REASON_VMPTRLD,               "VMPTRLD" }, \
	{ EXIT_REASON_VMPTRST,               "VMPTRST" }, \
	{ EXIT_REASON_VMREAD,                "VMREAD" }, \
	{ EXIT_REASON_VMRESUME,              "VMRESUME" }, \
	{ EXIT_REASON_VMWRITE,               "VMWRITE" }, \
	{ EXIT_REASON_VMOFF,                 "VMOFF" }, \
	{ EXIT_REASON_VMON,                  "VMON" }, \
	{ EXIT_REASON_CR_ACCESS,             "CR_ACCESS" }, \
	{ EXIT_REASON_DR_ACCESS,             "DR_ACCESS" }, \
	{ EXIT_REASON_IO_INSTRUCTION,        "IO_INSTRUCTION" }, \
	{ EXIT_REASON_MSR_READ,              "MSR_READ" }, \
	{ EXIT_REASON_MSR_WRITE,             "MSR_WRITE" }, \
	{ EXIT_REASON_INVALID_STATE,         "INVALID_STATE" }, \
	{ EXIT_REASON_MSR_LOAD_FAIL,         "MSR_LOAD_FAIL" }, \
	{ EXIT_REASON_MWAIT_INSTRUCTION,     "MWAIT_INSTRUCTION" }, \
	{ EXIT_REASON_MONITOR_TRAP_FLAG,     "MONITOR_TRAP_FLAG" }, \
	{ EXIT_REASON_MONITOR_INSTRUCTION,   "MONITOR_INSTRUCTION" }, \
	{ EXIT_REASON_PAUSE_INSTRUCTION,     "PAUSE_INSTRUCTION" }, \
	{ EXIT_REASON_MCE_DURING_VMENTRY,    "MCE_DURING_VMENTRY" }, \
	{ EXIT_REASON_TPR_BELOW_THRESHOLD,   "TPR_BELOW_THRESHOLD" }, \
	{ EXIT_REASON_APIC_ACCESS,           "APIC_ACCESS" }, \
	{ EXIT_REASON_EOI_INDUCED,           "EOI_INDUCED" }, \
	{ EXIT_REASON_GDTR_IDTR,             "GDTR_IDTR" }, \
	{ EXIT_REASON_LDTR_TR,               "LDTR_TR" }, \
	{ EXIT_REASON_EPT_VIOLATION,         "EPT_VIOLATION" }, \
	{ EXIT_REASON_EPT_MISCONFIG,         "EPT_MISCONFIG" }, \
	{ EXIT_REASON_INVEPT,                "INVEPT" }, \
	{ EXIT_REASON_RDTSCP,                "RDTSCP" }, \
	{ EXIT_REASON_PREEMPTION_TIMER,      "PREEMPTION_TIMER" }, \
	{ EXIT_REASON_INVVPID,               "INVVPID" }, \
	{ EXIT_REASON_WBINVD,                "WBINVD" }, \
	{ EXIT_REASON_XSETBV,                "XSETBV" }, \
	{ EXIT_REASON_APIC_WRITE,            "APIC_WRITE" }, \
	{ EXIT_REASON_RDRAND,                "RDRAND" }, \
	{ EXIT_REASON_INVPCID,               "INVPCID" }, \
	{ EXIT_REASON_VMFUNC,                "VMFUNC" }, \
	{ EXIT_REASON_ENCLS,                 "ENCLS" }, \
	{ EXIT_REASON_RDSEED,                "RDSEED" }, \
	{ EXIT_REASON_PML_FULL,              "PML_FULL" }, \
	{ EXIT_REASON_XSAVES,                "XSAVES" }, \
	{ EXIT_REASON_XRSTORS,               "XRSTORS" }, \
	{ EXIT_REASON_UMWAIT,                "UMWAIT" }, \
	{ EXIT_REASON_TPAUSE,                "TPAUSE" }, \
	{ EXIT_REASON_BUS_LOCK,              "BUS_LOCK" }, \
	{ EXIT_REASON_NOTIFY,                "NOTIFY" }

#define VMX_EXIT_REASON_FLAGS \
	{ VMX_EXIT_REASONS_FAILED_VMENTRY,	"FAILED_VMENTRY" }

#define VMX_ABORT_SAVE_GUEST_MSR_FAIL        1
#define VMX_ABORT_LOAD_HOST_PDPTE_FAIL       2
#define VMX_ABORT_LOAD_HOST_MSR_FAIL         4

#endif /* _UAPIVMX_H */
