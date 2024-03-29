/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IDTENTRY_H
#define _ASM_X86_IDTENTRY_H

/* Interrupts/Exceptions */
#include <asm/trapnr.h>

#ifndef __ASSEMBLY__
#include <linux/entry-common.h>
#include <linux/hardirq.h>

#include <asm/irq_stack.h>

bool idtentry_enter_nmi(struct pt_regs *regs);
void idtentry_exit_nmi(struct pt_regs *regs, bool irq_state);

/**
 * DECLARE_IDTENTRY - Declare functions for simple IDT entry points
 *		      No error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares three functions:
 * - The ASM entry point: asm_##func
 * - The XEN PV trap entry point: xen_##func (maybe unused)
 * - The C handler called from the ASM entry point
 *
 * Note: This is the C variant of DECLARE_IDTENTRY(). As the name says it
 * declares the entry points for usage in C code. There is an ASM variant
 * as well which is used to emit the entry stubs in entry_32/64.S.
 */
#define DECLARE_IDTENTRY(vector, func)					\
	asmlinkage void asm_##func(void);				\
	asmlinkage void xen_asm_##func(void);				\
	__visible void func(struct pt_regs *regs)

/**
 * DEFINE_IDTENTRY - Emit code for simple IDT entry points
 * @func:	Function name of the entry point
 *
 * @func is called from ASM entry code with interrupts disabled.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 *
 * irqentry_enter() contains common code which has to be invoked before
 * arbitrary code in the body. irqentry_exit() contains common code
 * which has to run before returning to the low level assembly code.
 */
#define DEFINE_IDTENTRY(func)						\
static __always_inline void __##func(struct pt_regs *regs);		\
									\
__visible noinstr void func(struct pt_regs *regs)			\
{									\
	irqentry_state_t state = irqentry_enter(regs);			\
									\
	instrumentation_begin();					\
	__##func (regs);						\
	instrumentation_end();						\
	irqentry_exit(regs, state);					\
}									\
									\
static __always_inline void __##func(struct pt_regs *regs)

/* Special case for 32bit IRET 'trap' */
#define DECLARE_IDTENTRY_SW	DECLARE_IDTENTRY
#define DEFINE_IDTENTRY_SW	DEFINE_IDTENTRY

/**
 * DECLARE_IDTENTRY_ERRORCODE - Declare functions for simple IDT entry points
 *				Error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares three functions:
 * - The ASM entry point: asm_##func
 * - The XEN PV trap entry point: xen_##func (maybe unused)
 * - The C handler called from the ASM entry point
 *
 * Same as DECLARE_IDTENTRY, but has an extra error_code argument for the
 * C-handler.
 */
#define DECLARE_IDTENTRY_ERRORCODE(vector, func)			\
	asmlinkage void asm_##func(void);				\
	asmlinkage void xen_asm_##func(void);				\
	__visible void func(struct pt_regs *regs, unsigned long error_code)

/**
 * DEFINE_IDTENTRY_ERRORCODE - Emit code for simple IDT entry points
 *			       Error code pushed by hardware
 * @func:	Function name of the entry point
 *
 * Same as DEFINE_IDTENTRY, but has an extra error_code argument
 */
#define DEFINE_IDTENTRY_ERRORCODE(func)					\
static __always_inline void __##func(struct pt_regs *regs,		\
				     unsigned long error_code);		\
									\
__visible noinstr void func(struct pt_regs *regs,			\
			    unsigned long error_code)			\
{									\
	irqentry_state_t state = irqentry_enter(regs);			\
									\
	instrumentation_begin();					\
	__##func (regs, error_code);					\
	instrumentation_end();						\
	irqentry_exit(regs, state);					\
}									\
									\
static __always_inline void __##func(struct pt_regs *regs,		\
				     unsigned long error_code)

/**
 * DECLARE_IDTENTRY_RAW - Declare functions for raw IDT entry points
 *		      No error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY().
 */
#define DECLARE_IDTENTRY_RAW(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

/**
 * DEFINE_IDTENTRY_RAW - Emit code for raw IDT entry points
 * @func:	Function name of the entry point
 *
 * @func is called from ASM entry code with interrupts disabled.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 *
 * Contrary to DEFINE_IDTENTRY() this does not invoke the
 * idtentry_enter/exit() helpers before and after the body invocation. This
 * needs to be done in the body itself if applicable. Use if extra work
 * is required before the enter/exit() helpers are invoked.
 */
#define DEFINE_IDTENTRY_RAW(func)					\
__visible noinstr void func(struct pt_regs *regs)

/**
 * DECLARE_IDTENTRY_RAW_ERRORCODE - Declare functions for raw IDT entry points
 *				    Error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_ERRORCODE()
 */
#define DECLARE_IDTENTRY_RAW_ERRORCODE(vector, func)			\
	DECLARE_IDTENTRY_ERRORCODE(vector, func)

/**
 * DEFINE_IDTENTRY_RAW_ERRORCODE - Emit code for raw IDT entry points
 * @func:	Function name of the entry point
 *
 * @func is called from ASM entry code with interrupts disabled.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 *
 * Contrary to DEFINE_IDTENTRY_ERRORCODE() this does not invoke the
 * irqentry_enter/exit() helpers before and after the body invocation. This
 * needs to be done in the body itself if applicable. Use if extra work
 * is required before the enter/exit() helpers are invoked.
 */
#define DEFINE_IDTENTRY_RAW_ERRORCODE(func)				\
__visible noinstr void func(struct pt_regs *regs, unsigned long error_code)

/**
 * DECLARE_IDTENTRY_IRQ - Declare functions for device interrupt IDT entry
 *			  points (common/spurious)
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_ERRORCODE()
 */
#define DECLARE_IDTENTRY_IRQ(vector, func)				\
	DECLARE_IDTENTRY_ERRORCODE(vector, func)

/**
 * DEFINE_IDTENTRY_IRQ - Emit code for device interrupt IDT entry points
 * @func:	Function name of the entry point
 *
 * The vector number is pushed by the low level entry stub and handed
 * to the function as error_code argument which needs to be truncated
 * to an u8 because the push is sign extending.
 *
 * irq_enter/exit_rcu() are invoked before the function body and the
 * KVM L1D flush request is set. Stack switching to the interrupt stack
 * has to be done in the function body if necessary.
 */
#define DEFINE_IDTENTRY_IRQ(func)					\
static __always_inline void __##func(struct pt_regs *regs, u8 vector);	\
									\
__visible noinstr void func(struct pt_regs *regs,			\
			    unsigned long error_code)			\
{									\
	irqentry_state_t state = irqentry_enter(regs);			\
									\
	instrumentation_begin();					\
	irq_enter_rcu();						\
	kvm_set_cpu_l1tf_flush_l1d();					\
	__##func (regs, (u8)error_code);				\
	irq_exit_rcu();							\
	instrumentation_end();						\
	irqentry_exit(regs, state);					\
}									\
									\
static __always_inline void __##func(struct pt_regs *regs, u8 vector)

/**
 * DECLARE_IDTENTRY_SYSVEC - Declare functions for system vector entry points
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares three functions:
 * - The ASM entry point: asm_##func
 * - The XEN PV trap entry point: xen_##func (maybe unused)
 * - The C handler called from the ASM entry point
 *
 * Maps to DECLARE_IDTENTRY().
 */
#define DECLARE_IDTENTRY_SYSVEC(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

/**
 * DEFINE_IDTENTRY_SYSVEC - Emit code for system vector IDT entry points
 * @func:	Function name of the entry point
 *
 * irqentry_enter/exit() and irq_enter/exit_rcu() are invoked before the
 * function body. KVM L1D flush request is set.
 *
 * Runs the function on the interrupt stack if the entry hit kernel mode
 */
#define DEFINE_IDTENTRY_SYSVEC(func)					\
static void __##func(struct pt_regs *regs);				\
									\
__visible noinstr void func(struct pt_regs *regs)			\
{									\
	irqentry_state_t state = irqentry_enter(regs);			\
									\
	instrumentation_begin();					\
	irq_enter_rcu();						\
	kvm_set_cpu_l1tf_flush_l1d();					\
	run_sysvec_on_irqstack_cond(__##func, regs);			\
	irq_exit_rcu();							\
	instrumentation_end();						\
	irqentry_exit(regs, state);					\
}									\
									\
static noinline void __##func(struct pt_regs *regs)

/**
 * DEFINE_IDTENTRY_SYSVEC_SIMPLE - Emit code for simple system vector IDT
 *				   entry points
 * @func:	Function name of the entry point
 *
 * Runs the function on the interrupted stack. No switch to IRQ stack and
 * only the minimal __irq_enter/exit() handling.
 *
 * Only use for 'empty' vectors like reschedule IPI and KVM posted
 * interrupt vectors.
 */
#define DEFINE_IDTENTRY_SYSVEC_SIMPLE(func)				\
static __always_inline void __##func(struct pt_regs *regs);		\
									\
__visible noinstr void func(struct pt_regs *regs)			\
{									\
	irqentry_state_t state = irqentry_enter(regs);			\
									\
	instrumentation_begin();					\
	__irq_enter_raw();						\
	kvm_set_cpu_l1tf_flush_l1d();					\
	__##func (regs);						\
	__irq_exit_raw();						\
	instrumentation_end();						\
	irqentry_exit(regs, state);					\
}									\
									\
static __always_inline void __##func(struct pt_regs *regs)

/**
 * DECLARE_IDTENTRY_XENCB - Declare functions for XEN HV callback entry point
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares three functions:
 * - The ASM entry point: asm_##func
 * - The XEN PV trap entry point: xen_##func (maybe unused)
 * - The C handler called from the ASM entry point
 *
 * Maps to DECLARE_IDTENTRY(). Distinct entry point to handle the 32/64-bit
 * difference
 */
#define DECLARE_IDTENTRY_XENCB(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

#ifdef CONFIG_X86_64
/**
 * DECLARE_IDTENTRY_IST - Declare functions for IST handling IDT entry points
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_RAW, but declares also the NOIST C handler
 * which is called from the ASM entry point on user mode entry
 */
#define DECLARE_IDTENTRY_IST(vector, func)				\
	DECLARE_IDTENTRY_RAW(vector, func);				\
	__visible void noist_##func(struct pt_regs *regs)

/**
 * DECLARE_IDTENTRY_VC - Declare functions for the VC entry point
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_RAW_ERRORCODE, but declares also the
 * safe_stack C handler.
 */
#define DECLARE_IDTENTRY_VC(vector, func)				\
	DECLARE_IDTENTRY_RAW_ERRORCODE(vector, func);			\
	__visible noinstr void ist_##func(struct pt_regs *regs, unsigned long error_code);	\
	__visible noinstr void safe_stack_##func(struct pt_regs *regs, unsigned long error_code)

/**
 * DEFINE_IDTENTRY_IST - Emit code for IST entry points
 * @func:	Function name of the entry point
 *
 * Maps to DEFINE_IDTENTRY_RAW
 */
#define DEFINE_IDTENTRY_IST(func)					\
	DEFINE_IDTENTRY_RAW(func)

/**
 * DEFINE_IDTENTRY_NOIST - Emit code for NOIST entry points which
 *			   belong to a IST entry point (MCE, DB)
 * @func:	Function name of the entry point. Must be the same as
 *		the function name of the corresponding IST variant
 *
 * Maps to DEFINE_IDTENTRY_RAW().
 */
#define DEFINE_IDTENTRY_NOIST(func)					\
	DEFINE_IDTENTRY_RAW(noist_##func)

/**
 * DECLARE_IDTENTRY_DF - Declare functions for double fault
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_RAW_ERRORCODE
 */
#define DECLARE_IDTENTRY_DF(vector, func)				\
	DECLARE_IDTENTRY_RAW_ERRORCODE(vector, func)

/**
 * DEFINE_IDTENTRY_DF - Emit code for double fault
 * @func:	Function name of the entry point
 *
 * Maps to DEFINE_IDTENTRY_RAW_ERRORCODE
 */
#define DEFINE_IDTENTRY_DF(func)					\
	DEFINE_IDTENTRY_RAW_ERRORCODE(func)

/**
 * DEFINE_IDTENTRY_VC_SAFE_STACK - Emit code for VMM communication handler
				   which runs on a safe stack.
 * @func:	Function name of the entry point
 *
 * Maps to DEFINE_IDTENTRY_RAW_ERRORCODE
 */
#define DEFINE_IDTENTRY_VC_SAFE_STACK(func)				\
	DEFINE_IDTENTRY_RAW_ERRORCODE(safe_stack_##func)

/**
 * DEFINE_IDTENTRY_VC_IST - Emit code for VMM communication handler
			    which runs on the VC fall-back stack
 * @func:	Function name of the entry point
 *
 * Maps to DEFINE_IDTENTRY_RAW_ERRORCODE
 */
#define DEFINE_IDTENTRY_VC_IST(func)				\
	DEFINE_IDTENTRY_RAW_ERRORCODE(ist_##func)

/**
 * DEFINE_IDTENTRY_VC - Emit code for VMM communication handler
 * @func:	Function name of the entry point
 *
 * Maps to DEFINE_IDTENTRY_RAW_ERRORCODE
 */
#define DEFINE_IDTENTRY_VC(func)					\
	DEFINE_IDTENTRY_RAW_ERRORCODE(func)

#else	/* CONFIG_X86_64 */

/**
 * DECLARE_IDTENTRY_DF - Declare functions for double fault 32bit variant
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares two functions:
 * - The ASM entry point: asm_##func
 * - The C handler called from the C shim
 */
//#define DECLARE_IDTENTRY_DF(vector, func)				\
//	asmlinkage void asm_##func(void);				\
//	__visible void func(struct pt_regs *regs,			\
//			    unsigned long error_code,			\
//			    unsigned long address)

/**
 * DEFINE_IDTENTRY_DF - Emit code for double fault on 32bit
 * @func:	Function name of the entry point
 *
 * This is called through the doublefault shim which already provides
 * cr2 in the address argument.
 */
//#define DEFINE_IDTENTRY_DF(func)					\
//__visible noinstr void func(struct pt_regs *regs,			\
//			    unsigned long error_code,			\
//			    unsigned long address)

#endif	/* !CONFIG_X86_64 */

/* C-Code mapping */
#define DECLARE_IDTENTRY_NMI		DECLARE_IDTENTRY_RAW
#define DEFINE_IDTENTRY_NMI		DEFINE_IDTENTRY_RAW

#ifdef CONFIG_X86_64
#define DECLARE_IDTENTRY_MCE		DECLARE_IDTENTRY_IST
#define DEFINE_IDTENTRY_MCE		DEFINE_IDTENTRY_IST
#define DEFINE_IDTENTRY_MCE_USER	DEFINE_IDTENTRY_NOIST

#define DECLARE_IDTENTRY_DEBUG		DECLARE_IDTENTRY_IST
#define DEFINE_IDTENTRY_DEBUG		DEFINE_IDTENTRY_IST
#define DEFINE_IDTENTRY_DEBUG_USER	DEFINE_IDTENTRY_NOIST
#endif

#else /* !__ASSEMBLY__ */

/*
 * The ASM variants for DECLARE_IDTENTRY*() which emit the ASM entry stubs.
 */
#define DECLARE_IDTENTRY(vector, func)					\
	idtentry vector asm_##func func has_error_code=0

#define DECLARE_IDTENTRY_ERRORCODE(vector, func)			\
	idtentry vector asm_##func func has_error_code=1

/* Special case for 32bit IRET 'trap'. Do not emit ASM code */
#define DECLARE_IDTENTRY_SW(vector, func)

#define DECLARE_IDTENTRY_RAW(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

#define DECLARE_IDTENTRY_RAW_ERRORCODE(vector, func)			\
	DECLARE_IDTENTRY_ERRORCODE(vector, func)

/* Entries for common/spurious (device) interrupts */
#define DECLARE_IDTENTRY_IRQ(vector, func)				\
	idtentry_irq vector func

/* System vector entries */
#define DECLARE_IDTENTRY_SYSVEC(vector, func)				\
	idtentry_sysvec vector func

#ifdef CONFIG_X86_64
# define DECLARE_IDTENTRY_MCE(vector, func)				\
	idtentry_mce_db vector asm_##func func

# define DECLARE_IDTENTRY_DEBUG(vector, func)				\
	idtentry_mce_db vector asm_##func func

# define DECLARE_IDTENTRY_DF(vector, func)				\
	idtentry_df vector asm_##func func

# define DECLARE_IDTENTRY_XENCB(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

# define DECLARE_IDTENTRY_VC(vector, func)				\
	idtentry_vc vector asm_##func func

#else
//# define DECLARE_IDTENTRY_MCE(vector, func)				\
//	DECLARE_IDTENTRY(vector, func)
//
///* No ASM emitted for DF as this goes through a C shim */
//# define DECLARE_IDTENTRY_DF(vector, func)
//
///* No ASM emitted for XEN hypervisor callback */
//# define DECLARE_IDTENTRY_XENCB(vector, func)

#endif

/* No ASM code emitted for NMI */
#define DECLARE_IDTENTRY_NMI(vector, func)

/*
 * ASM code to emit the common vector entry stubs where each stub is
 * packed into 8 bytes.
 *
 * Note, that the 'pushq imm8' is emitted via '.byte 0x6a, vector' because
 * GCC treats the local vector variable as unsigned int and would expand
 * all vectors above 0x7F to a 5 byte push. The original code did an
 * adjustment of the vector number to be in the signed byte range to avoid
 * this. While clever it's mindboggling counterintuitive and requires the
 * odd conversion back to a real vector number in the C entry points. Using
 * .byte achieves the same thing and the only fixup needed in the C entry
 * point is to mask off the bits above bit 7 because the push is sign
 * extending.
 */
	.align 8
SYM_CODE_START(irq_entries_start)
    vector=FIRST_EXTERNAL_VECTOR
    .rept (FIRST_SYSTEM_VECTOR - FIRST_EXTERNAL_VECTOR)
	UNWIND_HINT_IRET_REGS
0 :
	.byte	0x6a, vector
	jmp	asm_common_interrupt
	nop
	/* Ensure that the above is 8 bytes max */
	. = 0b + 8
	vector = vector+1
    .endr
SYM_CODE_END(irq_entries_start)

#ifdef CONFIG_X86_LOCAL_APIC
	.align 8
SYM_CODE_START(spurious_entries_start)
    vector=FIRST_SYSTEM_VECTOR
    .rept (NR_VECTORS - FIRST_SYSTEM_VECTOR)
	UNWIND_HINT_IRET_REGS
0 :
	.byte	0x6a, vector
	jmp	asm_spurious_interrupt
	nop
	/* Ensure that the above is 8 bytes max */
	. = 0b + 8
	vector = vector+1
    .endr
SYM_CODE_END(spurious_entries_start)
#endif

#endif /* __ASSEMBLY__ */
{}/* +++ */
/*
 * The actual entry points. Note that DECLARE_IDTENTRY*() serves two
 * purposes:
 *  - provide the function declarations when included from C-Code
 *  - emit the ASM stubs when included from entry_32/64.S
 *
 * This avoids duplicate defines and ensures that everything is consistent.
 */

/*
 * Dummy trap number so the low level ASM macro vector number checks do not
 * match which results in emitting plain IDTENTRY stubs without bells and
 * whistels.
 */
#define X86_TRAP_OTHER		0xFFFF
{}/* +++ */
/* Simple exception entry points. No hardware error code */
DECLARE_IDTENTRY(X86_TRAP_DE,		exc_divide_error);
DECLARE_IDTENTRY(X86_TRAP_OF,		exc_overflow);
DECLARE_IDTENTRY(X86_TRAP_BR,		exc_bounds);
DECLARE_IDTENTRY(X86_TRAP_NM,		exc_device_not_available);
DECLARE_IDTENTRY(X86_TRAP_OLD_MF,	exc_coproc_segment_overrun);
DECLARE_IDTENTRY(X86_TRAP_SPURIOUS,	exc_spurious_interrupt_bug);
DECLARE_IDTENTRY(X86_TRAP_MF,		exc_coprocessor_error);
DECLARE_IDTENTRY(X86_TRAP_XF,		exc_simd_coprocessor_error);

/* 32bit software IRET trap. Do not emit ASM code */
DECLARE_IDTENTRY_SW(X86_TRAP_IRET,	iret_error);

/* Simple exception entries with error code pushed by hardware */
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_TS,	exc_invalid_tss);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_NP,	exc_segment_not_present);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_SS,	exc_stack_segment);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_GP,	exc_general_protection);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_AC,	exc_alignment_check);

/* Raw exception entries which need extra work */
DECLARE_IDTENTRY_RAW(X86_TRAP_UD,		exc_invalid_op);
DECLARE_IDTENTRY_RAW(X86_TRAP_BP,		exc_int3);
DECLARE_IDTENTRY_RAW_ERRORCODE(X86_TRAP_PF,	exc_page_fault);

#ifdef CONFIG_X86_MCE
#ifdef CONFIG_X86_64
DECLARE_IDTENTRY_MCE(X86_TRAP_MC,	exc_machine_check);
#else
//DECLARE_IDTENTRY_RAW(X86_TRAP_MC,	exc_machine_check);
#endif
#endif

/* NMI */
DECLARE_IDTENTRY_NMI(X86_TRAP_NMI,	exc_nmi);
#ifdef CONFIG_XEN_PV
DECLARE_IDTENTRY_RAW(X86_TRAP_NMI,	xenpv_exc_nmi);
#endif

/* #DB */
#ifdef CONFIG_X86_64
DECLARE_IDTENTRY_DEBUG(X86_TRAP_DB,	exc_debug);
#else
//DECLARE_IDTENTRY_RAW(X86_TRAP_DB,	exc_debug);
#endif
#ifdef CONFIG_XEN_PV
DECLARE_IDTENTRY_RAW(X86_TRAP_DB,	xenpv_exc_debug);
#endif

/* #DF */
DECLARE_IDTENTRY_DF(X86_TRAP_DF,	exc_double_fault);

/* #VC */
#ifdef CONFIG_AMD_MEM_ENCRYPT
DECLARE_IDTENTRY_VC(X86_TRAP_VC,	exc_vmm_communication);
#endif

#ifdef CONFIG_XEN_PV
DECLARE_IDTENTRY_XENCB(X86_TRAP_OTHER,	exc_xen_hypervisor_callback);
DECLARE_IDTENTRY_RAW(X86_TRAP_OTHER,	exc_xen_unknown_trap);
#endif

/* Device interrupts common/spurious */
DECLARE_IDTENTRY_IRQ(X86_TRAP_OTHER,	common_interrupt);
#ifdef CONFIG_X86_LOCAL_APIC
DECLARE_IDTENTRY_IRQ(X86_TRAP_OTHER,	spurious_interrupt);
#endif

/* System vector entry points */
#ifdef CONFIG_X86_LOCAL_APIC
DECLARE_IDTENTRY_SYSVEC(ERROR_APIC_VECTOR,		sysvec_error_interrupt);
DECLARE_IDTENTRY_SYSVEC(SPURIOUS_APIC_VECTOR,		sysvec_spurious_apic_interrupt);
DECLARE_IDTENTRY_SYSVEC(LOCAL_TIMER_VECTOR,		sysvec_apic_timer_interrupt);
#ifdef rtoax_debug_sysvec_apic_timer_interrupt //++++
asmlinkage void asm_sysvec_apic_timer_interrupt(void); //++++
asmlinkage void xen_asm_sysvec_apic_timer_interrupt(void); //++++
__visible void sysvec_apic_timer_interrupt(struct pt_regs *regs); //++++
#endif //sysvec_apic_timer_interrupt

DECLARE_IDTENTRY_SYSVEC(X86_PLATFORM_IPI_VECTOR,	sysvec_x86_platform_ipi);
#endif

#ifdef CONFIG_SMP
/**
 * @brief 核间中断
 *
 */
DECLARE_IDTENTRY(RESCHEDULE_VECTOR,			sysvec_reschedule_ipi);
DECLARE_IDTENTRY_SYSVEC(IRQ_MOVE_CLEANUP_VECTOR,	sysvec_irq_move_cleanup);
DECLARE_IDTENTRY_SYSVEC(REBOOT_VECTOR,			sysvec_reboot);
DECLARE_IDTENTRY_SYSVEC(CALL_FUNCTION_SINGLE_VECTOR,	sysvec_call_function_single);
DECLARE_IDTENTRY_SYSVEC(CALL_FUNCTION_VECTOR,		sysvec_call_function);
#endif

#ifdef CONFIG_X86_LOCAL_APIC
# ifdef CONFIG_X86_MCE_THRESHOLD
DECLARE_IDTENTRY_SYSVEC(THRESHOLD_APIC_VECTOR,		sysvec_threshold);
# endif

# ifdef CONFIG_X86_MCE_AMD
DECLARE_IDTENTRY_SYSVEC(DEFERRED_ERROR_VECTOR,		sysvec_deferred_error);
# endif

# ifdef CONFIG_X86_THERMAL_VECTOR
DECLARE_IDTENTRY_SYSVEC(THERMAL_APIC_VECTOR,		sysvec_thermal);
# endif

# ifdef CONFIG_IRQ_WORK
DECLARE_IDTENTRY_SYSVEC(IRQ_WORK_VECTOR,		sysvec_irq_work);
# endif
#endif

#ifdef CONFIG_HAVE_KVM
DECLARE_IDTENTRY_SYSVEC(POSTED_INTR_VECTOR,		sysvec_kvm_posted_intr_ipi);
DECLARE_IDTENTRY_SYSVEC(POSTED_INTR_WAKEUP_VECTOR,	sysvec_kvm_posted_intr_wakeup_ipi);
DECLARE_IDTENTRY_SYSVEC(POSTED_INTR_NESTED_VECTOR,	sysvec_kvm_posted_intr_nested_ipi);
#endif

#if IS_ENABLED(CONFIG_HYPERV)
DECLARE_IDTENTRY_SYSVEC(HYPERVISOR_CALLBACK_VECTOR,	sysvec_hyperv_callback);
DECLARE_IDTENTRY_SYSVEC(HYPERV_REENLIGHTENMENT_VECTOR,	sysvec_hyperv_reenlightenment);
DECLARE_IDTENTRY_SYSVEC(HYPERV_STIMER0_VECTOR,	sysvec_hyperv_stimer0);
#endif

#if IS_ENABLED(CONFIG_ACRN_GUEST)
DECLARE_IDTENTRY_SYSVEC(HYPERVISOR_CALLBACK_VECTOR,	sysvec_acrn_hv_callback);
#endif

#ifdef CONFIG_XEN_PVHVM
DECLARE_IDTENTRY_SYSVEC(HYPERVISOR_CALLBACK_VECTOR,	sysvec_xen_hvm_callback);
#endif

#ifdef CONFIG_KVM_GUEST
DECLARE_IDTENTRY_SYSVEC(HYPERVISOR_CALLBACK_VECTOR,	sysvec_kvm_asyncpf_interrupt);
#endif

#undef X86_TRAP_OTHER

#ifdef __rtoax______________________________________//<asm/idtentry.h> gcc -E 展开

#ifdef __ASSEMBLY__

 .align 8
SYM_CODE_START(irq_entries_start)
    vector=FIRST_EXTERNAL_VECTOR
    .rept (FIRST_SYSTEM_VECTOR - FIRST_EXTERNAL_VECTOR)
 UNWIND_HINT_IRET_REGS
0 :
 .byte 0x6a, vector
 jmp asm_common_interrupt
 nop

 . = 0b + 8
 vector = vector+1
    .endr
SYM_CODE_END(irq_entries_start)
    {};//+++
/**
 * @brief	操作系统预留的一些 idt
 *
 */
idtentry X86_TRAP_DE asm_exc_divide_error exc_divide_error has_error_code=0;
idtentry X86_TRAP_OF asm_exc_overflow exc_overflow has_error_code=0;
idtentry X86_TRAP_BR asm_exc_bounds exc_bounds has_error_code=0;
idtentry X86_TRAP_NM asm_exc_device_not_available exc_device_not_available has_error_code=0;
idtentry X86_TRAP_OLD_MF asm_exc_coproc_segment_overrun exc_coproc_segment_overrun has_error_code=0;
idtentry X86_TRAP_SPURIOUS asm_exc_spurious_interrupt_bug exc_spurious_interrupt_bug has_error_code=0;
idtentry X86_TRAP_MF asm_exc_coprocessor_error exc_coprocessor_error has_error_code=0;
idtentry X86_TRAP_XF asm_exc_simd_coprocessor_error exc_simd_coprocessor_error has_error_code=0;


idtentry X86_TRAP_TS asm_exc_invalid_tss exc_invalid_tss has_error_code=1;
idtentry X86_TRAP_NP asm_exc_segment_not_present exc_segment_not_present has_error_code=1;
idtentry X86_TRAP_SS asm_exc_stack_segment exc_stack_segment has_error_code=1;
idtentry X86_TRAP_GP asm_exc_general_protection exc_general_protection has_error_code=1;
idtentry X86_TRAP_AC asm_exc_alignment_check exc_alignment_check has_error_code=1;


idtentry X86_TRAP_UD asm_exc_invalid_op exc_invalid_op has_error_code=0;
idtentry X86_TRAP_BP asm_exc_int3 exc_int3 has_error_code=0;
idtentry X86_TRAP_PF asm_exc_page_fault exc_page_fault has_error_code=1;
idtentry X86_TRAP_DB asm_exc_debug exc_debug has_error_code=0;


idtentry_irq 0xFFFF common_interrupt;
idtentry_sysvec HYPERVISOR_CALLBACK_VECTOR sysvec_hyperv_callback;
idtentry_sysvec HYPERV_REENLIGHTENMENT_VECTOR sysvec_hyperv_reenlightenment;
idtentry_sysvec HYPERV_STIMER0_VECTOR sysvec_hyperv_stimer0;

idtentry_sysvec HYPERVISOR_CALLBACK_VECTOR sysvec_acrn_hv_callback;

#else //__ASSEMBLY__


asmlinkage void asm_exc_divide_error(void);
 asmlinkage void xen_asm_exc_divide_error(void);
 __visible void exc_divide_error(struct pt_regs *regs);

asmlinkage void asm_exc_overflow(void);
 asmlinkage void xen_asm_exc_overflow(void);
 __visible void exc_overflow(struct pt_regs *regs);

asmlinkage void asm_exc_bounds(void);
 asmlinkage void xen_asm_exc_bounds(void);
 __visible void exc_bounds(struct pt_regs *regs);

asmlinkage void asm_exc_device_not_available(void);
 asmlinkage void xen_asm_exc_device_not_available(void);
 __visible void exc_device_not_available(struct pt_regs *regs);

asmlinkage void asm_exc_coproc_segment_overrun(void);
 asmlinkage void xen_asm_exc_coproc_segment_overrun(void);
 __visible void exc_coproc_segment_overrun(struct pt_regs *regs);

asmlinkage void asm_exc_spurious_interrupt_bug(void);
 asmlinkage void xen_asm_exc_spurious_interrupt_bug(void);
 __visible void exc_spurious_interrupt_bug(struct pt_regs *regs);

asmlinkage void asm_exc_coprocessor_error(void);
 asmlinkage void xen_asm_exc_coprocessor_error(void);
 __visible void exc_coprocessor_error(struct pt_regs *regs);

asmlinkage void asm_exc_simd_coprocessor_error(void);
 asmlinkage void xen_asm_exc_simd_coprocessor_error(void);
 __visible void exc_simd_coprocessor_error(struct pt_regs *regs);



asmlinkage void asm_iret_error(void);
 asmlinkage void xen_asm_iret_error(void);
 __visible void iret_error(struct pt_regs *regs);



asmlinkage void asm_exc_invalid_tss(void);
 asmlinkage void xen_asm_exc_invalid_tss(void);
 __visible void exc_invalid_tss(struct pt_regs *regs, unsigned long error_code);

asmlinkage void asm_exc_segment_not_present(void);
 asmlinkage void xen_asm_exc_segment_not_present(void);
 __visible void exc_segment_not_present(struct pt_regs *regs, unsigned long error_code);

asmlinkage void asm_exc_stack_segment(void);
 asmlinkage void xen_asm_exc_stack_segment(void);
 __visible void exc_stack_segment(struct pt_regs *regs, unsigned long error_code);

asmlinkage void asm_exc_general_protection(void);
 asmlinkage void xen_asm_exc_general_protection(void);
 __visible void exc_general_protection(struct pt_regs *regs, unsigned long error_code);

asmlinkage void asm_exc_alignment_check(void);
 asmlinkage void xen_asm_exc_alignment_check(void);
 __visible void exc_alignment_check(struct pt_regs *regs, unsigned long error_code);



asmlinkage void asm_exc_invalid_op(void);
 asmlinkage void xen_asm_exc_invalid_op(void);
 __visible void exc_invalid_op(struct pt_regs *regs);

asmlinkage void asm_exc_int3(void);
 asmlinkage void xen_asm_exc_int3(void);
 __visible void exc_int3(struct pt_regs *regs);

asmlinkage void asm_exc_page_fault(void);
 asmlinkage void xen_asm_exc_page_fault(void);
 __visible void exc_page_fault(struct pt_regs *regs, unsigned long error_code);

asmlinkage void asm_exc_nmi(void);
 asmlinkage void xen_asm_exc_nmi(void);
 __visible void exc_nmi(struct pt_regs *regs);

asmlinkage void asm_exc_debug(void);
 asmlinkage void xen_asm_exc_debug(void);
 __visible void exc_debug(struct pt_regs *regs);


asmlinkage void asm_exc_double_fault(void);
 __visible void exc_double_fault(struct pt_regs *regs, unsigned long error_code, unsigned long address);

asmlinkage void asm_common_interrupt(void);
 asmlinkage void xen_asm_common_interrupt(void);
 __visible void common_interrupt(struct pt_regs *regs, unsigned long error_code);

asmlinkage void asm_sysvec_hyperv_callback(void);
 asmlinkage void xen_asm_sysvec_hyperv_callback(void);
 __visible void sysvec_hyperv_callback(struct pt_regs *regs);

asmlinkage void asm_sysvec_hyperv_reenlightenment(void);
 asmlinkage void xen_asm_sysvec_hyperv_reenlightenment(void);
 __visible void sysvec_hyperv_reenlightenment(struct pt_regs *regs);

asmlinkage void asm_sysvec_hyperv_stimer0(void);
 asmlinkage void xen_asm_sysvec_hyperv_stimer0(void);
 __visible void sysvec_hyperv_stimer0(struct pt_regs *regs);


asmlinkage void asm_sysvec_acrn_hv_callback(void);
 asmlinkage void xen_asm_sysvec_acrn_hv_callback(void);
 __visible void sysvec_acrn_hv_callback(struct pt_regs *regs);


#endif

#endif

#endif
