// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * Prepare the machine for transition to protected mode.
 */

#include "boot.h"
#include <asm/segment.h>

/*
 * Invoke the realmode switch hook if present; otherwise
 * disable all interrupts.
 */
static void realmode_switch_hook(void)
{
    //存在实函数开关钩子
	if (boot_params.hdr.realmode_swtch) {
        //如果引导加载程序在敌对环境中运行，则使用钩子
        //用了 `lcallw` 指令进行远函数调用
		asm volatile("lcallw *%0"
			     : : "m" (boot_params.hdr.realmode_swtch)
			     : "eax", "ebx", "ecx", "edx");
	} else {
        //如果存在实函数开关钩子，此函数将调用它，并禁用NMI
		asm volatile("cli");    //cli其中包含清除中断标志（IF）的指令 将禁用外部中断

        //将字节0x80（禁用位）写入0x70（CMOS地址寄存器）
        //要向端口输出任何字节，0x80应精确延迟1微秒
		outb(0x80, 0x70); /* Disable NMI, Non Maskable Interrupt “不可屏蔽中断”*/
                            //不可屏蔽中断（NMI）是始终与许可无关地进行处理的中断
                            //它们不能被忽略，通常用于表示不可恢复的硬件错误
        //导致一个小的延迟
        //进行了短暂的延时以等待 I/O 操作完成
        io_delay();
	}
}

/*
 * Disable all interrupts at the legacy PIC.
 *  这将屏蔽辅助PIC（可编程中断控制器）和主要PIC上的所有中断，但主要PIC上的IRQ2除外
 */
static void mask_all_interrupts(void)
{
	outb(0xff, 0xa1);	/* Mask all interrupts on the secondary PIC */
	io_delay();
	outb(0xfb, 0x21);	/* Mask all but cascade on the primary PIC */
	io_delay();
}

/*
 * Reset IGNNE# if asserted in the FPU.
 *  通过将 `0` 写入 I/O 端口 `0xf0` 和 `0xf1` 以复位数字协处理器
 */
static void reset_coprocessor(void)
{
	outb(0, 0xf0);
	io_delay();
	outb(0, 0xf1);
	io_delay();
}

/*
 * Set up the GDT
 */
//+-----------------------------------+----------------------+
//|                                   |                      |
//|     Base address of the IDT       |   Limit of the IDT   |
//|                                   |                      |
//+-----------------------------------+----------------------+
//47                                16 15                    0
struct gdt_ptr {    /* 描述符表 -> GDTR 寄存器是 48 bit 长度的*/
	u16 len;
	u32 ptr;
} __attribute__((packed));
typedef struct gdt_ptr idt_ptr;/*是因为 idt_ptr 与`gdt_prt`具有相同的结构, +++*/

static void setup_gdt(void)
{
	/* There are machines which are known to not boot with the GDT
	   being 8-byte unaligned.  Intel recommends 16 byte alignment. */
	   //用于代码，数据和TSS（任务状态段）
	static const u64 boot_gdt[] __attribute__((aligned(16)))/* 16 字节对齐,共占用了 48 字节 */ = {
		/* CS: code, read/execute, 4 GB, base 0 */
		[GDT_ENTRY_BOOT_CS] = GDT_ENTRY(0xc09b, 0, 0xfffff),
		/* DS: data, read/write, 4 GB, base 0 */
		[GDT_ENTRY_BOOT_DS] = GDT_ENTRY(0xc093, 0, 0xfffff),
		/* TSS: 32-bit tss, 104 bytes, base 4096 */
		/* We only have a TSS here to keep Intel VT happy;
		   we don't actually use it for anything. */
		[GDT_ENTRY_BOOT_TSS] = GDT_ENTRY(0x0089, 4096, 103),
	};
	/* Xen HVM incorrectly stores a pointer to the gdt_ptr, instead
	   of the gdt_ptr contents.  Thus, make it static so it will
	   stay in memory, at least long enough that we switch to the
	   proper kernel GDT. */
	static struct gdt_ptr gdt;

    //GDT的长度为
	gdt.len = sizeof(boot_gdt)-1;

    //获得一个指向GDT的指针
    //获取boot_gdt的地址，并将其添加到左移4位的数据段的地址中
    //（请记住，我们现在处于实模式）
    //因为我们还在实模式，所以就是 （ ds << 4 + 数组起始地址）
	gdt.ptr = (u32)&boot_gdt + (ds() << 4);

    //执行lgdtl指令以将GDT加载到GDTR寄存器中
	asm volatile("lgdtl %0" : : "m" (gdt));
}

/*
 * Set up the IDT, 用 `NULL`填充了中断描述符表
 */
static void setup_idt(void)
{   //设置中断描述符表（IDT）
    //内核在此处并没有填充`Interrupt Descriptor Table`，这是因为此刻处理任何中断或异常还为时尚早
	static const struct gdt_ptr null_idt = {0, 0};
	asm volatile("lidtl %0" : : "m" (null_idt));    //加载到 `IDTR` 寄存器(`IDT` 的基址存储)
}

/*
 * Actual invocation sequence
 */
void go_to_protected_mode(void)
{
	/* Hook before leaving real mode, also disables interrupts */
    //存在实函数开关钩子
	realmode_switch_hook();

	/* Enable the A20 gate */
    //启用A20线: 尝试使用不同的方法启用A20门
	if (enable_a20()) {
		puts("A20 gate not responding, unable to boot...\n");
		die();  //die: arch/x86/boot/header.S
        //die:
        //	hlt
        //	jmp	die
        //
        //	.size	die, .-die
	}

	/* Reset coprocessor (IGNNE#) 
        通过将 `0` 写入 I/O 端口 `0xf0` 和 `0xf1` 以复位数字协处理器*/
	reset_coprocessor();

	/* Mask all interrupts in the PIC */
    //这将屏蔽辅助PIC（可编程中断控制器）和主要PIC上的所有中断，但主要PIC上的IRQ2除外
	mask_all_interrupts();

	/* Actual transition to protected mode... */
	setup_idt();    //设置中断描述符表（IDT）
	setup_gdt();    //设置全局描述符表

    /**
     *  `go_to_protected_mode` 函数在完成 IDT, GDT 初始化，并禁止了 NMI 中断之后，
     *  将调用 `protected_mode_jump` 函数完成从实模式到保护模式的跳转
     */

    //实际过渡到保护模式
    //arch/x86/boot/pmjump.S
	protected_mode_jump(boot_params.hdr.code32_start/* 保护模式入口点的地址 */,
			    (u32)&boot_params + (ds() << 4)/* boot_params 的地址  */);
}
