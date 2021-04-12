/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TRAPNR_H
#define _ASM_X86_TRAPNR_H

/* Interrupts/Exceptions */

/**
 *  start_kernel()->setup_arch()->idt_setup_early_traps()
 *  start_kernel()->setup_arch()->idt_setup_early_pf()
 *  start_kernel()->trap_init()->idt_setup_traps()
 *  start_kernel()->trap_init()->idt_setup_ist_traps()
 *  start_kernel()->early_irq_init()
 *  start_kernel()->init_IRQ()
 *  start_kernel()->softirq_init()
 */

/**
 *  arch\x86\include\asm\idtentry.h
 */

/**
 *  arch/x86/entry/entry_64.S
 *
 *  .macro idtentry_irq vector cfunc            for Interrupt entry/exit.
 *  .macro idtentry_sysvec vector cfunc         for System vectors
 *  .macro idtentry_mce_db vector asmsym cfunc  for #MC and #DB
 *  .macro idtentry_vc vector asmsym cfunc      for #VC
 *  .macro idtentry_df vector asmsym cfunc      for Double fault
 *  
 */


#define X86_TRAP_DE		 0	/*  0, 除零错误 *//* Divide-by-zero */    /* 被 0 除 */
                            /**
                             *  exc_divide_error    : arch\x86\kernel\traps.c
                             *  do_error_trap(...)  : arch\x86\kernel\traps.c
                             */
#define X86_TRAP_DB		 1	/*  1, 调试 *//* Debug 没有 Error Code */
                            //+-----------------------------------------------------+
                            //|Vector|Mnemonic|Description         |Type |Error Code|
                            //+-----------------------------------------------------+
                            //|1     | #DB    |Reserved            |F/T  |NO        |
                            //+-----------------------------------------------------+
                            /**
                             *  X86_TRAP_DB
                             *      exc_debug           : arch\x86\kernel\traps.c
                             *      exc_debug_kernel()  : arch\x86\kernel\traps.c
                             *      exc_debug_user()    : arch\x86\kernel\traps.c
                             */


//* External hardware asserts (外部设备断言)the non-maskable interrupt [pin] on the CPU.
//* The processor receives a message on the system bus or the APIC serial bus with a delivery mode `NMI`.
#define X86_TRAP_NMI	 2	/*  2, 不可屏蔽中断 *//* Non-maskable Interrupt 不可屏蔽中断， 严重问题 */
                            /**
                             *  hardware interrupt
                             *      exc_nmi  : arch\x86\kernel\nmi.c
                             *      ()
                             */
#define X86_TRAP_BP		 3	/*  3, 断点 *//* Breakpoint */    /* 断点 -> int3 */
                            /**
                             *  X86_TRAP_BP
                             *      do_int3() : arch\x86\kernel\traps.c
                             *      exc_int3  : arch\x86\kernel\traps.c
                             */
                            //#include <stdio.h>
                            //
                            //int main() {
                            //    int i;
                            //    while (i < 6){
                            //	    printf("i equal to: %d\n", i);
                            //	    __asm__("int3");
                            //		++i;
                            //    }
                            //}

#define X86_TRAP_OF		 4	/*  4, 溢出 *//* Overflow */
                            /**
                             *  exc_overflow        : arch\x86\kernel\traps.c
                             *  do_error_trap(...)  : arch\x86\kernel\traps.c
                             */
#define X86_TRAP_BR		 5	/*  5, 超出范围 *//* Bound Range Exceeded */
                            /**
                             *      exc_bounds  : arch\x86\kernel\traps.c
                             *      ()
                             */
#define X86_TRAP_UD		 6	/*  6, 操作码无效 *//* Invalid Opcode */
                            /**
                             *  X86_TRAP_UD
                             *      exc_invalid_op  : arch\x86\kernel\traps.c
                             *      handle_invalid_op()
                             */

//* an [x87 FPU]floating-point instruction while the EM flag in [control register]`cr0` was set;
//* a `wait` or `fwait` instruction while the `MP` and `TS` flags of register `cr0` were set;
//* an [x87 FPU], [MMX] or [SSE] instruction while the `TS` flag in control register `cr0` was set and the `EM` flag is clear.
#define X86_TRAP_NM		 7	/*  7, 设备不可用 *//* Device Not Available */
                            /**
                             *      exc_device_not_available  : arch\x86\kernel\traps.c
                             *      ()
                             */

#define X86_TRAP_DF		 8	/*  8, 当前处于异常处理,异常再次发生 *//* Double Fault */
                            /**
                             *      exc_double_fault : arch\x86\kernel\traps.c
                             *      ()
                             */
#define X86_TRAP_OLD_MF	9	/*  9, 协处理器段溢出 *//* Coprocessor Segment Overrun */
                            /**
                             *      exc_coproc_segment_overrun  : arch\x86\kernel\traps.c
                             *      do_error_trap()
                             */
#define X86_TRAP_TS		10	/* 10, 无效的 TSS *//* Invalid TSS */
                            /**
                             *      exc_invalid_tss  : arch\x86\kernel\traps.c
                             *      do_error_trap()
                             */
#define X86_TRAP_NP		11	/* 11, 段不存在 *//* Segment Not Present */
                            /**
                             *      exc_segment_not_present  : arch\x86\kernel\traps.c
                             *      do_error_trap()
                             */
#define X86_TRAP_SS		12	/* 12, 堆栈段故障 *//* Stack Segment Fault */
                            /**
                             *      exc_stack_segment  : arch\x86\kernel\traps.c
                             *      do_error_trap()
                             */
                             
//* Exceeding the segment limit when accessing the `cs`, `ds`, `es`, `fs` or `gs` segments;
//* Loading the `ss`, `ds`, `es`, `fs` or `gs` register with a segment selector for a system segment.;
//* Violating any of the privilege rules;
//* and other...
#define X86_TRAP_GP		13	/* 13, 一般保护故障 *//* General Protection Fault */
                            /**
                             *      exc_general_protection  : arch\x86\kernel\traps.c
                             *      ()
                             */


#define X86_TRAP_PF		14	/* 14, 页错误 *//* Page Fault */    /* 页 fault */
                            /**
                             *  X86_TRAP_PF
                             *      exc_page_fault  : fault.c arch\x86\mm 
                             *      handle_page_fault()
                             */
#define X86_TRAP_SPURIOUS	15	/* 15, 伪中断 *//* Spurious Interrupt */
                            /**
                             *      exc_spurious_interrupt_bug  : arch\x86\kernel\traps.c
                             *      ()
                             */
#define X86_TRAP_MF		16	/* 16, x87 浮点异常 *//* x87 Floating-Point Exception */
                            /**
                             *      exc_coprocessor_error  : arch\x86\kernel\traps.c
                             *      ()
                             */
#define X86_TRAP_AC		17	/* 17, 对齐检查 *//* Alignment Check */
                            /**
                             *      exc_alignment_check  : arch\x86\kernel\traps.c
                             *      do_trap()
                             */
#define X86_TRAP_MC		18	/* 18, 机器检测 *//* Machine Check */
                            /**
                             *      exc_machine_check  : arch\x86\kernel\cpu\mce\core.c
                             *      ()
                             */
#define X86_TRAP_XF		19	/* 19, SIMD （单指令多数据结构浮点）异常 *//* SIMD Floating-Point Exception */
                            /**
                             *      exc_simd_coprocessor_error  : arch\x86\kernel\traps.c
                             *      ()
                             */
                            /* `SSE` or `SSE2` or `SSE3` SIMD floating-point exception */
                            //There are six classes of numeric exception conditions that 
                            //can occur while executing an SIMD floating-point instruction:
                            //
                            //* Invalid operation
                            //* Divide-by-zero
                            //* Denormal operand
                            //* Numeric overflow
                            //* Numeric underflow
                            //* Inexact result (Precision)

#define X86_TRAP_VE		20	/* 20, 虚拟化异常 *//* Virtualization Exception */
                            /**
                             *        : arch\x86\kernel\traps.c
                             *      ()
                             */
#define X86_TRAP_CP		21	/* 21, 控制保护异常 *//* Control Protection Exception */
                            /**
                             *        : arch\x86\kernel\traps.c
                             *      ()
                             */
#define X86_TRAP_VC		29	/* 29, VMM 异常 *//* VMM Communication Exception 硬件虚拟化之vmm接管异常中断 */
                            /**
                             *        : arch\x86\kernel\traps.c
                             *      ()
                             */
#define X86_TRAP_IRET	32	/* 32, IRET （中断返回）异常 *//* IRET Exception */
                            /**
                             *        : arch\x86\kernel\traps.c
                             *      ()
                             */



#endif
