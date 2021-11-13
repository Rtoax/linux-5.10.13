/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_LOCAL64_H
#define _ASM_GENERIC_LOCAL64_H

#include <linux/percpu.h>
#include <asm/types.h>

/*
 * A signed long type for operations which are atomic for a single CPU.
 * Usually used in combination with per-cpu variables.
 *
 * This is the default implementation, which uses atomic64_t.  Which is
 * rather pointless.  The whole point behind local64_t is that some processors
 * can perform atomic adds and subtracts in a manner which is atomic wrt IRQs
 * running on this CPU.  local64_t allows exploitation of such capabilities.
 */

/* Implement in terms of atomics. */

#include <asm/local.h>

typedef struct {
	local_t a;
} local64_t;

#define LOCAL64_INIT(i)	{ LOCAL_INIT(i) }

#define local64_read(l)		local_read(&(l)->a)
#define local64_set(l,i)	local_set((&(l)->a),(i))
#define local64_inc(l)		local_inc(&(l)->a)
#define local64_dec(l)		local_dec(&(l)->a)
#define local64_add(i,l)	local_add((i),(&(l)->a))
#define local64_sub(i,l)	local_sub((i),(&(l)->a))

#define local64_sub_and_test(i, l) local_sub_and_test((i), (&(l)->a))
#define local64_dec_and_test(l) local_dec_and_test(&(l)->a)
#define local64_inc_and_test(l) local_inc_and_test(&(l)->a)
#define local64_add_negative(i, l) local_add_negative((i), (&(l)->a))
#define local64_add_return(i, l) local_add_return((i), (&(l)->a))
#define local64_sub_return(i, l) local_sub_return((i), (&(l)->a))
#define local64_inc_return(l)	local_inc_return(&(l)->a)

#define local64_cmpxchg(l, o, n) local_cmpxchg((&(l)->a), (o), (n))
#define local64_xchg(l, n)	local_xchg((&(l)->a), (n))
#define local64_add_unless(l, _a, u) local_add_unless((&(l)->a), (_a), (u))
#define local64_inc_not_zero(l)	local_inc_not_zero(&(l)->a)

/* Non-atomic variants, ie. preemption disabled and won't be touched
 * in interrupt, etc.  Some archs can optimize this case well. */
#define __local64_inc(l)	local64_set((l), local64_read(l) + 1)
#define __local64_dec(l)	local64_set((l), local64_read(l) - 1)
#define __local64_add(i,l)	local64_set((l), local64_read(l) + (i))
#define __local64_sub(i,l)	local64_set((l), local64_read(l) - (i))


#endif /* _ASM_GENERIC_LOCAL64_H */
