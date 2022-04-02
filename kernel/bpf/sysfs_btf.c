// SPDX-License-Identifier: GPL-2.0
/*
 * Provide kernel BTF information for introspection and use by eBPF tools.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/init.h>
#include <linux/sysfs.h>

/**
 * See scripts/link-vmlinux.sh, gen_btf() func for details
 *
 * 参见
 * CONFIG_DEBUG_INFO_BTF
 */
extern char __weak __start_BTF[];
extern char __weak __stop_BTF[];

static ssize_t
btf_vmlinux_read(struct file *file, struct kobject *kobj,
		 struct bin_attribute *bin_attr,
		 char *buf, loff_t off, size_t len)
{
	/**
	 * @brief 读取 BTF
	 *
	 */
	memcpy(buf, __start_BTF + off, len);
	return len;
}

/**
 * @brief /sys/kernel/btf/vmlinux
 *
 * 参见
 * btf_vmlinux_init() 初始化
 * libbpf_find_kernel_btf() 函数
 */
static struct bin_attribute __ro_after_init bin_attr_btf_vmlinux  = {
	.attr = {
		.name = "vmlinux",
		.mode = 0444,
	},
	.read = btf_vmlinux_read,
};

/**
 * @brief
 *
 */
static struct kobject *btf_kobj;

/**
 * @brief BTF 初始化
 *
 * @return int
 */
static int __init btf_vmlinux_init(void)
{
	bin_attr_btf_vmlinux.size = __stop_BTF - __start_BTF;

	if (!__start_BTF || bin_attr_btf_vmlinux.size == 0)
		return 0;

	btf_kobj = kobject_create_and_add("btf", kernel_kobj);
	if (!btf_kobj)
		return -ENOMEM;

	/**
	 * @brief /sys/kernel/btf/vmlinux
	 */
	return sysfs_create_bin_file(btf_kobj, &bin_attr_btf_vmlinux);
}

subsys_initcall(btf_vmlinux_init);
