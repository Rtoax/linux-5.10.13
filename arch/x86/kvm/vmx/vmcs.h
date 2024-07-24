/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_VMCS_H
#define __KVM_X86_VMX_VMCS_H

#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/nospec.h>

#include <asm/kvm.h>
#include <asm/vmx.h>

#include "capabilities.h"

/**
 *
 */
struct vmcs_hdr {
	/**
	 * 修正标识符
	 * 标识不同的 vmcs 版本
	 */
	u32 revision_id:31;
	/**
	 *
	 * 在 alloc_vmcs_cpu() 中赋值
	 */
	u32 shadow_vmcs:1;
};

/**
 *  https://zhuanlan.zhihu.com/p/49257842
 *  -------------------------------------
 *  在虚拟化中，为了实现vCPU，既要模拟CPU的运行，又要记录vCPU的状态
 *  （包括对vCPU运行的控制信息），在Intel x86处理器的VMX（Virtual
 *  Machine Extension）功能中，通过引入根运行模式（VMX root operation）
 *  和非根模式（VMX non-root operation），直接让vCPU运行在逻辑CPU上，
 *  在软件上省去了对vCPU运行的模拟，同时也大大提升了性能。
 *  剩下的就是对vCPU状态的记录了，为此Intel引入了VMCS（Virtual Machine
 *  Control Structure）功能。
 *
 *  VMCS（Virtual Machine Control Structure）是Intel x86处理器中实现CPU虚拟化，
 *  记录vCPU状态的一个关键数据结构。
 *
 *  1. 每个任务有自己的上下文
 *  2. 不同模式(Guest,Host)也需要保存自己的上下文
 *
 *  VMX 设计了一个保存上下文的数据结构
 *
 *  vmcs 保存了两大类数据
 *  1. Host的状态和 Guest 的状态
 *  2. 控制Guest 运行时的行为
 *
 *  https://zhuanlan.zhihu.com/p/49257842
 *  1.[Guest-state area]
 *  2.[Host-state area]
 *  3.[VM-execution control fields]
 *  4.[VM-exit control fields]
 *  5.[VM-entry control fields]
 *  6.[VM-exit information fields]
 *
 * 1. Guest State 区域：进行 VM Entry 时，虚拟机处理器的状态从这个区加载
 * 2. Host State 区域：发生 VM Exit 时，却换到 VMM 上下文，处理器的状态从这个区加载
 * 3. VM-Execution 区域：控制处理器在进入 VM Entry 之后的处理行为
 * 4. VM-Exit 控制区：指定 VM 在发生 VMExit 时的行为，如寄存器的保存
 * 5. VM-Entry 控制区：用来指定 VM 在发生 VM Entry 时的行为，如寄存器的加载
 * 6. VM-Exit 信息区：包含最近产生的 VM Exit 信息，如退出原因以及相应的数据
 *
 *  1. [Guest-state area]，即vCPU的状态信息，包括vCPU的基本运行环境（如通用寄存器等）
 *      和一些非寄存器信息，如当前vCPU是否接收中断，是否有挂起的exception，VMCS的状态等等。
 *  2. [Host-state area]，即主机物理CPU的状态信息，因为物理CPU是在主机CPU和vCPU之间来回切换
 *      运行的，所以在VMCS中既要记录vCPU的状态，也需要记录主机CPU的状态，这样vCPU才能有足
 *      够的信息恢复到原来主机CPU的状态，继续主机CPU的运行。其包含的具体信息和前面记录的
 *      vCPU的状态信息大体相同。
 *  3. [VM-execution control fields]，该方面信息主要用于对vCPU的运行行为进行控制，这是VMM对
 *      vCPU进行配置最复杂的一部分，如
 *      3.1 控制vCPU在接收到某些中断事件的时候，是否直接在vCPU中处理掉，即虚拟机直接处理掉
 *          该中断事件还是需要退出到VMM中，让VMM去处理该中断事件。
 *      3.2 是否使用EPT（Extended Page Table）功能。
 *      3.3 是否允许vCPU直接访问某些I/O口，MSR寄存器等资源。
 *      3.4 vCPU执行某些指令的时候，是否会出发VM Exit等等。
 *  4. [VM-exit control fields]，即对VM Exit的行为进行控制，如VM Exit的时候对vCPU来说需要保存
 *      哪些MSR寄存器，对于主机CPU来说需要恢复哪些MSR寄存器。
 *  5. [VM-entry control fields]，即对VM Entry的行为进行控制，如需要保存和恢复哪些MSR寄存器，
 *      是否需要向vCPU注入中断和异常等事件（VM Exit的时候不需要向主机CPU注入中断/异常事件，
 *      因为可以让那些事件直接触发VM Exit）。
 *  6. [VM-exit information fields]，即记录下发生VM Exit发生的原因及一些必要的信息，方便于VMM
 *      对VM Exit事件进行处理，如vCPU访问了特权资源造成VM Exit，则在该区域中，会记录下这个
 *      特权资源的类型，如I/O地址，内存地址或者是MSR寄存器，并且也会记录下该特权资源的地址，
 *      好让VMM对该特权资源进行模拟。
 */
struct vmcs {
	struct vmcs_hdr hdr;
	/**
	 * 当 VM 发生错误时候，会产生 VMX-abort
	 */
	u32 abort;
	/**
	 *  见 `enum vmcs_field` 和 `vmcs_read64()`
	 *  VMM 通过 vmread/vmwrite 在这里读写
	 */
	char data[];
};

/**
 * 初始化/分配： alloc_kvm_area()->alloc_vmcs_cpu()
 * VMCS 配置： vmcs_config, 由 setup_vmcs_config() 初始化
 */

DECLARE_PER_CPU(struct vmcs *, current_vmcs);


/*
 * vmcs_host_state tracks registers that are loaded from the VMCS on VMEXIT
 * and whose values change infrequently, but are not constant.  I.e. this is
 * used as a write-through cache of the corresponding VMCS fields.
 */
struct vmcs_host_state {
	/**
	 *
	 */
	unsigned long cr3;	/* May not match real cr3 */
	unsigned long cr4;	/* May not match real cr4 */
	unsigned long gs_base;
	unsigned long fs_base;
	unsigned long rsp;

	u16           fs_sel, gs_sel, ldt_sel;
#ifdef CONFIG_X86_64
	u16           ds_sel, es_sel;
#endif
};

struct vmcs_controls_shadow {
	u32 vm_entry;
	u32 vm_exit;
	u32 pin;
	u32 exec;
	u32 secondary_exec;
};

/*
 * Track a VMCS that may be loaded on a certain CPU. If it is (cpu!=-1), also
 * remember whether it was VMLAUNCHed, and maintain a linked list of all VMCSs
 * loaded on this CPU (so we can clear them if the CPU goes down).
 *
 * 跟踪一个可能在特定 CPU 上加载的 VMCS
 * 见：
 * - alloc_loaded_vmcs(struct loaded_vmcs *__loaded_vmcs)
 */
struct loaded_vmcs {
	struct vmcs *vmcs;
	struct vmcs *shadow_vmcs;
	int cpu;
	/**
	 * 如果首次运行 Guest，则使用 vmlaunch，否则运行 vmresume 指令
	 * 见 vmx_vcpu_enter_exit() -> __vmx_vcpu_run()
	 */
	bool launched;
	bool nmi_known_unmasked;
	bool hv_timer_soft_disabled;
	/* Support for vnmi-less CPUs */
	int soft_vnmi_blocked;
	ktime_t entry_time;
	s64 vnmi_blocked_time;
	unsigned long *msr_bitmap;
	struct list_head loaded_vmcss_on_cpu_link;
	struct vmcs_host_state host_state;
	struct vmcs_controls_shadow controls_shadow;
};

static inline bool is_intr_type(u32 intr_info, u32 type)
{
	const u32 mask = INTR_INFO_VALID_MASK | INTR_INFO_INTR_TYPE_MASK;

	return (intr_info & mask) == (INTR_INFO_VALID_MASK | type);
}

static inline bool is_intr_type_n(u32 intr_info, u32 type, u8 vector)
{
	const u32 mask = INTR_INFO_VALID_MASK | INTR_INFO_INTR_TYPE_MASK |
			 INTR_INFO_VECTOR_MASK;

	return (intr_info & mask) == (INTR_INFO_VALID_MASK | type | vector);
}

static inline bool is_exception_n(u32 intr_info, u8 vector)
{
	return is_intr_type_n(intr_info, INTR_TYPE_HARD_EXCEPTION, vector);
}

static inline bool is_debug(u32 intr_info)
{
	return is_exception_n(intr_info, DB_VECTOR);
}

static inline bool is_breakpoint(u32 intr_info)
{
	return is_exception_n(intr_info, BP_VECTOR);
}

static inline bool is_page_fault(u32 intr_info)
{
	return is_exception_n(intr_info, PF_VECTOR);
}

/**
 * #UD
 */
static inline bool is_invalid_opcode(u32 intr_info)
{
	return is_exception_n(intr_info, UD_VECTOR);
}

static inline bool is_gp_fault(u32 intr_info)
{
	return is_exception_n(intr_info, GP_VECTOR);
}

static inline bool is_machine_check(u32 intr_info)
{
	return is_exception_n(intr_info, MC_VECTOR);
}

/* Undocumented: icebp/int1 */
static inline bool is_icebp(u32 intr_info)
{
	return is_intr_type(intr_info, INTR_TYPE_PRIV_SW_EXCEPTION);
}

static inline bool is_nmi(u32 intr_info)
{
	return is_intr_type(intr_info, INTR_TYPE_NMI_INTR);
}

static inline bool is_external_intr(u32 intr_info)
{
	return is_intr_type(intr_info, INTR_TYPE_EXT_INTR);
}

static inline bool is_exception_with_error_code(u32 intr_info)
{
	const u32 mask = INTR_INFO_VALID_MASK | INTR_INFO_DELIVER_CODE_MASK;

	return (intr_info & mask) == mask;
}

enum vmcs_field_width {
	VMCS_FIELD_WIDTH_U16 = 0,
	VMCS_FIELD_WIDTH_U64 = 1,
	VMCS_FIELD_WIDTH_U32 = 2,
	VMCS_FIELD_WIDTH_NATURAL_WIDTH = 3
};

static inline int vmcs_field_width(unsigned long field)
{
	if (0x1 & field)	/* the *_HIGH fields are all 32 bit */
		return VMCS_FIELD_WIDTH_U32;
	return (field >> 13) & 0x3;
}

static inline int vmcs_field_readonly(unsigned long field)
{
	return (((field >> 10) & 0x3) == 1);
}

#endif /* __KVM_X86_VMX_VMCS_H */
