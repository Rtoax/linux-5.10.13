// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 * ----------------------------------------------------------------------- */

/*
 * Memory detection code
 */

#include "boot.h"

/**
 *  
 */
#define SMAP	0x534d4150	/* ASCII "SMAP" */

/**
 *  
 */
static void detect_memory_e820(void)
{
	int count = 0;
	struct biosregs ireg, oreg;
	struct boot_e820_entry *desc = boot_params.e820_table;
	static struct boot_e820_entry buf; /* static so it is zeroed */

    /**
     *  初始化biosregs
     */
	initregs(&ireg);

    /**
     *  为0xe820调用填充特殊值的寄存器
     *  ax 包含函数的编号（本例中为0xe820）
     *  cx 包含缓冲区的大小，该缓冲区将包含有关内存的数据
     *  edx 必须包含SMAP幻数
     *  es:di 必须包含将包含内存数据的缓冲区的地址
     *  ebx 必须为零。
     */
	ireg.ax  = 0xe820;
    /**
     *  cx 存放每个 e820 记录的大小
     */
	ireg.cx  = sizeof(buf);
    /**
     *  按照 0x15 号中断的约定，将 edx 寄存器设置为 魔数 
     */
	ireg.edx = SMAP;
    /**
     *  存储 e820 记录的内存地址
     */
	ireg.di  = (size_t)&buf;

	/*
	 * Note: at least one BIOS is known which assumes that the
	 * buffer pointed to by one e820 call is the same one as
	 * the previous call, and only changes modified fields.  Therefore,
	 * we use a temporary buffer and copy the results entry by entry.
	 *
	 * This routine deliberately does not try to account for
	 * ACPI 3+ extended attributes.  This is because there are
	 * BIOSes in the field which report zero for the valid bit for
	 * all ranges, and we don't currently make any use of the
	 * other attribute bits.  Revisit this if we see the extended
	 * attribute bits deployed in a meaningful way in the future.
	 *
	 * 接下来是一个循环，收集有关内存的数据
	 *
     *  从地址分配表中收集数据，并将此数据写入e820_entry数组
     *  内存段的开始
     *  内存段的大小
     *  内存段的类型（特定段是可用的还是保留的）
     *  $ sudo cat /var/log/messages | grep BIOS-e820
     *  Mar  2 08:58:32 localhost kernel: BIOS-e820: [mem 0x0000000000000000-0x000000000009fbff] usable
     *  Mar  2 08:58:32 localhost kernel: BIOS-e820: [mem 0x000000000009fc00-0x000000000009ffff] reserved
     *  Mar  2 08:58:32 localhost kernel: BIOS-e820: [mem 0x00000000000f0000-0x00000000000fffff] reserved
     *  Mar  2 08:58:32 localhost kernel: BIOS-e820: [mem 0x0000000000100000-0x00000000bff7ffff] usable
     *  Mar  2 08:58:32 localhost kernel: BIOS-e820: [mem 0x00000000bff80000-0x00000000bfffffff] reserved
     *  Mar  2 08:58:32 localhost kernel: BIOS-e820: [mem 0x00000000feffc000-0x00000000feffffff] reserved
     *  Mar  2 08:58:32 localhost kernel: BIOS-e820: [mem 0x00000000fffc0000-0x00000000ffffffff] reserved
     *  Mar  2 08:58:32 localhost kernel: BIOS-e820: [mem 0x0000000100000000-0x000000023fffffff] usable
     */
	do {
        /**
         *  它始于对 0x15 BIOS中断的调用，该中断从地址分配表中写入一行。
         *  为了获得下一行，我们需要再次调用此中断（在循环中进行此操作）
         *
         *  Q&A?
         *  1. 0x15 中断的处理函数是谁？ 这个中断处理函数由 BIOS 实现，存储在 BIOS ROM 中
         */
		intcall(0x15, &ireg, &oreg);

        /**
         *  在下一次调用之前，ebx必须包含先前返回的值
         */
		ireg.ebx = oreg.ebx; /* for next iteration... */

		/* BIOSes which terminate the chain with CF = 1 as opposed
		   to %ebx = 0 don't always report the SMAP signature on
		   the final, failing, probe. */
		if (oreg.eflags & X86_EFLAGS_CF)
			break;

		/* Some BIOSes stop returning SMAP in the middle of
		   the search loop.  We don't know exactly how the BIOS
		   screwed up the map at that point, we might have a
		   partial map, the full map, or complete garbage, so
		   just return failure. */
		if (oreg.eax != SMAP) {
			count = 0;
			break;
		}

        /**
         *  探测到的一个内存段
         */
		*desc++ = buf;
		count++;
	} while (ireg.ebx && count < ARRAY_SIZE(boot_params.e820_table));

    
	boot_params.e820_entries = count;
}

static void detect_memory_e801(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ax = 0xe801;
	intcall(0x15, &ireg, &oreg);

	if (oreg.eflags & X86_EFLAGS_CF)
		return;

	/* Do we really need to do this? */
	if (oreg.cx || oreg.dx) {
		oreg.ax = oreg.cx;
		oreg.bx = oreg.dx;
	}

	if (oreg.ax > 15*1024) {
		return;	/* Bogus! */
	} else if (oreg.ax == 15*1024) {
		boot_params.alt_mem_k = (oreg.bx << 6) + oreg.ax;
	} else {
		/*
		 * This ignores memory above 16MB if we have a memory
		 * hole there.  If someone actually finds a machine
		 * with a memory hole at 16MB and no support for
		 * 0E820h they should probably generate a fake e820
		 * map.
		 */
		boot_params.alt_mem_k = oreg.ax;
	}
}

static void detect_memory_88(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ah = 0x88;
	intcall(0x15, &ireg, &oreg);

	boot_params.screen_info.ext_mem_k = oreg.ax;
}


/* Detect memory layout */
//提供了可用RAM到CPU的映射
//它使用的内存检测喜欢不同的编程接口 0xe820 ， 0xe801 和 0x88
void detect_memory(void)
{
    /**
     *  先试试从 e820 协议从 BIOS 中获取内存布局
     */
	detect_memory_e820();   //arch/x86/boot/memory.c 获取全部内存分配
    /**
     *  不成功的话试试e801，再试88。一般而言都是e820就可以了
     */
	detect_memory_e801();
    /**
     *  
     */
	detect_memory_88(); //获取临近内存大小
}
