/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/barrier.h
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#ifndef __ASSEMBLY__

#include <linux/kasan-checks.h>

#define __nops(n)	".rept	" #n "\nnop\n.endr\n"
#define nops(n)		asm volatile(__nops(n))

#define sev()		asm volatile("sev" : : : "memory")
#define wfe()		asm volatile("wfe" : : : "memory")
#define wfi()		asm volatile("wfi" : : : "memory")


/**
 *  指令同步屏障 - Instruction Synchronization Barrier
 */
#define isb()		asm volatile("isb" : : : "memory")
/**
 *  数据存储屏障 - Data Memory Barrier
 */
#define dmb(opt)	asm volatile("dmb " #opt : : : "memory")

/**
 *  数据同步屏障 - Data Synchronization Barrier
 */
#define dsb(opt)	asm volatile("dsb " #opt : : : "memory")

#define psb_csync()	asm volatile("hint #17" : : : "memory")

/**
 *  CSDB - Consume Speculative Data Barrier 消费预测数据屏障
 *  为了减缓或者局部修复 幽灵 漏洞， arm64 添加的 指令
 */
#define csdb()		asm volatile("hint #20" : : : "memory")

#define spec_bar()	asm volatile(ALTERNATIVE("dsb nsh\nisb\n",		\
						 SB_BARRIER_INSN"nop\n",	\
						 ARM64_HAS_SB))

#ifdef CONFIG_ARM64_PSEUDO_NMI
#define pmr_sync()						\
	do {							\
		extern struct static_key_false gic_pmr_sync;	\
								\
		if (static_branch_unlikely(&gic_pmr_sync))	\
			dsb(sy);				\
	} while(0)
#else
#define pmr_sync()	do {} while (0)
#endif

/**
 *  读写内存屏障 - 用于 SMP 和 UP
 */
#define mb()		dsb(sy)

/**
 *  读内存屏障 - 用于 SMP 和 UP
 */
#define rmb()		dsb(ld)

/**
 *  写内存屏障 - 用于 SMP 和 UP
 */
#define wmb()		dsb(st)

#define dma_mb()	dmb(osh)
#define dma_rmb()	dmb(oshld)
#define dma_wmb()	dmb(oshst)

/*
 * Generate a mask for array_index__nospec() that is ~0UL when 0 <= idx < sz
 * and 0 otherwise.
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
#define array_index_mask_nospec array_index_mask_nospec
static inline unsigned long array_index_mask_nospec(unsigned long idx,
						    unsigned long sz)
{
	unsigned long mask;

	asm volatile(
	"	cmp	%1, %2\n"
	"	sbc	%0, xzr, xzr\n"
	: "=r" (mask)
	: "r" (idx), "Ir" (sz)
	: "cc");

    /**
     *  CSDB - Consume Speculative Data Barrier 消费预测数据屏障
     *  为了减缓或者局部修复 幽灵 漏洞， arm64 添加的 指令
     */
	csdb();
	return mask;
}

#define __smp_mb()	dmb(ish)
#define __smp_rmb()	dmb(ishld)
#define __smp_wmb()	dmb(ishst)

#define __smp_store_release(p, v)					\
do {									\
	typeof(p) __p = (p);						\
	union { __unqual_scalar_typeof(*p) __val; char __c[1]; } __u =	\
		{ .__val = (__force __unqual_scalar_typeof(*p)) (v) };	\
	compiletime_assert_atomic_type(*p);				\
	kasan_check_write(__p, sizeof(*p));				\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile ("stlrb %w1, %0"				\
				: "=Q" (*__p)				\
				: "r" (*(__u8 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 2:								\
		asm volatile ("stlrh %w1, %0"				\
				: "=Q" (*__p)				\
				: "r" (*(__u16 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 4:								\
		asm volatile ("stlr %w1, %0"				\
				: "=Q" (*__p)				\
				: "r" (*(__u32 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 8:								\
		asm volatile ("stlr %1, %0"				\
				: "=Q" (*__p)				\
				: "r" (*(__u64 *)__u.__c)		\
				: "memory");				\
		break;							\
	}								\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	union { __unqual_scalar_typeof(*p) __val; char __c[1]; } __u;	\
	typeof(p) __p = (p);						\
	compiletime_assert_atomic_type(*p);				\
	kasan_check_read(__p, sizeof(*p));				\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile ("ldarb %w0, %1"				\
			: "=r" (*(__u8 *)__u.__c)			\
			: "Q" (*__p) : "memory");			\
		break;							\
	case 2:								\
		asm volatile ("ldarh %w0, %1"				\
			: "=r" (*(__u16 *)__u.__c)			\
			: "Q" (*__p) : "memory");			\
		break;							\
	case 4:								\
		asm volatile ("ldar %w0, %1"				\
			: "=r" (*(__u32 *)__u.__c)			\
			: "Q" (*__p) : "memory");			\
		break;							\
	case 8:								\
		asm volatile ("ldar %0, %1"				\
			: "=r" (*(__u64 *)__u.__c)			\
			: "Q" (*__p) : "memory");			\
		break;							\
	}								\
	(typeof(*p))__u.__val;						\
})

/**
 * 自旋等待
 */
#define smp_cond_load_relaxed(ptr, cond_expr)				\
({									\
	typeof(ptr) __PTR = (ptr);					\
	__unqual_scalar_typeof(*ptr) VAL;				\
	for (;;) {							\
		VAL = READ_ONCE(*__PTR);				\
		if (cond_expr)						\
			break;						\
		__cmpwait_relaxed(__PTR, VAL);				\
	}								\
	(typeof(*ptr))VAL;						\
})

/**
 * 自旋等待
 *
 * - 例如 queued_spin_lock_slowpath() 中的
 *   atomic_cond_read_acquire(&lock->val, !(VAL & _Q_LOCKED_MASK));
 */
#define smp_cond_load_acquire(ptr, cond_expr)				\
({									\
	typeof(ptr) __PTR = (ptr);					\
	__unqual_scalar_typeof(*ptr) VAL;				\
	for (;;) {							\
		VAL = smp_load_acquire(__PTR);				\
		if (cond_expr)						\
			break;						\
		__cmpwait_relaxed(__PTR, VAL);				\
	}								\
	(typeof(*ptr))VAL;						\
})

#include <asm-generic/barrier.h>

#endif	/* __ASSEMBLY__ */

#endif	/* __ASM_BARRIER_H */
