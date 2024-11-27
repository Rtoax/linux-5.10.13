/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_VSYSCALL_H
#define _UAPI_ASM_X86_VSYSCALL_H

enum vsyscall_num {
	__NR_vgettimeofday,
	__NR_vtime,
	__NR_vgetcpu,
};


/**
 * vsyscall 映射的起始地址
 *
 * ffffffffff600000-ffffffffff601000 --xp 00000000 00:00 0 [vsyscall]
 */
#define VSYSCALL_ADDR /* 0xFFFF FFF6 0000 0000 */ (-10UL << 20)  __ro_after_init

#endif /* _UAPI_ASM_X86_VSYSCALL_H */
