/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_VSYSCALL_H
#define _UAPI_ASM_X86_VSYSCALL_H

enum vsyscall_num { /* 为什么只有这几个系统调用 */
	__NR_vgettimeofday,
	__NR_vtime,
	__NR_vgetcpu,
};


#define VSYSCALL_ADDR /* 0xFFFF FFF6 0000 0000 */ (-10UL << 20)  __ro_after_init /* vsyscall 映射的起始地址 */

#endif /* _UAPI_ASM_X86_VSYSCALL_H */
