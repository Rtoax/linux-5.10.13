/* SPDX-License-Identifier: GPL-2.0-only */
/* ----------------------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * The actual transition into protected mode
 */

#include <asm/boot.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>
#include <linux/linkage.h>

	.text
	.code16

/*
 * void protected_mode_jump(u32 entrypoint, u32 bootparams);
 *  第一个参数在 eax 寄存器中, 保护模式入口点的地址
 *  第二个参数在中 edx, boot_params 的地址
 */
SYM_FUNC_START_NOALIGN(protected_mode_jump)
	movl	%edx, %esi		//# Pointer to boot_params table

	xorl	%ebx, %ebx  //ebx = 0
	movw	%cs, %bx    //bx = cs (代码段)
	shll	$4, %ebx    //ebx = bx << 4;
	addl	%ebx, 2f    //将 ebx 添加到标记的存储位置2 即((cs << 4) + in_pm32)转换为32位模式后要跳转的物理地址
	jmp	1f			//# Short jump to serialize on 386/486
1:

	movw	$__BOOT_DS, %cx     //数据段 cx = __BOOT_DS = 2 * 8 = 16
	movw	$__BOOT_TSS, %di    //任务段 di = __BOOT_TSS = 24

    ////在CR0控制寄存器中设置PE（保护启用）位(X86_CR0_PE)：
	movl	%cr0, %edx  
	orb	$X86_CR0_PE, %dl	//# Protected mode X86_CR0_PE
	movl	%edx, %cr0

    //跳至保护模式
    //0x66 是操作数大小的前缀，它使我们可以混合使用16位和32位代码
    //0xea -是跳转操作码
    //in_pm32是保护模式下的段偏移量，其值(cs << 4) + in_pm32源自实模式
    //__BOOT_CS 是我们要跳转到的代码段。
	//# Transition to 32-bit mode //跳至保护模式
	.byte	0x66, 0xea		//# ljmpl opcode
2:	.long	.Lin_pm32		//# offset -> `in_pm32`包含了对32-bit入口的跳转语句
	.word	__BOOT_CS		//# segment (GDT_ENTRY_BOOT_CS*8) = 16，段描述符表索引
	//在执行了这个跳转命令之后，我们就在保护模式下执行代码了
	
SYM_FUNC_END(protected_mode_jump)
    //终于进入保护模式
	.code32
	.section ".text32","ax"

    //在保护模式下采取的第一步。首先，我们使用以下命令设置数据段：
SYM_FUNC_START_LOCAL_NOALIGN(.Lin_pm32)
	//# Set up data segments for flat 32-bit mode
    //保护模式代码的第一步就是重置所有的段寄存器（除了 `CS` 寄存器）:
    //保存$__BOOT_DS在cx注册表中,
    //用cs（cs已经是__BOOT_CS）填充所有段寄存器
	movl	%ecx, %ds
	movl	%ecx, %es
	movl	%ecx, %fs
	movl	%ecx, %gs
	movl	%ecx, %ss
	//在实模式代码中将 `$__BOOT_DS` （数据段描述符索引）放入了 `cx` 寄存器
	//上面的代码设置所有段寄存器（除了 `CS` 寄存器）指向数据段
	
	//# The 32-bit code sets up its own stack, but this way we do have
	//# a valid stack if some debugging hack wants to use it.
    //设置有效的堆栈以进行调试
	addl	%ebx, %esp

	//# Set up TR to make Intel VT happy
	ltr	%di

	//# Clear registers to allow for future extensions to the
	//# 32-bit boot protocol
    //跳入32位入口点之前的最后一步是清除通用寄存器
	xorl	%ecx, %ecx
	xorl	%edx, %edx
	xorl	%ebx, %ebx
	xorl	%ebp, %ebp
	xorl	%edi, %edi

	//# Set up LDTR to make Intel VT happy
	lldt	%cx

    //并最后跳到32位入口点
    // eax-保护模式入口点的地址 protected_mode_jump 的入参（eax其中包含32位条目的地址）,即
    //eax = boot_params.hdr.code32_start
        //arch/x86/include/uapi/asm/bootparam.h code32_start;
        //arch/x86/boot/header.S code32_start
	jmpl	*%eax			//# Jump to the 32-bit entrypoint

    //通过查看32位入口点的寄存器值来确保是这样的：
    //eax            0x100000    1048576
    //ecx            0x0        0
    //edx            0x0        0
    //ebx            0x0        0
    //esp            0x1ff5c    0x1ff5c
    //ebp            0x0        0x0
    //esi            0x14470    83056
    //edi            0x0        0
    //eip            0x100000    0x100000
    //eflags         0x46        [ PF ZF ]
    //cs             0x10    16
    //ss             0x18    24
    //ds             0x18    24
    //es             0x18    24
    //fs             0x18    24
    //gs             0x18    24
    //我们在这里可以看到该cs寄存器包含一个值0x10（正如您可能从上一部分回忆起的那样，
    //这是 Global Descriptor Table 的第二个索引($__BOOT_DS)），该eip寄存器包含值0x100000，
    //并且包括代码段在内的所有段的基址均为零。
    //因此，加载内核的物理地址将为0:0x100000或仅为0x100000引导协议指定的地址。
    //现在让我们从32-bit入口点开始
    //该32-bit入口点是在所定义的 arch/x86/boot/compressed/head_64.S 汇编源代码文件：
SYM_FUNC_END(.Lin_pm32) 
