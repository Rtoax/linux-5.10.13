/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vsyscall_emu_64.S: Vsyscall emulation page
 *
 * Copyright (c) 2011 Andy Lutomirski
 */

#include <linux/linkage.h>

#include <asm/irq_vectors.h>
#include <asm/page_types.h>
#include <asm/unistd_64.h>

__PAGE_ALIGNED_DATA
	.globl __vsyscall_page  /* ffffffff82c09000 */
	.balign PAGE_SIZE, 0xcc
	.type __vsyscall_page, @object
__vsyscall_page:    /* ffffffff82c09000 */

	mov $__NR_gettimeofday, %rax
	/**
	 * vsyscall 将 int80 转化为了 syscall 指令
	 */
	syscall
	ret

	.balign 1024, 0xcc
	mov $__NR_time, %rax
	syscall
	ret

	.balign 1024, 0xcc
	mov $__NR_getcpu, %rax
	syscall
	ret

	.balign 4096, 0xcc

	.size __vsyscall_page, 4096
