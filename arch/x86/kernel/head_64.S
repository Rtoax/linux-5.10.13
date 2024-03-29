/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/arch/x86/kernel/head_64.S -- start in 32bit and switch to 64bit
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright (C) 2000 Pavel Machek <pavel@suse.cz>
 *  Copyright (C) 2000 Karsten Keil <kkeil@suse.de>
 *  Copyright (C) 2001,2002 Andi Kleen <ak@suse.de>
 *  Copyright (C) 2005 Eric Biederman <ebiederm@xmission.com>
 */


#include <linux/linkage.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/pgtable.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/msr.h>
#include <asm/cache.h>
#include <asm/processor-flags.h>
#include <asm/percpu.h>
#include <asm/nops.h>
#include "../entry/calling.h"
#include <asm/export.h>
#include <asm/nospec-branch.h>
#include <asm/fixmap.h>

#ifdef CONFIG_PARAVIRT_XXL
#include <asm/asm-offsets.h>
#include <asm/paravirt.h>
#define GET_CR2_INTO(reg) GET_CR2_INTO_AX ; _ASM_MOV %_ASM_AX, reg
#else
//#define INTERRUPT_RETURN iretq
//#define GET_CR2_INTO(reg) _ASM_MOV %cr2, reg
#endif

/*
 * We are not able to switch in one step to the final KERNEL ADDRESS SPACE
 * because we need identity-mapped pages.
 */
#define l4_index(x)	(((x) >> 39) & 511)
#define pud_index(x)	(((x) >> PUD_SHIFT) & (PTRS_PER_PUD-1))

L4_PAGE_OFFSET = l4_index(__PAGE_OFFSET_BASE_L4/*0xffff888000000000*/)/* =0x111 */
    //0xffff888000000000        =1111111111111111100010001000000000000000000000000000000000000000
    //0xffff888000000000>>39    =1111111111111111100010001
    //0xffff888000000000>>39&511=1111111111111111100010001
    //                                           111111111 = 100010001 = 0x111 = 273
L4_START_KERNEL = l4_index(__START_KERNEL_map/*0xffffffff80000000*/)/* =0x1FF */
    //1111111111111111111111111111111110000000000000000000000000000000>>39
    //                                      =1111111111111111111111111&511
    //                                      =                111111111=511
L3_START_KERNEL = pud_index(__START_KERNEL_map)

    /**
     *  除了对 `.text` 区段的定义，
     *  我们还能从这个脚本文件中得知内核的默认物理地址与虚拟地址
     */
	.text
	__HEAD
	.code64
/**
 *  vmlinux.bin（真正内核代码）
 *  Vmlinux.bin的入口函数是arch/x86/kernel/head_64.S中定义的startup_64
 *  在内核还没有运行在自己的虚拟地址空间(0xffffffff81000000)之前，head_64.S中的代码必须是PIE
 *  但vmlinux的编译是加-fno-pic选项的，所以这就要求我们手工将head_64.S写成PIE code。
 *
 *  这样做的目的：
 *  将vmlinux.bin加载到任意物理地址后，head_64.S能根据被重定位的虚拟基地址重新初
 *  始化 PML512 paging structure，这样内核才可以正确的运行在已经被重定位的虚拟地址空间，
 *  这是因为之前定义的pgtable占用6pages，是实地址映射4G物理地址的，也就意味着只能管理4G虚拟地址空间，
 *  而现在内核的默认基地址是0xffffffff81000000，被重定位后一定是 > 0xffffffff81000000的，
 *  所以要重新初始化一个PML512 paging structure， 所以这里明白吧，head_64.S一定手工写成PIE code.
 */
SYM_CODE_START_NOALIGN(startup_64)
	UNWIND_HINT_EMPTY
	/*
	 * At this point the CPU runs in 64bit mode CS.L = 1 CS.D = 0,
	 * and someone has loaded an identity mapped page table
	 * for us.  These identity mapped page tables map all of the
	 * kernel pages and possibly all of memory.
	 *
	 * %rsi holds a physical pointer to real_mode_data.
	 *
	 * We come here either directly from a 64bit bootloader, or from
	 * arch/x86/boot/compressed/head_64.S.
	 *
	 * We only come here initially at boot nothing else comes here.
	 *
	 * Since we may be loaded at an address different from what we were
	 * compiled to run at we first fixup the physical addresses in our page
	 * tables and then reload them.
	 */

	/* Set up the stack for verify_cpu(), similar to initial_stack below */
	leaq	(__end_init_task - SIZEOF_PTREGS)(%rip), %rsp

    /**
     *  `_text` 是一个地址计数器,对于 [x86_64] 来说，它定义为 . = __START_KERNEL;
     *  __START_KERNEL = (__START_KERNEL_map + __PHYSICAL_START)
     *  Linux 内核的物理基址 - `0x1000000`;(CONFIG_PHYSICAL_START=0x1000000)
     *  Linux 内核的虚拟基址 - `0xffffffff81000000`.(__START_KERNEL_map = 0xffffffff80000000)
     *
     *  通过 %rip 进行相对寻址，实现 PIC code，这里假设随机选取的物理加载地址是
     *  output=32M， 随机选取的 virt_addr=128M(output/virt_addr 参考 extract_kernel 函数)，
     *  内核编译默认加载值d是 LOAD_PHYSICAL_ADDR=16M，所以下一行代码执行后 %rdi=32M,
     *  当前内核运行在 32M 其实地址处。
     */
	leaq	_text(%rip), %rdi
	pushq	%rsi
	call	startup_64_setup_env
	popq	%rsi

	/* Now switch to __KERNEL_CS so IRET works reliably */
	pushq	$__KERNEL_CS
	leaq	.Lon_kernel_cs(%rip), %rax
	pushq	%rax
	lretq

.Lon_kernel_cs:
	UNWIND_HINT_EMPTY

	/* Sanitize CPU configuration */
	call verify_cpu /* verify_cpu.S */

	/*
	 * Perform pagetable fixups. Additionally, if SME is active, encrypt
	 * the kernel and retrieve the modifier (SME encryption mask if SME
	 * is active) to be added to the initial pgdir entry that will be
	 * programmed into CR3.
	 */
	leaq	_text(%rip), %rdi
	pushq	%rsi
	call	__startup_64
	popq	%rsi

	/**
	 *  Form the CR3 value being sure to include the CR3 modifier
	 *  虽然定义为 `0x1000000`，但是仍然有可能变化，例如启用 [kASLR]
	 *  CONFIG_PHYSICAL_START = 0x1000000
	 *  __START_KERNEL_map = 0xffffffff80000000
	 */
	addq	$(early_top_pgt - __START_KERNEL_map), %rax
	jmp 1f
SYM_CODE_END(startup_64)

SYM_CODE_START(secondary_startup_64)
	UNWIND_HINT_EMPTY
	/*
	 * At this point the CPU runs in 64bit mode CS.L = 1 CS.D = 0,
	 * and someone has loaded a mapped page table.
	 *
	 * %rsi holds a physical pointer to real_mode_data.
	 *
	 * We come here either from startup_64 (using physical addresses)
	 * or from trampoline.S (using virtual addresses).
	 *
	 * Using virtual addresses from trampoline.S removes the need
	 * to have any identity mapped pages in the kernel page table
	 * after the boot processor executes this code.
	 */

	/* Sanitize CPU configuration */
	call verify_cpu /* verify_cpu.S */

	/*
	 * The secondary_startup_64_no_verify entry point is only used by
	 * SEV-ES guests. In those guests the call to verify_cpu() would cause
	 * #VC exceptions which can not be handled at this stage of secondary
	 * CPU bringup.
	 *
	 * All non SEV-ES systems, especially Intel systems, need to execute
	 * verify_cpu() above to make sure NX is enabled.
	 */
SYM_INNER_LABEL(secondary_startup_64_no_verify, SYM_L_GLOBAL)
	UNWIND_HINT_EMPTY

	/*
	 * Retrieve the modifier (SME encryption mask if SME is active) to be
	 * added to the initial pgdir entry that will be programmed into CR3.
	 */
	pushq	%rsi
	call	__startup_secondary_64
	popq	%rsi

	/* Form the CR3 value being sure to include the CR3 modifier
	 *
	 *  __START_KERNEL_map = 0xffffffff80000000
	 */
	addq	$(init_top_pgt - __START_KERNEL_map), %rax
1:

	/* Enable PAE mode, PGE and LA57 */
	movl	$(X86_CR4_PAE | X86_CR4_PGE), %ecx
#ifdef CONFIG_X86_5LEVEL
	testl	$1, __pgtable_l5_enabled(%rip)
	jz	1f
	orl	$X86_CR4_LA57, %ecx
1:
#endif
	movq	%rcx, %cr4

	/* Setup early boot stage 4-/5-level pagetables. */
	addq	phys_base(%rip), %rax

	/*
	 * For SEV guests: Verify that the C-bit is correct. A malicious
	 * hypervisor could lie about the C-bit position to perform a ROP
	 * attack on the guest by writing to the unencrypted stack and wait for
	 * the next RET instruction.
	 * %rsi carries pointer to realmode data and is callee-clobbered. Save
	 * and restore it.
	 */
	pushq	%rsi
	movq	%rax, %rdi
	call	sev_verify_cbit /* sev_verify_cbit.S */
	popq	%rsi

	/* Switch to new page-table */
	movq	%rax, %cr3

	/* Ensure I am executing from virtual addresses */
	movq	$1f, %rax
	ANNOTATE_RETPOLINE_SAFE
	jmp	*%rax
1:
	UNWIND_HINT_EMPTY

	/**
	 * We must switch to a new descriptor in kernel space for the GDT
	 * because soon the kernel won't have access anymore to the userspace
	 * addresses where we're currently running on. We have to do that here
	 * because in 32bit we couldn't load a 64bit linear address.
	 *
	 * 全局描述附表
	 */
	lgdt	early_gdt_descr(%rip)

	/* set up data segments */
	xorl %eax,%eax
	movl %eax,%ds
	movl %eax,%ss
	movl %eax,%es

	/*
	 * We don't really need to load %fs or %gs, but load them anyway
	 * to kill any stale realmode selectors.  This allows execution
	 * under VT hardware.
	 */
	movl %eax,%fs
	movl %eax,%gs

	/* Set up %gs.
	 *
	 * The base of %gs always points to fixed_percpu_data. If the
	 * stack protector canary is enabled, it is located at %gs:40.
	 * Note that, on SMP, the boot cpu uses init data section until
	 * the per cpu areas are set up.
	 */
	movl	$MSR_GS_BASE,%ecx   //通过 `MSR_GS_BASE`指定了一个平台相关寄存器
	movl	initial_gs(%rip),%eax   //将 `initial_gs`的64-bit地址放到了 `edx:eax`段寄存器中
	movl	initial_gs+4(%rip),%edx
	wrmsr   //执行 `wrmsr`指令，将 `init_per_cpu__irq_stack_union`的基地址放入 `gs`寄存器
	        //这个地址将是中断栈的栈底地址

	/*
	 * Setup a boot time stack - Any secondary CPU will have lost its stack
	 * by now because the cr3-switch above unmaps the real-mode stack
	 */
	movq initial_stack(%rip), %rsp

	/* Setup and Load IDT */
	pushq	%rsi
	call	early_setup_idt
	popq	%rsi

	/* Check if nx is implemented
	 *
	 * put 0x80000001 value to the eax and execute cpuid instruction
	 * for getting the extended processor info and feature bits
	 */
	movl	$0x80000001, %eax
	cpuid
	movl	%edx,%edi

	/**
	 * Setup EFER (Extended Feature Enable Register)
	 */
	movl	$MSR_EFER, %ecx
	rdmsr
	/*
	https://0xax.gitbooks.io/linux-insides/content/Initialization/linux-initialization-1.html

	63                                                                              32
	--------------------------------------------------------------------------------
	|                                                                               |
	|                                Reserved MBZ                                   |
	|                                                                               |
	--------------------------------------------------------------------------------
	31                            16  15      14      13   12  11   10  9  8 7  1   0
	--------------------------------------------------------------------------------
	|                              | T |       |       |    |   |   |   |   |   |   |
	| Reserved MBZ                 | C | FFXSR | LMSLE |SVME|NXE|LMA|MBZ|LME|RAZ|SCE|
	|                              | E |       |       |    |   |   |   |   |   |   |
	--------------------------------------------------------------------------------
	By the setting SCE bit we enable SYSCALL and SYSRET instructions
	*/
	btsl	$_EFER_SCE, %eax	/* Enable System Call */
	btl	$20,%edi		/* No Execute supported? */
	jnc     1f
	btsl	$_EFER_NX, %eax
	btsq	$_PAGE_BIT_NX,early_pmd_flags(%rip)
1:	wrmsr				/* Make changes effective */

	/* Setup cr0 */
	/**
	* X86_CR0_PE: 系统处于保护模式;
	* X86_CR0_MP: 控制了在 cr0中使用 TS 标志的 WAIT/FWAIT 指令的交互;
	* X86_CR0_ET: 在386上，它允许指定外部数学协处理器是80287还是80387;
	* X86_CR0_NE: 启用内部 x87浮点错误报告，否则启用 PC 风格的 x87错误检测;
	* X86_CR0_WP: 当设置权限级别为0时，CPU 不能写只读页面;
	* X86_CR0_AM: 如果 AM 设置，AC 标志(在 EFLAGS 寄存器中)设置，特权级别为3，则启用 X86_CR0_AM 对齐检查;
	* X86_CR0_PG: enable 分页。
	*
	* #define CR0_STATE	(X86_CR0_PE | X86_CR0_MP | X86_CR0_ET | \
	*			X86_CR0_NE | X86_CR0_WP | X86_CR0_AM | \
	*			X86_CR0_PG)
	*/
	movl	$CR0_STATE, %eax
	/* Make changes effective */
	movq	%rax, %cr0

	/* zero EFLAGS after setting rsp */
	pushq $0
	popfq

	/* rsi is pointer to real mode structure with interesting info.
	   pass it to C */
	movq	%rsi, %rdi

.Ljump_to_C_code:
	/*
	 * Jump to run C code and to be on a real kernel address.
	 * Since we are running on identity-mapped space we have to jump
	 * to the full 64bit address, this is only possible as indirect
	 * jump.  In addition we need to ensure %cs is set so we make this
	 * a far return.
	 *
	 * Note: do not change to far jump indirect with 64bit offset.
	 *
	 * AMD does not support far jump indirect with 64bit offset.
	 * AMD64 Architecture Programmer's Manual, Volume 3: states only
	 *	JMP FAR mem16:16 FF /5 Far jump indirect,
	 *		with the target specified by a far pointer in memory.
	 *	JMP FAR mem16:32 FF /5 Far jump indirect,
	 *		with the target specified by a far pointer in memory.
	 *
	 * Intel64 does support 64bit offset.
	 * Software Developer Manual Vol 2: states:
	 *	FF /5 JMP m16:16 Jump far, absolute indirect,
	 *		address given in m16:16
	 *	FF /5 JMP m16:32 Jump far, absolute indirect,
	 *		address given in m16:32.
	 *	REX.W + FF /5 JMP m16:64 Jump far, absolute indirect,
	 *		address given in m16:64.
	 */
	pushq	$.Lafter_lret	//# put return address on stack for unwinder
	xorl	%ebp, %ebp	//# clear frame pointer
	movq	initial_code(%rip), %rax
	pushq	$__KERNEL_CS	//# set correct cs
	pushq	%rax		//# target address in negative space
	lretq
.Lafter_lret:
SYM_CODE_END(secondary_startup_64)

#include "verify_cpu.S"
#include "sev_verify_cbit.S"

#ifdef CONFIG_HOTPLUG_CPU
/*
 * Boot CPU0 entry point. It's called from play_dead(). Everything has been set
 * up already except stack. We just set up stack here. Then call
 * start_secondary() via .Ljump_to_C_code.
 */
SYM_CODE_START(start_cpu0)
	UNWIND_HINT_EMPTY
	movq	initial_stack(%rip), %rsp
	jmp	.Ljump_to_C_code
SYM_CODE_END(start_cpu0)
#endif

#ifdef CONFIG_AMD_MEM_ENCRYPT
/*
 * VC Exception handler used during early boot when running on kernel
 * addresses, but before the switch to the idt_table can be made.
 * The early_idt_handler_array can't be used here because it calls into a lot
 * of __init code and this handler is also used during CPU offlining/onlining.
 * Therefore this handler ends up in the .text section so that it stays around
 * when .init.text is freed.
 */
SYM_CODE_START_NOALIGN(vc_boot_ghcb)
	UNWIND_HINT_IRET_REGS offset=8

	/* Build pt_regs */
	PUSH_AND_CLEAR_REGS

	/* Call C handler */
	movq    %rsp, %rdi
	movq	ORIG_RAX(%rsp), %rsi
	movq	initial_vc_handler(%rip), %rax
	ANNOTATE_RETPOLINE_SAFE
	call	*%rax

	/* Unwind pt_regs */
	POP_REGS

	/* Remove Error Code */
	addq    $8, %rsp

	/* Pure iret required here - don't use INTERRUPT_RETURN */
	iretq
SYM_CODE_END(vc_boot_ghcb)
#endif

	/* Both SMP bootup and ACPI suspend change these variables */
	__REFDATA
	.balign	8
SYM_DATA(initial_code,	.quad x86_64_start_kernel)
SYM_DATA(initial_gs,	.quad INIT_PER_CPU_VAR(fixed_percpu_data))
#ifdef CONFIG_AMD_MEM_ENCRYPT
SYM_DATA(initial_vc_handler,	.quad handle_vc_boot_ghcb)
#endif

/*
 * The SIZEOF_PTREGS gap is a convention which helps the in-kernel unwinder
 * reliably detect the end of the stack.
 *
 * arch/x86/include/asm/page_64_types.h
 * ----------------------------------------
 * #ifdef CONFIG_KASAN
 * #define KASAN_STACK_ORDER 1
 * #else
 * #define KASAN_STACK_ORDER 0
 * #endif
 * #define THREAD_SIZE_ORDER       (2 + KASAN_STACK_ORDER)
 * #define THREAD_SIZE  (PAGE_SIZE << THREAD_SIZE_ORDER)
 */
SYM_DATA(initial_stack, .quad init_thread_union + THREAD_SIZE - SIZEOF_PTREGS)
	__FINITDATA

	__INIT
// 这段代码自动生成为前 `32` 个异常生成了中断处理程序
SYM_CODE_START(early_idt_handler_array)
	i = 0
	.rept NUM_EXCEPTION_VECTORS
	.if ((EXCEPTION_ERRCODE_MASK >> i) & 1) == 0
		UNWIND_HINT_IRET_REGS
		pushq $0	//# Dummy error code, to make stack frame uniform
	.else
		UNWIND_HINT_IRET_REGS offset=8
	.endif

    /**
     *
     */
	pushq $i		//# 72(%rsp) Vector number
	jmp early_idt_handler_common
	UNWIND_HINT_IRET_REGS
	i = i + 1
	.fill early_idt_handler_array + i*EARLY_IDT_HANDLER_SIZE - ., 1, 0xcc
	.endr
	UNWIND_HINT_IRET_REGS offset=16
SYM_CODE_END(early_idt_handler_array)

/**
 *
 */
SYM_CODE_START_LOCAL(early_idt_handler_common)
	/*
	 * The stack is the hardware frame, an error code or zero, and the
	 * vector number.
	 */
	cld

    //检查 `early_recursion_flag` 以避免在 `early_idt_handler_common` 程序中递归地产生中断
	incl early_recursion_flag(%rip)

	/* The vector number is currently in the pt_regs->di slot. */
    //在栈上保存通用寄存器，为了防止中断返回时寄存器的内容错乱
	pushq %rsi				/* pt_regs->si */
	movq 8(%rsp), %rsi			/* RSI = vector number */
	movq %rdi, 8(%rsp)			/* pt_regs->di = RDI */
	pushq %rdx				/* pt_regs->dx */
	pushq %rcx				/* pt_regs->cx */
	pushq %rax				/* pt_regs->ax */
	pushq %r8				/* pt_regs->r8 */
	pushq %r9				/* pt_regs->r9 */
	pushq %r10				/* pt_regs->r10 */
	pushq %r11				/* pt_regs->r11 */
	pushq %rbx				/* pt_regs->bx */
	pushq %rbp				/* pt_regs->bp */
	pushq %r12				/* pt_regs->r12 */
	pushq %r13				/* pt_regs->r13 */
	pushq %r14				/* pt_regs->r14 */
	pushq %r15				/* pt_regs->r15 */
	UNWIND_HINT_REGS

	movq %rsp,%rdi		/* RDI = pt_regs; RSI is already trapnr */

    /**
     *
     */
	call do_early_exception

	decl early_recursion_flag(%rip)
	jmp restore_regs_and_return_to_kernel
SYM_CODE_END(early_idt_handler_common)

#ifdef CONFIG_AMD_MEM_ENCRYPT
/*
 * VC Exception handler used during very early boot. The
 * early_idt_handler_array can't be used because it returns via the
 * paravirtualized INTERRUPT_RETURN and pv-ops don't work that early.
 *
 * This handler will end up in the .init.text section and not be
 * available to boot secondary CPUs.
 */
SYM_CODE_START_NOALIGN(vc_no_ghcb)
	UNWIND_HINT_IRET_REGS offset=8

	/* Build pt_regs */
	PUSH_AND_CLEAR_REGS

	/* Call C handler */
	movq    %rsp, %rdi
	movq	ORIG_RAX(%rsp), %rsi
	call    do_vc_no_ghcb

	/* Unwind pt_regs */
	POP_REGS

	/* Remove Error Code */
	addq    $8, %rsp

	/* Pure iret required here - don't use INTERRUPT_RETURN */
	iretq
SYM_CODE_END(vc_no_ghcb)
#endif

#define SYM_DATA_START_PAGE_ALIGNED(name)			\
	SYM_START(name, SYM_L_GLOBAL, .balign PAGE_SIZE)

#ifdef CONFIG_PAGE_TABLE_ISOLATION
/*
 * Each PGD needs to be 8k long and 8k aligned.  We do not
 * ever go out to userspace with these, so we do not
 * strictly *need* the second page, but this allows us to
 * have a single set_pgd() implementation that does not
 * need to worry about whether it has 4k or 8k to work
 * with.
 *
 * This ensures PGDs are 8k long:
 */
#define PTI_USER_PGD_FILL	512
/* This ensures they are 8k-aligned: */
#define SYM_DATA_START_PTI_ALIGNED(name) \
	SYM_START(name, SYM_L_GLOBAL, .balign 2 * PAGE_SIZE)
#else

#endif

/* Automate the creation of 1 to 1 mapping pmd entries */
#define PMDS(START, PERM, COUNT)			\
	i = 0 ;						\
	.rept (COUNT) ;					\
	.quad	(START) + (i << PMD_SHIFT) + (PERM) ;	\
	i = i + 1 ;					\
	.endr

	__INITDATA
	.balign 4

SYM_DATA_START_PTI_ALIGNED(early_top_pgt)   /* 从4096零字节开始,不使用第一个512条目 */
	.fill	512,8,0
	.fill	PTI_USER_PGD_FILL,8,0 /* PTI_USER_PGD_FILL=512 */
SYM_DATA_END(early_top_pgt)

SYM_DATA_START_PAGE_ALIGNED(early_dynamic_pgts)
	.fill	512*EARLY_DYNAMIC_PAGE_TABLES,8,0
SYM_DATA_END(early_dynamic_pgts)

SYM_DATA(early_recursion_flag, .long 0)

	.data

#if defined(CONFIG_XEN_PV) || defined(CONFIG_PVH)

    //这段代码为内核的代码段、数据段和 bss 段映射了前 2.5G 个字节
SYM_DATA_START_PTI_ALIGNED(init_top_pgt)
	.quad   level3_ident_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.org    init_top_pgt + L4_PAGE_OFFSET*8, 0
	.quad   level3_ident_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.org    init_top_pgt + L4_START_KERNEL*8, 0
	/* (2^48-(2*1024*1024*1024))/(2^39) = 511 */
	.quad   level3_kernel_pgt - __START_KERNEL_map + _PAGE_TABLE_NOENC
	.fill	PTI_USER_PGD_FILL,8,0/* PTI_USER_PGD_FILL=512 */
SYM_DATA_END(init_top_pgt)

SYM_DATA_START_PAGE_ALIGNED(level3_ident_pgt)
	.quad	level2_ident_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.fill	511, 8, 0
SYM_DATA_END(level3_ident_pgt)
SYM_DATA_START_PAGE_ALIGNED(level2_ident_pgt)
	/*
	 * Since I easily can, map the first 1G.
	 * Don't set NX because code runs from these pages.
	 *
	 * Note: This sets _PAGE_GLOBAL despite whether
	 * the CPU supports it or it is enabled.  But,
	 * the CPU should ignore the bit.
	 */
	PMDS(0, __PAGE_KERNEL_IDENT_LARGE_EXEC, PTRS_PER_PMD)
SYM_DATA_END(level2_ident_pgt)
#else
SYM_DATA_START_PTI_ALIGNED(init_top_pgt)
	.fill	512,8,0 /* 4096 */
	.fill	PTI_USER_PGD_FILL,8,0/* PTI_USER_PGD_FILL=512 *//* 4096 */
SYM_DATA_END(init_top_pgt)
#endif

#ifdef CONFIG_X86_5LEVEL
SYM_DATA_START_PAGE_ALIGNED(level4_kernel_pgt)
	.fill	511,8,0
	.quad	level3_kernel_pgt - __START_KERNEL_map + _PAGE_TABLE_NOENC
SYM_DATA_END(level4_kernel_pgt)
#endif

SYM_DATA_START_PAGE_ALIGNED(level3_kernel_pgt) /* 4080 零（L3_START_KERNEL等于510）的字节 */
	.fill	L3_START_KERNEL,8,0
	/* (2^48-(2*1024*1024*1024)-((2^39)*511))/(2^30) = 510 */
	.quad	level2_kernel_pgt - __START_KERNEL_map + _KERNPG_TABLE_NOENC
	.quad	level2_fixmap_pgt - __START_KERNEL_map + _PAGE_TABLE_NOENC
SYM_DATA_END(level3_kernel_pgt)

//           +-----------+-----------------+---------------+------------------+
//           |           |                 |               |                  |
//           |kernel text|      kernel     |               |    vsyscalls     |
//           | mapping   |       text      |    Modules    |    fix-mapped    |
//           |from phys 0|       data      |               |    addresses     |
//           |           |                 |               |                  |
//           +-----------+-----------------+---------------+------------------+
//    __START_KERNEL_map   __START_KERNEL    MODULES_VADDR            0xffffffffffffffff

SYM_DATA_START_PAGE_ALIGNED(level2_kernel_pgt)  /* 内核代码 + 数据 + bss */
	/*
	 * 512 MB kernel mapping. We spend a full page on this pagetable
	 * anyway.
	 *
	 * The kernel code+data+bss must not be bigger than that.
	 *
	 * (NOTE: at +512MB starts the module area, see MODULES_VADDR.
	 *  If you want to increase this then increase MODULES_VADDR
	 *  too.)
	 *
	 *  This table is eventually used by the kernel during normal
	 *  runtime.  Care must be taken to clear out undesired bits
	 *  later, like _PAGE_RW or _PAGE_GLOBAL in some cases.
	 */
	PMDS(0, __PAGE_KERNEL_LARGE_EXEC,
		KERNEL_IMAGE_SIZE/PMD_SIZE)
SYM_DATA_END(level2_kernel_pgt)
//`level2_fixmap_pgt` 紧挨着 `level2_kernel_pgt`,保存了内核的 code+data+bss 段
SYM_DATA_START_PAGE_ALIGNED(level2_fixmap_pgt)
	.fill	(512 - 4 - FIXMAP_PMD_NUM),8,0
	pgtno = 0
	.rept (FIXMAP_PMD_NUM)
	/* __START_KERNEL_map = 0xffffffff80000000 */
	.quad level1_fixmap_pgt + (pgtno << PAGE_SHIFT) - __START_KERNEL_map \
		+ _PAGE_TABLE_NOENC;
	pgtno = pgtno + 1
	.endr
	/* 6 MB reserved space + a 2MB hole */
	.fill	4,8,0
SYM_DATA_END(level2_fixmap_pgt)

SYM_DATA_START_PAGE_ALIGNED(level1_fixmap_pgt)
	.rept (FIXMAP_PMD_NUM)
	.fill	512,8,0
	.endr
SYM_DATA_END(level1_fixmap_pgt)

#undef PMDS

	.data
	.align 16

/* 全局描述附表 */
SYM_DATA(early_gdt_descr,		.word GDT_ENTRIES*8-1)
SYM_DATA_LOCAL(early_gdt_descr_base,	.quad INIT_PER_CPU_VAR(gdt_page))

	.align 16
/* This must match the first entry in level2_kernel_pgt */
SYM_DATA(phys_base, .quad 0x0)
EXPORT_SYMBOL(phys_base)

#include "../../x86/xen/xen-head.S"

	__PAGE_ALIGNED_BSS
SYM_DATA_START_PAGE_ALIGNED(empty_zero_page)
	.skip PAGE_SIZE
SYM_DATA_END(empty_zero_page)
EXPORT_SYMBOL(empty_zero_page)

