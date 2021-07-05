/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _X86_IRQFLAGS_H_
#define _X86_IRQFLAGS_H_

#include <asm/processor-flags.h>

#ifndef __ASSEMBLY__

#include <asm/nospec-branch.h>

/* Provide __cpuidle; we can't safely include <linux/cpu.h> */
#define __cpuidle __section(".cpuidle.text")    /* CPUIDLE_TEXT */

/*
 * Interrupt control:
 */

/* Declaration required for gcc < 4.9 to prevent -Werror=missing-prototypes */
extern inline unsigned long native_save_fl(void);
extern __always_inline unsigned long native_save_fl(void)
{
	unsigned long flags;

	/*
	 * "=rm" is safe here, because "pop" adjusts the stack before
	 * it evaluates its effective address -- this is part of the
	 * documented behavior of the "pop" instruction.
	 */
	asm volatile("# __raw_save_flags\n\t"
		     "pushf ; pop %0"
		     : "=rm" (flags)
		     : /* no input */
		     : "memory");

	return flags;
}

extern inline void native_restore_fl(unsigned long flags)
{
	asm volatile("push %0 ; popf"
		     : /* no output */
		     :"g" (flags)
		     :"memory", "cc");
}

//`cli` 指令将清除[IF](http://en.wikipedia.org/wiki/Interrupt_flag) 标志位
static __always_inline void native_irq_disable(void)
{
	asm volatile("cli": : :"memory");/* cli-关中断 close irq*/
}

//`sti` 指令使能了中断
static __always_inline void native_irq_enable(void)
{
	asm volatile("sti": : :"memory");/* sti-开中断 start irq*/
}

static inline __cpuidle void native_safe_halt(void)
{
	mds_idle_clear_cpu_buffers();
	asm volatile("sti; hlt": : :"memory");
}

static inline __cpuidle void native_halt(void)
{
	mds_idle_clear_cpu_buffers();
	asm volatile("hlt": : :"memory");
}

#endif

#ifdef CONFIG_PARAVIRT_XXL
#include <asm/paravirt.h>
#else
#ifndef __ASSEMBLY__
#include <linux/types.h>

static __always_inline unsigned long arch_local_save_flags(void)
{
	return native_save_fl();
}

static __always_inline void arch_local_irq_restore(unsigned long flags)
{
	native_restore_fl(flags);
}

static __always_inline void arch_local_irq_disable(void)/* 中断 */
{
	native_irq_disable();
}

static __always_inline void arch_local_irq_enable(void)/* 中断 */
{
	native_irq_enable();
}

/*
 * Used in the idle loop; sti takes one instruction cycle
 * to complete:
 */
static inline __cpuidle void arch_safe_halt(void)
{
	native_safe_halt();
}

/*
 * Used when interrupts are already enabled or to
 * shutdown the processor:
 */
static inline __cpuidle void halt(void)
{
	native_halt();
}

/*
 * For spinlocks, etc:
 */
static __always_inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();
	arch_local_irq_disable();
	return flags;
}
#else

#define ENABLE_INTERRUPTS(x)	sti
#define DISABLE_INTERRUPTS(x)	cli

#ifdef CONFIG_X86_64
#ifdef CONFIG_DEBUG_ENTRY
//#define SAVE_FLAGS(x)		pushfq; popq %rax
#endif

////交换 `MSR_KERNEL_GS_BASE` and `MSR_GS_BASE`
//`swapgs`指令交换 GS 段选择符及 `MSR_KERNEL_GS_BASE ` 特殊模式寄存器中的值
#define SWAPGS	swapgs
/*
 * Currently paravirt can't handle swapgs nicely when we
 * don't have a stack we can rely on (such as a user space
 * stack).  So we either find a way around these or just fault
 * and emulate if a guest tries to call swapgs directly.
 *
 * Either way, this is a good way to document that we don't
 * have a reliable stack. x86_64 only.
 *
 * `swapgs`指令交换 GS 段选择符及 `MSR_KERNEL_GS_BASE ` 特殊模式寄存器中的值
 */
#define SWAPGS_UNSAFE_STACK	swapgs
#define swapgs RTOAX_ADDED/* 我加的, `swapgs`指令交换 GS 段选择符及 `MSR_KERNEL_GS_BASE ` 特殊模式寄存器中的值 */

#define INTERRUPT_RETURN	jmp native_iret
#define USERGS_SYSRET64				\
	swapgs;					\
	sysretq;
#define USERGS_SYSRET32				\
	swapgs;					\
	sysretl

#else
//#define INTERRUPT_RETURN		iret
#endif

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PARAVIRT_XXL */

#ifndef __ASSEMBLY__
static __always_inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & X86_EFLAGS_IF);
}

static __always_inline int arch_irqs_disabled(void)
{
	unsigned long flags = arch_local_save_flags();

	return arch_irqs_disabled_flags(flags);
}
#endif /* !__ASSEMBLY__ */

#endif
