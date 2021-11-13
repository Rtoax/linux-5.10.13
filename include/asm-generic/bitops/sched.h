/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_BITOPS_SCHED_H_
#define _ASM_GENERIC_BITOPS_SCHED_H_

#include <linux/compiler.h>	/* unlikely() */
#include <asm/types.h>

/*
 * Every architecture must define this function. It's the fastest
 * way of searching a 100-bit bitmap.  It's guaranteed that at least
 * one of the 100 bits is cleared.
 */
static inline int sched_find_first_bit(const unsigned long *b)
{
	if (b[0])
		return __ffs(b[0]);
	return __ffs(b[1]) + 64;
}

#endif /* _ASM_GENERIC_BITOPS_SCHED_H_ */
