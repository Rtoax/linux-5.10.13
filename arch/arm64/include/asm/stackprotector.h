/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GCC stack protector support.
 *
 * Stack protector works by putting predefined pattern at the start of
 * the stack frame and verifying that it hasn't been overwritten when
 * returning from the function.  The pattern is called stack canary
 * and gcc expects it to be defined by a global variable called
 * "__stack_chk_guard" on ARM.  This unfortunately means that on SMP
 * we cannot have a different canary value per task.
 */

#ifndef __ASM_STACKPROTECTOR_H
#define __ASM_STACKPROTECTOR_H

#include <linux/random.h>
#include <linux/version.h>
#include <asm/pointer_auth.h>

extern unsigned long __stack_chk_guard;

/*
 * Initialize the stackprotector canary value.
 *
 * NOTE: this must only be called from functions that never return,
 * and it must always be inlined.
 */
static __always_inline void boot_init_stack_canary(void)
{
	/**
	 * 栈保护
	 *
	 * 全局canary对于内核来说并没有太多的工作，只需要在系统启动时设置好__stack_chk_guard
	 * 并定义检测失败的回调__stack_chk_fail 即可，插桩代码均由编译器实现
	 */
#if defined(CONFIG_STACKPROTECTOR)
	unsigned long canary;

	/**
	 * Try to get a semi random initial value.
	 * 获取一个半随机数
	 */
	get_random_bytes(&canary, sizeof(canary));
	canary ^= LINUX_VERSION_CODE;
	canary &= CANARY_MASK;

	current->stack_canary = canary;
	/**
	 * 如果没指定 per thread,则初始化全局canary
	 * 即：
	 *  CONFIG_STACKPROTECTOR =y
	 *  CONFIG_STACKPROTECTOR_STRONG =y
	 *  CONFIG_STACKPROTECTOR_PER_TASK =n
	 */
	if (!IS_ENABLED(CONFIG_STACKPROTECTOR_PER_TASK))
		/**
		 * 默认stack canary使用全局符号(变量) __stack_chk_guard 作为原始
		 * 的canary(后续称为全局canary), 在gcc/clang中均使用相同的名字.
		 */
		__stack_chk_guard = current->stack_canary;
#endif
	ptrauth_thread_init_kernel(current);
	ptrauth_thread_switch_kernel(current);
}

#endif	/* _ASM_STACKPROTECTOR_H */
