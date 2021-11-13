// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 * ----------------------------------------------------------------------- */

/*
 * Main module for the real-mode kernel code
 */
#include <linux/build_bug.h>

#include "boot.h"
#include "string.h"

struct boot_parameters __attribute__((aligned(16))) boot_params ;/* page0 */

char *HEAP = _end;
char *heap_end = _end;		/* Default end of heap = no heap */

/*
 * Copy the header into the boot parameter block.  Since this
 * screws up the old-style command line protocol, adjust by
 * filling in the new-style command line pointer instead.
 */
//它将内核设置标头复制到 arch/x86/include/uapi/asm/bootparam.h 标头文件中 boot_params 定义的结构的相应字段中。
//
//copy_boot_params做两件事：
//
// 它hdr从header.S复制到结构中的setup_header字段boot_params。
// 如果内核已加载旧的命令行协议，它将更新指向内核命令行的指针。

static void copy_boot_params(void)
{
	struct old_cmdline {
		u16 cl_magic;
		u16 cl_offset;
	};
	const struct old_cmdline * const oldcmd =
		(const struct old_cmdline *)OLD_CL_ADDRESS;

	BUILD_BUG_ON(sizeof(boot_params) != 4096);

    /* setup_header hdr 
        该结构包含与linux引导协议中定义的字段相同的字段，并由引导加载程序以及内核编译/构建时填充 
        arch/x86/boot/copy.S:memcpy 
            ax 将包含的地址 boot_params.hdr
            dx 将包含的地址 hdr
            cx将包含hdr以字节为单位的大小。*/
	memcpy(&boot_params.hdr, &hdr, sizeof(hdr));
    
	if (!boot_params.hdr.cmd_line_ptr &&
	    oldcmd->cl_magic == OLD_CL_MAGIC) {
		/* Old-style command line protocol. */
		u16 cmdline_seg;

		/* Figure out if the command line falls in the region
		   of memory that an old kernel would have copied up
		   to 0x90000... */
		if (oldcmd->cl_offset < boot_params.hdr.setup_move_size)
			cmdline_seg = ds();
		else
			cmdline_seg = 0x9000;

		boot_params.hdr.cmd_line_ptr =
			(cmdline_seg << 4) + oldcmd->cl_offset;
	}
}

/*
 * Query the keyboard lock status as given by the BIOS, and
 * set the keyboard repeat rate to maximum.  Unclear why the latter
 * is done here; this might be possible to kill off as stale code.
 */
static void keyboard_init(void)
{
	struct biosregs ireg, oreg;
	initregs(&ireg);

	ireg.ah = 0x02;		/* Get keyboard status */

    //调用0x16中断以查询键盘的状态
	intcall(0x16, &ireg, &oreg);
	boot_params.kbd_status = oreg.al;

    //再次调用0x16来设置重复率和延迟
	ireg.ax = 0x0305;	/* Set keyboard repeat rate */
	intcall(0x16, &ireg, NULL);
}

/*
 * Get Intel SpeedStep (IST) information.
 */
static void query_ist(void)
{
	struct biosregs ireg, oreg;

	/* Some older BIOSes apparently crash on this call, so filter
	   it from machines too old to have SpeedStep at all. */
	   //检查CPU级别
	if (cpu.level < 6)
		return;

    //检查CPU级别，如果正确，则调用0x15以获取信息并将结果保存到boot_params
	initregs(&ireg);
	ireg.ax  = 0xe980;	 /* IST Support */
	ireg.edx = 0x47534943;	 /* Request value */
	intcall(0x15, &ireg, &oreg);

	boot_params.ist_info.signature  = oreg.eax;
	boot_params.ist_info.command    = oreg.ebx;
	boot_params.ist_info.event      = oreg.ecx;
	boot_params.ist_info.perf_level = oreg.edx;
}

/*
 * Tell the BIOS what CPU mode we intend to run in.
 * //set_bios_mode 可能会在安装代码发现合适的CPU之后对该函数调用
 */
static void set_bios_mode(void)
{
#ifdef CONFIG_X86_64
	struct biosregs ireg;

	initregs(&ireg);
	ireg.ax = 0xec00;
	ireg.bx = 2;
	intcall(0x15, &ireg, NULL);
#endif
}

//在header.S中准备好stack和bss部分之后（请参阅上一部分），内核需要使用 init_heap 函数来初始化堆。
static void init_heap(void)
{
	char *stack_end;

    //检查CAN_USE_HEAP标志
	if (boot_params.hdr.loadflags & CAN_USE_HEAP) {

        //并loadflags在设置了该标志的情况下计算堆栈的结尾
        //stack_end = esp - STACK_SIZE
		asm("leal %P1(%%esp),%0"
		    : "=r" (stack_end) : "i" (-STACK_SIZE/* 1024 */));

        //heap_end = heap_end_ptr(=_end) + 512（0x200h）
		heap_end = (char *)
			((size_t)boot_params.hdr.heap_end_ptr + 0x200);
		if (heap_end > stack_end)
			heap_end = stack_end;
	} else {
		/* Boot protocol 2.00 only, no heap available */
		puts("WARNING: Ancient bootloader, some functionality "
		     "may be limited!\n");
	}
}

/* 从 arch/x86/boot/header.S 中 calll main 跳转 */
//什么是protected mode，
//过渡到它，
//堆和控制台的初始化，
//内存检测，CPU验证和键盘初始化

void main(void) /* 这是无参数 的 no args */
{
	/* First, copy the boot header into the "zeropage" */
	copy_boot_params();

	/* Initialize the early-boot console 初始化控制台 early_serial_console.c */
	console_init();
	if (cmdline_find_option_bool("debug"))
		puts("early console in setup code\n");

	/* End of heap check */
    //在header.S中准备好stack和bss部分之后（请参阅上一部分），内核需要使用 init_heap 函数来初始化堆。
    //堆已初始化，我们可以使用 GET_HEAP 方法使用它
	init_heap();

	/* Make sure we have all the proper CPU support */
    //cpu验证
	if (validate_cpu()) {
		puts("Unable to boot - please use a kernel appropriate "
		     "for your CPU.\n");
		die();
	}

	/* Tell the BIOS what CPU mode we intend to run in. */
    //set_bios_mode 可能会在安装代码发现合适的CPU之后对该函数调用
	set_bios_mode();

	/* Detect memory layout */
    //提供了可用RAM到CPU的映射
    //它使用的内存检测喜欢不同的编程接口0xe820，0xe801和0x88。在这里，我们将仅看到0xE820接口的实现。
	detect_memory();

	/* Set keyboard repeat rate (why?) and query the lock flags */
    //初始化键盘
	keyboard_init();

	/* Query Intel SpeedStep (IST) information */
	query_ist();

	/* Query APM information */
#if defined(CONFIG_APM) || defined(CONFIG_APM_MODULE)
    //从BIOS获取高级电源管理信息
	query_apm_bios();
#endif

	/* Query EDD information */
#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
    //从 BIOS 查询 Enhanced Disk Drive 信息
	query_edd();
#endif

	/* Set the video mode */
    //视频模式设置    : arch/x86/boot/video.c
	set_video();

	/* Do the last things and invoke protected mode */
    //进行最后的准备工作然后进入保护模式
	go_to_protected_mode();
}
