/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <linux/hrtimer.h>
#include <linux/timekeeper_internal.h>
#include <vdso/datapage.h>
#include <asm/vgtod.h>
#include <asm/vvar.h>
/**
 * 展开 asm/vvar.h
 * #define DECLARE_VVAR(offset, type, name)				\
 *	extern type vvar_ ## name[CS_BASES]				\
 *	__attribute__((visibility("hidden")));				\
 *	extern type timens_ ## name[CS_BASES]				\
 *	__attribute__((visibility("hidden")));				\
 * DECLARE_VVAR(128, struct vdso_data, _vdso_data)
 * >>>>>
 * extern struct vdso_data vvar__vdso_data[CS_BASES] __attribute__((visibility("hidden")));
 * extern struct vdso_data timens__vdso_data[CS_BASES] __attribute__((visibility("hidden")));
 * Rong Tao 2022.05.10
 */
extern struct vdso_data vvar__vdso_data[CS_BASES] __attribute__((visibility("hidden")));
extern struct vdso_data timens__vdso_data[CS_BASES] __attribute__((visibility("hidden")));

DEFINE_VVAR(struct vdso_data, _vdso_data);
/* 展开 */
struct vdso_data _vdso_data[CS_BASES]\
	__attribute__((section(".vvar__vdso_data"), aligned(16))) __visible;
/*
 * Update the vDSO data page to keep in sync with kernel timekeeping.
 */
static __always_inline
struct vdso_data *__x86_get_k_vdso_data(void)
{
	return _vdso_data;
}
#define __arch_get_k_vdso_data __x86_get_k_vdso_data

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
