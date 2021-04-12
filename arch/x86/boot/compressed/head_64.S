/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/boot/head.S
 *
 *  Copyright (C) 1991, 1992, 1993  Linus Torvalds
 */

/*
 *  head.S contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00001000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory. [According to comments etc elsewhere on a compressed
 * kernel it will end up at 0x1000 + 1Mb I hope so as I assume this. - AC]
 *
 * Page 0 is deliberately kept safe, since System Management Mode code in 
 * laptops may need to access the BIOS data stored there.  This is also
 * useful for future device drivers that either access the BIOS via VM86 
 * mode.
 */

/**
 *  参考学习链接: https://0xax.gitbooks.io/linux-insides/content/Booting/linux-bootstrap-4.html
 */

/*
 * High loaded stuff by Hans Lermen & Werner Almesberger, Feb. 1996
 */
	.code32
	.text

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/boot.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>
#include <asm/asm-offsets.h>
#include <asm/bootparam.h>
#include <asm/desc_defs.h>
#include "pgtable.h"

/*
 * Locally defined symbols should be marked hidden:
 */
	.hidden _bss
	.hidden _ebss
	.hidden _end

	__HEAD  /* __HEAD 是在 include/linux/init.h */

/*
 * This macro gives the relative virtual address of X, i.e. the offset of X
 * from startup_32. This is the same as the link-time virtual address of X,
 * since startup_32 is at 0, but defining it this way tells the
 * assembler/linker that we do not want the actual run-time address of X. This
 * prevents the linker from trying to create unwanted run-time relocation
 * entries for the reference when the compressed kernel is linked as PIE.
 *
 * A reference X(%reg) will result in the link-time VA of X being stored with
 * the instruction, and a run-time R_X86_64_RELATIVE relocation entry that
 * adds the 64-bit base address where the kernel is loaded.
 *
 * Replacing it with (X-startup_32)(%reg) results in the offset being stored,
 * and no run-time relocation.
 *
 * The macro should be used as a displacement with a base register containing
 * the run-time address of startup_32 [i.e. rva(X)(%reg)], or as an immediate
 * [$ rva(X)].
 *
 * This macro can only be used from within the .head.text section, since the
 * expression requires startup_32 to be in the same section as the code being
 * assembled.
 */
#define rva(X) ((X) - startup_32)

	.code32
	//为什么目录名为compressed？
	//这个问题的答案是，bzimage是一个压缩包vmlinux,header和kernel setup code组成
SYM_FUNC_START(startup_32)
	/*
	 * 32bit entry is 0 and it is ABI so immutable!
	 * If we come here directly from a bootloader,
	 * kernel(text+data+bss+brk) ramdisk, zero_page, command line
	 * all need to be under the 4G limit.
	 */
	cld //cld清除标志寄存器中DF位（方向标志）的指令,因为稍后我们将使用字符串操作来执行各种操作
	    //当方向标志被清空，所有的串操作指令像[stos]， [scas]等等将会增加索引寄存器 `esi` 或者 `edi` 的值
	    //需要清空方向标志是因为接下来我们会使用汇编的串操作指令来做为页表腾出空间等工作
	cli

/*
 * Calculate the delta between where we were compiled to run
 * at and where we were actually loaded at.  This can only be done
 * with a short local call on x86.  Nothing  else will tell us what
 * address we are running at.  The reserved chunk of the real-mode
 * data at 0x1e4 (defined as a scratch field) are used as the stack
 * for this calculation. Only 4 bytes are needed.
 */
    //`esi` 寄存器包含了 [boot_params]
    ///* 0x1e4 = BP_scratch */ 
    //BP_scratch ~ BP_scratch+4 4 字节的区域将会成为 `call` 指令的临时栈
	leal	(BP_scratch+4)(%esi), %esp
	call	1f
1:	popl	%ebp    //把该标签的地址放入 `ebp` 寄存器
                    // `ebp` 寄存器包含了 `1f` 标签的实际物理地址
	subl	$ rva(1b), %ebp

	/* Load new GDT with the 64bit segments using 32bit descriptor */
	leal	rva(gdt)(%ebp), %eax
	movl	%eax, 2(%eax)
	lgdt	(%eax)

	/* Load segment registers with our descriptors 用 `__BOOT_DS` 更新段寄存器 */
	movl	$__BOOT_DS/* 0x18 */, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %fs
	movl	%eax, %gs
	movl	%eax, %ss

/* setup a stack and make sure cpu supports long mode. */
	leal	rva(boot_stack_end)(%ebp), %esp 

    //既然我们将要切换到 `长模式` ，我们需要检查 CPU 是否支持 `长模式` 和 `SSE`
    //arch/x86/kernel/verify_cpu.S verify_cpu
	call	verify_cpu
	testl	%eax, %eax  //通过 `eax` 寄存器返回0表示成功，1表示失败
	jnz	.Lno_longmode   //如果 `eax` 的值不是 0 ，我们就跳转到 `no_longmode` 标签

    /* 如果 `eax` 的值为0，支持 `长模式` 和 `SSE` 万事大吉，我们可以继续。 */
/*
 * Compute the delta between where we were compiled to run at
 * and where the code will actually run at.
 *
 * %ebp contains the address we are loaded at by the boot loader and %ebx
 * contains the address where we should move the kernel image temporarily
 * for safe in-place decompression.
 */

//默认的内核基地址由内核配置项CONFIG_PHYSICAL_START=0x1000000(16 MB)
//如果内核崩溃了，内核开发者需要一个配置于不同地址加载的 `救援内核` 来进行 [kdump]
//Linux 内核提供了特殊的配置选项以解决此问题 - `CONFIG_RELOCATABLE`
//如果 CONFIG_RELOCATABLE=y， 那么 内核将会从其被加载的位置运行，
//编译时的物理地址 (CONFIG_PHYSICAL_START) 将会被作为最低地址位置的限制。
// [位置无关代码]的形式编译来达到的
#ifdef CONFIG_RELOCATABLE
	movl	%ebp, %ebx  //ebp寄存器的值是startup_32标签的物理地址。
	                    //ebx = startup_32
    
#ifdef CONFIG_EFI_STUB
/*
 * If we were loaded via the EFI LoadImage service, startup_32 will be at an
 * offset to the start of the space allocated for the image. efi_pe_entry will
 * set up image_offset to tell us where the image actually starts, so that we
 * can use the full available buffer.
 *	image_offset = startup_32 - image_base
 * Otherwise image_offset will be zero and has no effect on the calculations.
 */
	subl    rva(image_offset)(%ebp), %ebx
#endif

	movl	BP_kernel_alignment(%esi), %eax
	decl	%eax
	addl	%eax, %ebx
	notl	%eax
	andl	%eax, %ebx
	cmpl	$LOAD_PHYSICAL_ADDR, %ebx   /* LOAD_PHYSICAL_ADDR = 1000000 */
	jae	1f
#endif
	movl	$LOAD_PHYSICAL_ADDR, %ebx
1:
    
	/* Target address to relocate to for decompression */
	addl	BP_init_size(%esi), %ebx
	subl	$ rva(_end), %ebx   //给 `startup_32` 加上偏移来获得解压内核镜像的地址


    /* 在前面的操作之后，`ebp` 包含了我们加载时的地址，`ebx` 被设为内核解压缩的目标地址 */
/*
 * Prepare for entering 64 bit mode
 */

    //在我们载入 `全局描述符表` 之后,必须启用 [PAE]
    //方法是将 `cr4` 寄存器的值传入 `eax` ，将第5位置1，然后再写回 `cr4` 
	/* Enable PAE mode */
	movl	%cr4, %eax
	orl	$X86_CR4_PAE, %eax /* X86_CR4_PAE: enable physical address extensions */
	movl	%eax, %cr4

/**
 *  现在我们已经接近完成进入64位模式前的所有准备工作了
 *  最后一步是建立页表
 *
 *  [长模式]是 [x86_64] 系列处理器的原生模式
 *   `64位` 模式提供了一些新特性，比如：

    * 从 `r8` 到 `r15` 8个新的通用寄存器，并且所有通用寄存器都是64位的了。
    * 64位指令指针 - `RIP` ;
    * 新的操作模式 - 长模式;
    * 64位地址和操作数;
    * RIP 相对寻址 (我们将会在接下来的章节看到一个例子).
    
    长模式是一个传统保护模式的扩展，其由两个子模式构成：
    
    * 64位模式
    * 兼容模式
 */
    
//要切换到64-bit模式，我们需要执行以下操作：
//
//    启用PAE ;
//    建立页表并将顶级页表的地址加载到cr3寄存器中；
//    启用EFER.LME;
//    启用分页。

 /*
  * Build early 4G boot pagetable
  */
    //Linux 内核使用 `4级` 页表，通常我们会建立6个页表：
    //
    //* 1 个 `PML4` 或称为 `4级页映射` 表，包含 1 个项；
    //* 1 个 `PDP` 或称为 `页目录指针` 表，包含 4 个项；
    //* 4 个 页目录表，一共包含 `2048` 个项；
	/*
	 * If SEV is active then set the encryption mask in the page tables.
	 * This will insure that when the kernel is copied and decompressed
	 * it will be done so encrypted.
	 */
	call	get_sev_encryption_bit
	xorl	%edx, %edx
	testl	%eax, %eax
	jz	1f
	subl	$32, %eax	/* Encryption bit is always above bit 31 */
	bts	%eax, %edx	/* Set encryption mask for page tables */
1:

	/* Initialize Page tables to 0, 清除内存中页表的缓冲区
	    每个表都是4096字节，因此我们需要清除一个24千字节缓冲区 */
	leal	rva(pgtable)(%ebx), %edi
	xorl	%eax, %eax
	movl	$(BOOT_INIT_PGT_SIZE/4), %ecx /* (4096*6)/4=6144 */
	rep	stosl   //把 `eax` 的值写到 `edi` 指向的地址,然后给 `edi` 加 4 ， `ecx` 减 4 ，重复直到 `ecx` 小于等于 0

	/* Build Level 4 ,开始构建顶级页表 - `PML4`*/
	leal	rva(pgtable + 0)(%ebx), %edi
	leal	0x1007 (%edi), %eax
	movl	%eax, 0(%edi)
	addl	%edx, 4(%edi)

	/* Build Level 3 */
	leal	rva(pgtable + 0x1000)(%ebx), %edi
	leal	0x1007(%edi), %eax
	movl	$4, %ecx
1:	movl	%eax, 0x00(%edi)
	addl	%edx, 0x04(%edi)
	addl	$0x00001000, %eax
	addl	$8, %edi
	decl	%ecx
	jnz	1b

	/* Build Level 2 */
	leal	rva(pgtable + 0x2000)(%ebx), %edi
	movl	$0x00000183, %eax
	movl	$2048, %ecx
1:	movl	%eax, 0(%edi)
	addl	%edx, 4(%edi)
	addl	$0x00200000, %eax
	addl	$8, %edi
	decl	%ecx
	jnz	1b

    /**
     *  我们刚刚完成我们的初期页表结构，其映射了 `4G` 大小的内存，
     * 现在我们可以把高级页表 `PML4` 的地址放到 `cr3` 寄存器中了
     */
	/* Enable the boot page tables */
	leal	rva(pgtable)(%ebx), %eax
	movl	%eax, %cr3  /* 将高级页面表的地址PML4--放入cr3控制寄存器中 */

    /**
     *  现在准备切换到长模式
     */

	/* Enable Long mode in EFER (Extended Feature Enable Register) 
    需要EFER.LME将MSR中的标志设置为0xC0000080 */
	movl	$MSR_EFER, %ecx     //将 MSR_EFER(0xc0000080) 标志放入ecx寄存器
	rdmsr   //调用 `rdmsr` 指令读取 [MSR] 寄存器
	btsl	$_EFER_LME, %eax    //检查当前 _EFER_LME 位
	wrmsr   //通过 `wrmsr` 指令将 `eax` 的数据写入 `MSR` 寄存器

	/* After gdt is loaded */
	xorl	%eax, %eax
	lldt	%ax
	movl    $__BOOT_TSS, %eax
	ltr	%ax

    /**
     *  下一步我们将内核段代码地址入栈
     */
	/*
	 * Setup for the jump to 64bit mode 跳转到 64位 模式
	 *
	 * When the jump is performend we will be in long mode but
	 * in 32bit compatibility mode with EFER.LME = 1, CS.L = 0, CS.D = 1
	 * (and in turn EFER.LMA = 1).	To jump into 64bit mode we use
	 * the new gdt/idt that has __KERNEL_CS with CS.L = 1.
	 * We place all of the values on our mini stack so lret can
	 * used to perform that far jump.
	 */
	leal	rva(startup_64)(%ebp), %eax //将 `startup_64` 的地址导入 `eax`
#ifdef CONFIG_EFI_MIXED
	movl	rva(efi32_boot_args)(%ebp), %edi
	cmp	$0, %edi
	jz	1f
	leal	rva(efi64_stub_entry)(%ebp), %eax
	movl	rva(efi32_boot_args+4)(%ebp), %esi
	movl	rva(efi32_boot_args+8)(%ebp), %edx	// saved bootparams pointer
	cmpl	$0, %edx
	jnz	1f
	/*
	 * efi_pe_entry uses MS calling convention, which requires 32 bytes of
	 * shadow space on the stack even if all arguments are passed in
	 * registers. We also need an additional 8 bytes for the space that
	 * would be occupied by the return address, and this also results in
	 * the correct stack alignment for entry.
	 */
	subl	$40, %esp
	leal	rva(efi_pe_entry)(%ebp), %eax
	movl	%edi, %ecx			// MS calling convention
	movl	%esi, %edx
1:
#endif
	pushl	$__KERNEL_CS    //__KERNEL_CS 入栈
	pushl	%eax    //将 `startup_64` 的地址

	/* Enter paged protected Mode, activating Long Mode */
    //通过设置 cr0 寄存器中的 PG 和 PE 位来启用分页 
	movl	$(X86_CR0_PG | X86_CR0_PE), %eax /* Enable Paging and Protected mode */
	movl	%eax, %cr0

	/* Jump from 32bit compatibility mode into 64bit mode. 
        记住前一步我们已经将 `startup_64` 函数的地址入栈，
        在 `lret` 指令之后，CPU 取出了其地址跳转到那里
        步骤之后我们最后来到了64位模式 startup_64 */
	lret 
SYM_FUNC_END(startup_32)

#ifdef CONFIG_EFI_MIXED
	.org 0x190
SYM_FUNC_START(efi32_stub_entry)
	add	$0x4, %esp		/* Discard return address */
	popl	%ecx
	popl	%edx
	popl	%esi

	call	1f
1:	pop	%ebp
	subl	$ rva(1b), %ebp

	movl	%esi, rva(efi32_boot_args+8)(%ebp)
SYM_INNER_LABEL(efi32_pe_stub_entry, SYM_L_LOCAL)
	movl	%ecx, rva(efi32_boot_args)(%ebp)
	movl	%edx, rva(efi32_boot_args+4)(%ebp)
	movb	$0, rva(efi_is64)(%ebp)

	/* Save firmware GDTR and code/data selectors */
	sgdtl	rva(efi32_boot_gdt)(%ebp)
	movw	%cs, rva(efi32_boot_cs)(%ebp)
	movw	%ds, rva(efi32_boot_ds)(%ebp)

	/* Disable paging */
	movl	%cr0, %eax
	btrl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	jmp	startup_32
SYM_FUNC_END(efi32_stub_entry)
#endif

/**
 *  进入了64位模式
 *  内核解压前的准备、重定位和直接内核解压
 *
 *  解压代码为: extract_kernel()
 */
	.code64
	.org 0x200
SYM_CODE_START(startup_64)  /* 进入了64位模式 */
	/*
	 * 64bit entry is 0x200 and it is ABI so immutable!
	 * We come here either from startup_32 or directly from a
	 * 64bit bootloader.
	 * If we come here from a bootloader, kernel(text+data+bss+brk),
	 * ramdisk, zero_page, command line could be above 4G.
	 * We depend on an identity mapped page table being provided
	 * that maps our entire kernel(text+data+bss+brk), zero page
	 * and command line.
	 */

	cld //cld清除标志寄存器中DF位的指令,因为稍后我们将使用字符串操作来执行各种操作
	cli
    
	/* Setup data segments. 设置段寄存器 现在，除cs寄存器外的所有段寄存器都将复位为long mode
        数据段的建立 */
	xorl	%eax, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %ss
	movl	%eax, %fs
	movl	%eax, %gs
	//除`cs`之外的段寄存器在我们进入`长模式`时已经重置

	/*
	 * Compute the decompressed kernel start address.  It is where
	 * we were loaded at aligned to a 2M boundary. %rbp contains the
	 * decompressed kernel start address.
	 *
	 * If it is a relocatable kernel then decompress and run the kernel
	 * from load address aligned to 2MB addr, otherwise decompress and
	 * run the kernel from LOAD_PHYSICAL_ADDR
	 *
	 * We cannot rely on the calculation done in 32-bit mode, since we
	 * may have been invoked via the 64-bit entry point.
	 */

	/* Start with the delta to where the kernel will run at. */
#ifdef CONFIG_RELOCATABLE
    //计算编译内核要加载的位置与内核实际加载的位置之间的差
    //该rbp寄存器包含解压缩后的内核的起始地址
    //{{{ 执行此代码后，rbx寄存器将包含内核代码将被重新定位到的地址，以进行解压缩
	leaq	startup_32(%rip) /* - $startup_32 */, %rbp

#ifdef CONFIG_EFI_STUB
/*
 * If we were loaded via the EFI LoadImage service, startup_32 will be at an
 * offset to the start of the space allocated for the image. efi_pe_entry will
 * set up image_offset to tell us where the image actually starts, so that we
 * can use the full available buffer.
 *	image_offset = startup_32 - image_base
 * Otherwise image_offset will be zero and has no effect on the calculations.
 */
	movl    image_offset(%rip), %eax
	subq	%rax, %rbp
#endif

	movl	BP_kernel_alignment(%rsi), %eax
	decl	%eax
	addq	%rax, %rbp
	notq	%rax
	andq	%rax, %rbp
	cmpq	$LOAD_PHYSICAL_ADDR, %rbp
	jae	1f
#endif
	movq	$LOAD_PHYSICAL_ADDR, %rbp
1:

	/* Target address to relocate to for decompression */
	movl	BP_init_size(%rsi), %ebx
	subl	$ rva(_end), %ebx
	addq	%rbp, %rbx // 执行此代码后，rbx 寄存器将包含内核代码将被重新定位到的地址，以进行解压缩
    //}}}
    

	/* Set up the stack 
        栈指针的设置和标志寄存器的重置 
        `rbx`寄存器包含了内核解压代码的起始地址，
        把这个地址的`boot_stack_entry`偏移地址相加放到表示栈顶指针的`rsp`寄存器
        在这一步之后，栈就是正确的 */
	leaq	rva(boot_stack_end)(%rbx), %rsp

	/*
	 * At this point we are in long mode with 4-level paging enabled,
	 * but we might want to enable 5-level paging or vice versa.
	 *
	 * The problem is that we cannot do it directly. Setting or clearing
	 * CR4.LA57 in long mode would trigger #GP. So we need to switch off
	 * long mode and paging first.
	 *
	 * We also need a trampoline in lower memory to switch over from
	 * 4- to 5-level paging for cases when the bootloader puts the kernel
	 * above 4G, but didn't enable 5-level paging for us.
	 *
	 * The same trampoline can be used to switch from 5- to 4-level paging
	 * mode, like when starting 4-level paging kernel via kexec() when
	 * original kernel worked in 5-level paging mode.
	 *
	 * For the trampoline, we need the top page table to reside in lower
	 * memory as we don't have a way to load 64-bit values into CR3 in
	 * 32-bit mode.
	 *
	 * We go though the trampoline even if we don't have to: if we're
	 * already in a desired paging mode. This way the trampoline code gets
	 * tested on every boot.
	 */

	/* Make sure we have GDT with 32-bit code segment */
	leaq	gdt64(%rip), %rax
	addq	%rax, 2(%rax)
	lgdt	(%rax)

	/* Reload CS so IRET returns to a CS actually in the GDT */
	pushq	$__KERNEL_CS
	leaq	.Lon_kernel_cs(%rip), %rax
	pushq	%rax
	lretq

.Lon_kernel_cs:

	pushq	%rsi
	call	load_stage1_idt
	popq	%rsi

	/*
	 * paging_prepare() sets up the trampoline and checks if we need to
	 * enable 5-level paging.
	 *
	 * paging_prepare() returns a two-quadword structure which lands
	 * into RDX:RAX:
	 *   - Address of the trampoline is returned in RAX.
	 *   - Non zero RDX means trampoline needs to enable 5-level
	 *     paging.
	 *
	 * RSI holds real mode data and needs to be preserved across
	 * this function call.
	 */
	pushq	%rsi
	movq	%rsi, %rdi		/* real mode address */
	call	paging_prepare
	popq	%rsi

	/* Save the trampoline address in RCX */
	movq	%rax, %rcx

	/*
	 * Load the address of trampoline_return() into RDI.
	 * It will be used by the trampoline to return to the main code.
	 */
	leaq	trampoline_return(%rip), %rdi

	/* Switch to compatibility mode (CS.L = 0 CS.D = 1) via far return */
	pushq	$__KERNEL32_CS
	leaq	TRAMPOLINE_32BIT_CODE_OFFSET(%rax), %rax
	pushq	%rax
	lretq
trampoline_return:
	/* Restore the stack, the 32-bit trampoline uses its own stack */
	leaq	rva(boot_stack_end)(%rbx), %rsp

	/*
	 * cleanup_trampoline() would restore trampoline memory.
	 *
	 * RDI is address of the page table to use instead of page table
	 * in trampoline memory (if required).
	 *
	 * RSI holds real mode data and needs to be preserved across
	 * this function call.
	 */
	pushq	%rsi
	leaq	rva(top_pgtable)(%rbx), %rdi
	call	cleanup_trampoline
	popq	%rsi

	/* Zero EFLAGS */
	pushq	$0
	popfq

/*
 * Copy the compressed kernel to the end of our buffer
 * where decompression in place becomes safe.
 */
	pushq	%rsi    //首先我们把`rsi`压进栈
	                //我们需要保存`rsi`的值，因为这个寄存器现在存放指向`boot_params`的指针，
	                //这是包含引导相关数据的实模式结构体（你一定记得这个结构体，我们在开始设置内核的时候就填充了它）。
    //两个`leaq`指令用`_bss - 8`偏移和`rip`和`rbx`计算有效地址并存放到`rsi`和`rdi`.
    //我们为什么要计算这些地址？
    //实际上，压缩了的代码镜像存放在这份复制了的代码（从`startup_32`到当前的代码）和解压了的代码之间。
    leaq	(_bss-8)(%rip), %rsi    /* `rsi`包含`_bss - 8`的绝对地址 */
	leaq	rva(_bss-8)(%rbx), %rdi /* `rdi`包含`_bss - 8`的重定位的相对地址 */
	
	movl	$(_bss - startup_32), %ecx
	shrl	$3, %ecx
	
	std         //在数据复制前有`std`指令：它设置`DF`标志，意味着`rsi`和`rdi`会递减
	rep	movsq   //现在我们可以开始用`movsq`指令每次8字节地从`rsi`到`rdi`复制代码。
	cld         //用`cld`指令清除`DF`标志
	popq	%rsi    //重新恢复指向`boot_params`的指针到`rsi`

	/*
	 * The GDT may get overwritten either during the copy we just did or
	 * during extract_kernel below. To avoid any issues, repoint the GDTR
	 * to the new copy of the GDT.
	 */
	leaq	rva(gdt64)(%rbx), %rax
	leaq	rva(gdt)(%rbx), %rdx
	movq	%rdx, 2(%rax)
	lgdt	(%rax)

/*
 * Jump to the relocated address.
 *  现在我们有`.text`节的重定位后的地址，我们可以跳到那里
 *  可能是 arch/x86/kernel/head_64.S startup_64
 */
	leaq	rva(.Lrelocated)(%rbx), %rax
	jmp	*%rax
SYM_CODE_END(startup_64)    /* 结束 */

#ifdef CONFIG_EFI_STUB
	.org 0x390
SYM_FUNC_START(efi64_stub_entry)
SYM_FUNC_START_ALIAS(efi_stub_entry)
	and	$~0xf, %rsp			/* realign the stack */
	movq	%rdx, %rbx			/* save boot_params pointer */
	call	efi_main
	movq	%rbx,%rsi
	leaq	rva(startup_64)(%rax), %rax
	jmp	*%rax //可能是 arch/x86/kernel/head_64.S startup_64
	
SYM_FUNC_END(efi64_stub_entry)
SYM_FUNC_END_ALIAS(efi_stub_entry)
#endif

	.text
SYM_FUNC_START_LOCAL_NOALIGN(.Lrelocated)

/*
 * Clear BSS (stack is currently empty)
 * `.text`节从`relocated`标签开始。
 * 它做的第一件事是清空`.bss`节
 */
	xorl	%eax, %eax          //清空`eax`
	leaq    _bss(%rip), %rdi    //把`_bss`的地址放到`rdi`
	leaq    _ebss(%rip), %rcx   //把`_ebss`放到`rcx`
	subq	%rdi, %rcx
	shrq	$3, %rcx
	rep	stosq   //用`rep stosq`填零

/*
 * If running as an SEV guest, the encryption mask is required in the
 * page-table setup code below. When the guest also has SEV-ES enabled
 * set_sev_encryption_mask() will cause #VC exceptions, but the stage2
 * handler can't map its GHCB because the page-table is not set up yet.
 * So set up the encryption mask here while still on the stage1 #VC
 * handler. Then load stage2 IDT and switch to the kernel's own
 * page-table.
 */
	pushq	%rsi
	call	set_sev_encryption_mask //arch/x86/boot/compressed/mem_encrypt.S
	call	load_stage2_idt //arch/x86/boot/compressed/idt_64.c

	/* Pass boot_params to initialize_identity_maps() */
	movq	(%rsp), %rdi
	call	initialize_identity_maps    
	popq	%rsi

/*
 * Do the extraction, and jump to the new kernel..
 */
	pushq	%rsi			/* Save the real mode argument */
	movq	%rsi, %rdi		/* real mode address 
	                            再一次设置`rdi`为指向`boot_params`结构体的指针并把它保存到栈中*/
	leaq	boot_heap(%rip), %rsi	/* malloc area for uncompression 
	                                同时我们设置`rsi`指向用于内核解压的区域*/
	        /* 准备`extract_kernel`的参数 */
	leaq	input_data(%rip), %rdx  /* input_data */
	movl	input_len(%rip), %ecx	/* input_len */
	movq	%rbp, %r8		/* output target address */
	movl	output_len(%rip), %r9d	/* decompressed length, end of relocs */

    //* `rmode` - 指向 [boot_params] arch/x86/include/uapi/asm/bootparam.h#L114) 结构体的指针，
    //            `boot_params`被引导加载器填充或在早期内核初始化时填充
    //* `heap` - 指向早期启动堆的起始地址 `boot_heap` 的指针
    //* `input_data` - 指向压缩的内核，即 `arch/x86/boot/compressed/vmlinux.bin.bz2` 的指针
    //* `input_len` - 压缩的内核的大小
    //* `output` - 解压后内核的起始地址
    //* `output_len` - 解压后内核的大小
	call	extract_kernel		/* returns kernel location in %rax */
	//从 extract_kernel 返回
	popq	%rsi

/*
 * Jump to the decompressed kernel.
 * 内核的地址在`rax`寄存器，我们跳到那里
 * 可能是 arch/x86/kernel/head_64.S startup_64
 */
	jmp	*%rax
SYM_FUNC_END(.Lrelocated)

	.code32
/*
 * This is the 32-bit trampoline that will be copied over to low memory.
 *
 * RDI contains the return address (might be above 4G).
 * ECX contains the base address of the trampoline memory.
 * Non zero RDX means trampoline needs to enable 5-level paging.
 */
SYM_CODE_START(trampoline_32bit_src)
	/* Set up data and stack segments */
	movl	$__KERNEL_DS, %eax
	movl	%eax, %ds
	movl	%eax, %ss

	/* Set up new stack */
	leal	TRAMPOLINE_32BIT_STACK_END(%ecx), %esp

	/* Disable paging */
	movl	%cr0, %eax
	btrl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	/* Check what paging mode we want to be in after the trampoline */
	cmpl	$0, %edx
	jz	1f

	/* We want 5-level paging: don't touch CR3 if it already points to 5-level page tables */
	movl	%cr4, %eax
	testl	$X86_CR4_LA57, %eax
	jnz	3f
	jmp	2f
1:
	/* We want 4-level paging: don't touch CR3 if it already points to 4-level page tables */
	movl	%cr4, %eax
	testl	$X86_CR4_LA57, %eax
	jz	3f
2:
	/* Point CR3 to the trampoline's new top level page table */
	leal	TRAMPOLINE_32BIT_PGTABLE_OFFSET(%ecx), %eax
	movl	%eax, %cr3
3:
	/* Set EFER.LME=1 as a precaution in case hypervsior pulls the rug */
	pushl	%ecx
	pushl	%edx
	movl	$MSR_EFER, %ecx
	rdmsr
	btsl	$_EFER_LME, %eax
	wrmsr
	popl	%edx
	popl	%ecx

	/* Enable PAE and LA57 (if required) paging modes */
	movl	$X86_CR4_PAE, %eax
	cmpl	$0, %edx
	jz	1f
	orl	$X86_CR4_LA57, %eax
1:
	movl	%eax, %cr4

	/* Calculate address of paging_enabled() once we are executing in the trampoline */
	leal	.Lpaging_enabled - trampoline_32bit_src + TRAMPOLINE_32BIT_CODE_OFFSET(%ecx), %eax

	/* Prepare the stack for far return to Long Mode */
	pushl	$__KERNEL_CS
	pushl	%eax

	/* Enable paging again */
	movl	$(X86_CR0_PG | X86_CR0_PE), %eax
	movl	%eax, %cr0

	lret
SYM_CODE_END(trampoline_32bit_src)

	.code64
SYM_FUNC_START_LOCAL_NOALIGN(.Lpaging_enabled)
	/* Return from the trampoline */
	jmp	*%rdi
SYM_FUNC_END(.Lpaging_enabled)

	/*
         * The trampoline code has a size limit.
         * Make sure we fail to compile if the trampoline code grows
         * beyond TRAMPOLINE_32BIT_CODE_SIZE bytes.
	 */
	.org	trampoline_32bit_src + TRAMPOLINE_32BIT_CODE_SIZE

	.code32
SYM_FUNC_START_LOCAL_NOALIGN(.Lno_longmode) 
    //如果检查对 `长模式` 和 `SSE` 不支持
    //用 `hlt` 指令停止 CPU ，期间不会发生硬件中断
	/* This isn't an x86-64 CPU, so hang intentionally, we cannot continue */
1:
	hlt
	jmp     1b
SYM_FUNC_END(.Lno_longmode)

#include "../../kernel/verify_cpu.S"

	.data
SYM_DATA_START_LOCAL(gdt64)
	.word	gdt_end - gdt - 1
	.quad   gdt - gdt64
SYM_DATA_END(gdt64)
	.balign	8
SYM_DATA_START_LOCAL(gdt)
	.word	gdt_end - gdt - 1
	.long	0
	.word	0
	.quad	0x00cf9a000000ffff	/* __KERNEL32_CS */
	.quad	0x00af9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf92000000ffff	/* __KERNEL_DS */
	.quad	0x0080890000000000	/* TS descriptor */
	.quad   0x0000000000000000	/* TS continued */
SYM_DATA_END_LABEL(gdt, SYM_L_LOCAL, gdt_end)

SYM_DATA_START(boot_idt_desc)
	.word	boot_idt_end - boot_idt - 1
	.quad	0
SYM_DATA_END(boot_idt_desc)
	.balign 8
SYM_DATA_START(boot_idt)
	.rept	BOOT_IDT_ENTRIES
	.quad	0
	.quad	0
	.endr
SYM_DATA_END_LABEL(boot_idt, SYM_L_GLOBAL, boot_idt_end)

#ifdef CONFIG_EFI_STUB
SYM_DATA(image_offset, .long 0)
#endif
#ifdef CONFIG_EFI_MIXED
SYM_DATA_LOCAL(efi32_boot_args, .long 0, 0, 0)
SYM_DATA(efi_is64, .byte 1)

#define ST32_boottime		60 // offsetof(efi_system_table_32_t, boottime)
#define BS32_handle_protocol	88 // offsetof(efi_boot_services_32_t, handle_protocol)
#define LI32_image_base		32 // offsetof(efi_loaded_image_32_t, image_base)

	__HEAD
	.code32
SYM_FUNC_START(efi32_pe_entry)
/*
 * efi_status_t efi32_pe_entry(efi_handle_t image_handle,
 *			       efi_system_table_32_t *sys_table)
 */

	pushl	%ebp
	movl	%esp, %ebp
	pushl	%eax				// dummy push to allocate loaded_image

	pushl	%ebx				// save callee-save registers
	pushl	%edi

	call	verify_cpu			// check for long mode support
	testl	%eax, %eax
	movl	$0x80000003, %eax		// EFI_UNSUPPORTED
	jnz	2f

	call	1f
1:	pop	%ebx
	subl	$ rva(1b), %ebx

	/* Get the loaded image protocol pointer from the image handle */
	leal	-4(%ebp), %eax
	pushl	%eax				// &loaded_image
	leal	rva(loaded_image_proto)(%ebx), %eax
	pushl	%eax				// pass the GUID address
	pushl	8(%ebp)				// pass the image handle

	/*
	 * Note the alignment of the stack frame.
	 *   sys_table
	 *   handle             <-- 16-byte aligned on entry by ABI
	 *   return address
	 *   frame pointer
	 *   loaded_image       <-- local variable
	 *   saved %ebx		<-- 16-byte aligned here
	 *   saved %edi
	 *   &loaded_image
	 *   &loaded_image_proto
	 *   handle             <-- 16-byte aligned for call to handle_protocol
	 */

	movl	12(%ebp), %eax			// sys_table
	movl	ST32_boottime(%eax), %eax	// sys_table->boottime
	call	*BS32_handle_protocol(%eax)	// sys_table->boottime->handle_protocol
	addl	$12, %esp			// restore argument space
	testl	%eax, %eax
	jnz	2f

	movl	8(%ebp), %ecx			// image_handle
	movl	12(%ebp), %edx			// sys_table
	movl	-4(%ebp), %esi			// loaded_image
	movl	LI32_image_base(%esi), %esi	// loaded_image->image_base
	movl	%ebx, %ebp			// startup_32 for efi32_pe_stub_entry
	/*
	 * We need to set the image_offset variable here since startup_32() will
	 * use it before we get to the 64-bit efi_pe_entry() in C code.
	 */
	subl	%esi, %ebx
	movl	%ebx, rva(image_offset)(%ebp)	// save image_offset
	jmp	efi32_pe_stub_entry

2:	popl	%edi				// restore callee-save registers
	popl	%ebx
	leave
	ret
SYM_FUNC_END(efi32_pe_entry)

	.section ".rodata"
	/* EFI loaded image protocol GUID */
	.balign 4
SYM_DATA_START_LOCAL(loaded_image_proto)
	.long	0x5b1b31a1
	.word	0x9562, 0x11d2
	.byte	0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b
SYM_DATA_END(loaded_image_proto)
#endif

/*
 * Stack and heap for uncompression
 */
	.bss    //在`.bss`节
	.balign 4
SYM_DATA_LOCAL(boot_heap,	.fill BOOT_HEAP_SIZE, 1, 0)

SYM_DATA_START_LOCAL(boot_stack)
	.fill BOOT_STACK_SIZE, 1, 0
	.balign 16
SYM_DATA_END_LABEL(boot_stack, SYM_L_LOCAL, boot_stack_end) //在`.bss`节的末尾

/*
 * Space for page tables (not in .bss so not zeroed)
 */
	.section ".pgtable","aw",@nobits
	.balign 4096
SYM_DATA_LOCAL(pgtable,		.fill BOOT_PGT_SIZE, 1, 0)

/*
 * The page table is going to be used instead of page table in the trampoline
 * memory.
 */
SYM_DATA_LOCAL(top_pgtable,	.fill PAGE_SIZE, 1, 0)
