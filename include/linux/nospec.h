// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2018 Linus Torvalds. All rights reserved.
// Copyright(c) 2018 Alexei Starovoitov. All rights reserved.
// Copyright(c) 2018 Intel Corporation. All rights reserved.

#ifndef _LINUX_NOSPEC_H
#define _LINUX_NOSPEC_H

#include <linux/compiler.h>
#include <asm/barrier.h>

struct task_struct;

/**
 * array_index_mask_nospec() - generate a ~0 mask when index < size, 0 otherwise
 * @index: array element index
 * @size: number of elements in array
 *
 * When @index is out of bounds (@index >= @size), the sign bit will be
 * set.  Extend the sign bit to all bits and invert, giving a result of
 * zero for an out of bounds index, or ~0 if within bounds [0, @size).
 *
 * 下面是 array_index_nospec 函数的注释
 * -------------------------------------------------------------------------
 * array_index_nospec - sanitize an array index after a bounds check
 *                      在边界检查后清理数组索引
 * For a code sequence(顺序) like:
 *
 *     if (index < size) {
 *         index = array_index_nospec(index, size);
 *         val = array[index];
 *     }
 *
 * ...if the CPU speculates past the bounds check then
 * array_index_nospec() will clamp the index within the range of [0,
 * size).
 *
 * 如果 CPU 推测超出边界检查，则 array_index_nospec() 会将索引限制在 [0,size) 的范围内。
 *
 * array_index_nospec 是如何避免幽灵漏洞的? 在边界检查后清理数组索引
 *  软件角度只能 减缓 或 局部修复 熔断漏洞
 *  该函数可以确保即使在分支预取的情况下，也不会发生边界越界情况。
 */
#ifndef array_index_mask_nospec
static inline unsigned long array_index_mask_nospec(unsigned long index,
						    unsigned long size)
{
	/*
	 * Always calculate and emit the mask even if the compiler
	 * thinks the mask is not needed. The compiler does not take
	 * into account the value of @index under speculation.
	 */
	OPTIMIZER_HIDE_VAR(index);
	return ~(long)(index | (size - 1UL - index)) >> (BITS_PER_LONG - 1);
}
#endif

/*
 * array_index_nospec - sanitize an array index after a bounds check
 *                      在边界检查后清理数组索引
 * For a code sequence(顺序) like:
 *
 *     if (index < size) {
 *         index = array_index_nospec(index, size);
 *         val = array[index];
 *     }
 *
 * ...if the CPU speculates past the bounds check then
 * array_index_nospec() will clamp the index within the range of [0,
 * size).
 *
 * 如果 CPU 推测超出边界检查，则 array_index_nospec() 会将索引限制在 [0,size) 的范围内。
 *
 * array_index_nospec 是如何避免幽灵漏洞的? 在边界检查后清理数组索引
 *  软件角度只能 减缓 或 局部修复 熔断漏洞
 *  该函数可以确保即使在分支预取的情况下，也不会发生边界越界情况。
 *
 *
 * gcc 对此的优化,`type __builtin_speculation_safe_value(type val, type failval)`, 例：
 *  ---------------------------------
 *  int load_array(unsigned untrusted_index) {
 *      if(untrusted_index < MAX_ARRAY_ELEMS) {
 *          return array[__builtin_speculation_safe_value(untrusted_index)];
 *      }
 *      return 0;
 *  }
 */
#define array_index_nospec(index, size)					\
({									\
	typeof(index) _i = (index);					\
	typeof(size) _s = (size);					\
	unsigned long _mask = array_index_mask_nospec(_i, _s);		\
									\
	BUILD_BUG_ON(sizeof(_i) > sizeof(long));			\
	BUILD_BUG_ON(sizeof(_s) > sizeof(long));			\
									\
	(typeof(_i)) (_i & _mask);					\
})

/* Speculation control prctl */
int arch_prctl_spec_ctrl_get(struct task_struct *task, unsigned long which);
int arch_prctl_spec_ctrl_set(struct task_struct *task, unsigned long which,
			     unsigned long ctrl);
/* Speculation control for seccomp enforced mitigation */
void arch_seccomp_spec_mitigate(struct task_struct *task);

#endif /* _LINUX_NOSPEC_H */
