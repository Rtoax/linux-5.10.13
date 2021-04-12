// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   Original APM BIOS checking by Stephen Rothwell, May 1994
 *   (sfr@canb.auug.org.au)
 *
 * ----------------------------------------------------------------------- */

/*
 * Get APM BIOS information
 */

#include "boot.h"

//从BIOS获取高级电源管理信息
int query_apm_bios(void)
{
	struct biosregs ireg, oreg;

	/* APM BIOS installation check */
	initregs(&ireg);

    //调用0x15BIOS中断，但带有ah=0x53来检查APM安装
	ireg.ah = 0x53;
	intcall(0x15, &ireg, &oreg);

    //检查进位标志（如果APM支持，则必须为0 ）和
	if (oreg.flags & X86_EFLAGS_CF)
		return -1;		/* No APM BIOS */

    //检查PM签名（必须为0x504d）
	if (oreg.bx != 0x504d)		/* "PM" signature */
		return -1;

    //检查cx寄存器的值（如果为0x02，则支持保护模式接口）
	if (!(oreg.cx & 0x02))		/* 32 bits supported? */
		return -1;

    //接下来，它0x15再次调用，但是使用ax = 0x5304 来断开APM接口并连接32位保护模式接口。
    //最后，它填充boot_params.apm_bios_info了从BIOS获得的值。

	/* Disconnect first, just in case */
	ireg.al = 0x04;
	intcall(0x15, &ireg, NULL);

	/* 32-bit connect */
	ireg.al = 0x03;
	intcall(0x15, &ireg, &oreg);

	boot_params.apm_bios_info.cseg        = oreg.ax;
	boot_params.apm_bios_info.offset      = oreg.ebx;
	boot_params.apm_bios_info.cseg_16     = oreg.cx;
	boot_params.apm_bios_info.dseg        = oreg.dx;
	boot_params.apm_bios_info.cseg_len    = oreg.si;
	boot_params.apm_bios_info.cseg_16_len = oreg.hsi;
	boot_params.apm_bios_info.dseg_len    = oreg.di;

	if (oreg.flags & X86_EFLAGS_CF)
		return -1;

	/* Redo the installation check as the 32-bit connect;
	   some BIOSes return different flags this way... */

	ireg.al = 0x00;
	intcall(0x15, &ireg, &oreg);

	if ((oreg.eflags & X86_EFLAGS_CF) || oreg.bx != 0x504d) {
		/* Failure with 32-bit connect, try to disconnect and ignore */
		ireg.al = 0x04;
		intcall(0x15, &ireg, NULL);
		return -1;
	}

	boot_params.apm_bios_info.version = oreg.ax;
	boot_params.apm_bios_info.flags   = oreg.cx;
	return 0;
}

