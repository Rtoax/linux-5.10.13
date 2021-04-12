// SPDX-License-Identifier: GPL-2.0
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pgtable.h>

#include <asm/proto.h>
#include <asm/cpufeature.h>

static int disable_nx;

/*
 * noexec = on|off
 *
 * Control non-executable mappings for processes.
 *
 * on      Enable
 * off     Disable
 */
static int __init noexec_setup(char *str)
{
	if (!str)
		return -EINVAL;
	if (!strncmp(str, "on", 2)) {
		disable_nx = 0;
	} else if (!strncmp(str, "off", 3)) {
		disable_nx = 1;
	}
	x86_configure_nx();
	return 0;
}
early_param("noexec", noexec_setup);

/**
 * `NX` 配置。`NX-bit` 或者 `no-execute` 位是页目录条目的第 63 比特位。
 * 它的作用是控制被映射的物理页面是否具有执行代码的能力。
 * 这个比特位只会在通过把 `EFER.NXE` 置为1使能 `no-execute` 页保护机制
 * 的时候被使用/设置。在 `x86_configure_nx` 函数中会检查 `CPU` 是否支持
 * `NX-bit`，以及是否被禁用。
 */
void x86_configure_nx(void)/*No Exec  */
{
	if (boot_cpu_has(X86_FEATURE_NX/* 不可执行 */) && !disable_nx)
		__supported_pte_mask |= _PAGE_NX;/* No exec */
	else
		__supported_pte_mask &= ~_PAGE_NX;
}

/**
 * 如我的 LOG
 * # sudo cat /var/log/messages | grep Execute
 * Mar  2 08:58:32 localhost kernel: NX (Execute Disable) protection: active
 */
void __init x86_report_nx(void)
{
	if (!boot_cpu_has(X86_FEATURE_NX)) {
		printk(KERN_NOTICE "Notice: NX (Execute Disable) protection "
		       "missing in CPU!\n");
	} else {
#if defined(CONFIG_X86_64) || defined(CONFIG_X86_PAE)
		if (disable_nx) {
			printk(KERN_INFO "NX (Execute Disable) protection: "
			       "disabled by kernel command line option\n");
		} else {
            /* 正常情况下执行这里 */
			printk(KERN_INFO "NX (Execute Disable) protection: "
			       "active\n");
		}
#else
		/* 32bit non-PAE kernel, NX cannot be used */
		printk(KERN_NOTICE "Notice: NX (Execute Disable) protection "
		       "cannot be enabled: non-PAE kernel!\n");
#endif
	}
}
