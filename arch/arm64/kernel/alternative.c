// SPDX-License-Identifier: GPL-2.0-only
/*
 * alternative runtime patching
 * inspired by the x86 version
 *
 * Copyright (C) 2014 ARM Ltd.
 */

#define pr_fmt(fmt) "alternatives: " fmt

#include <linux/init.h>
#include <linux/cpu.h>
#include <asm/cacheflush.h>
#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <asm/insn.h>
#include <asm/sections.h>
#include <linux/stop_machine.h>

#define __ALT_PTR(a,f)		((void *)&(a)->f + (a)->f)
#define ALT_ORIG_PTR(a)		__ALT_PTR(a, orig_offset)
#define ALT_REPL_PTR(a)		__ALT_PTR(a, alt_offset)

static int all_alternatives_applied;

static DECLARE_BITMAP(applied_alternatives, ARM64_NCAPS);

struct alt_region {
	struct alt_instr *begin;
	struct alt_instr *end;
};

bool alternative_is_applied(u16 cpufeature)
{
	if (WARN_ON(cpufeature >= ARM64_NCAPS))
		return false;

	return test_bit(cpufeature, applied_alternatives);
}

/*
 * Check if the target PC is within an alternative block.
 */
static bool branch_insn_requires_update(struct alt_instr *alt, unsigned long pc)
{
	/* (void *)&(a)->alt_offset + (a)->alt_offset */
	unsigned long replptr = (unsigned long)ALT_REPL_PTR(alt);
	return !(pc >= replptr && pc <= (replptr + alt->alt_len));
}

#define align_down(x, a)	((unsigned long)(x) & ~(((unsigned long)(a)) - 1))

static u32 get_alt_insn(struct alt_instr *alt, __le32 *insnptr, __le32 *altinsnptr)
{
	u32 insn;

	insn = le32_to_cpu(*altinsnptr);

	/**
	 * 如果是分支指令
	 */
	if (aarch64_insn_is_branch_imm(insn)) {
		/* 获取偏移量 */
		s32 offset = aarch64_get_branch_offset(insn);
		unsigned long target;

		target = (unsigned long)altinsnptr + offset;

		/*
		 * If we're branching inside the alternate sequence,
		 * do not rewrite the instruction, as it is already
		 * correct. Otherwise, generate the new instruction.
		 */
		if (branch_insn_requires_update(alt, target)) {
			offset = target - (unsigned long)insnptr;
			insn = aarch64_set_branch_offset(insn, offset);
		}
	/**
	 * ADR 指令将基于PC 相对偏移的地址值读取到寄存器中
	 * ADRP 以页为单位的大范围的地址读取指令，这里的P就是page的意思。
	 *
	 * 通俗来讲，ADRP指令就是先进行PC+imm（偏移值）然后找到lable所在的一个4KB
	 * 的页，然后取得label的基址，再进行偏移去寻址
	 */
	} else if (aarch64_insn_is_adrp(insn)) {
		s32 orig_offset, new_offset;
		unsigned long target;

		/*
		 * If we're replacing an adrp instruction, which uses PC-relative
		 * immediate addressing, adjust the offset to reflect the new
		 * PC. adrp operates on 4K aligned addresses.
		 */
		orig_offset  = aarch64_insn_adrp_get_offset(insn);
		target = align_down(altinsnptr, SZ_4K) + orig_offset;
		new_offset = target - align_down(insnptr, SZ_4K);
		insn = aarch64_insn_adrp_set_offset(insn, new_offset);
	/**
	 *
	 */
	} else if (aarch64_insn_uses_literal(insn)) {
		/*
		 * Disallow patching unhandled instructions using PC relative
		 * literal addresses
		 */
		BUG();
	}

	return insn;
}

/**
 * alternative 指令替换，这是 arm64 alternative 功能的核心函数
 *
 * @param alt - 对应的 alt_instr 结构，在 宏定义 ALTERNATIVE() 中创建
 * @param origptr - 原始指令
 * @param updptr - 原始指令的地址
 * @param nr_inst - 指令个数
 */
static void patch_alternative(struct alt_instr *alt,
			      __le32 *origptr, __le32 *updptr, int nr_inst)
{
	__le32 *replptr;
	int i;

	/* (void *)&(a)->alt_offset + (a)->alt_offset */
	replptr = ALT_REPL_PTR(alt);
	for (i = 0; i < nr_inst; i++) {
		// aarch64 固定指令长度为 4
		u32 insn;

		/**
		 * 获取 alt 指令
		 */
		insn = get_alt_insn(alt, origptr + i, replptr + i);

		// 进行替换，也就是写入代码段内存
		updptr[i] = cpu_to_le32(insn);
	}
}

/*
 * We provide our own, private D-cache cleaning function so that we don't
 * accidentally call into the cache.S code, which is patched by us at
 * runtime.
 */
static void clean_dcache_range_nopatch(u64 start, u64 end)
{
	u64 cur, d_size, ctr_el0;

	ctr_el0 = read_sanitised_ftr_reg(SYS_CTR_EL0);
	d_size = 4 << cpuid_feature_extract_unsigned_field(ctr_el0,
							   CTR_DMINLINE_SHIFT);
	cur = start & ~(d_size - 1);
	do {
		/*
		 * We must clean+invalidate to the PoC in order to avoid
		 * Cortex-A53 errata 826319, 827319, 824069 and 819472
		 * (this corresponds to ARM64_WORKAROUND_CLEAN_CACHE)
		 */
		asm volatile("dc civac, %0" : : "r" (cur) : "memory");
	} while (cur += d_size, cur < end);
}

/**
 * alternative 在 arm64 上的实现
 *
 * TODO: 2022年5月9日 vDSO 看过来
 * 解释TODO： 因为 vDSO 是需要 CPU 特性的加入，如 rdtsc，所以需要 alternative
 */
static void __apply_alternatives(void *alt_region,  bool is_module,
				 unsigned long *feature_mask)
{
	struct alt_instr *alt;
	struct alt_region *region = alt_region;
	__le32 *origptr, *updptr;
	alternative_cb_t alt_cb;

	/**
	 * 遍历所有 alternative
	 *
	 */
	for (alt = region->begin; alt < region->end; alt++) {
		int nr_inst;

		/**
		 * 检测 cpu 特性
		 * 如果不支持，直接跳过
		 */
		if (!test_bit(alt->cpufeature, feature_mask))
			continue;

		/* Use ARM64_CB_PATCH(ARM64_NCAPS) as an unconditional patch */
		if (alt->cpufeature < ARM64_CB_PATCH &&
		    !cpus_have_cap(alt->cpufeature))
			continue;

		/* ARM64_CB_PATCH: */
		if (alt->cpufeature == ARM64_CB_PATCH)
			BUG_ON(alt->alt_len != 0);
		else
			BUG_ON(alt->alt_len != alt->orig_len);

		/**
		 * 开始替换
		 */
		pr_info_once("patching kernel code\n");

		/**
		 * 原始指令的地址
		 * (void *)&(a)->orig_offset + (a)->orig_offset
		 */
		origptr = ALT_ORIG_PTR(alt);

		/**
		 * TODO：这里不明白。2022-10-16 21:01
		 */
		updptr = is_module ? origptr : lm_alias(origptr);

		/**
		 * 指令个数
		 * arm64 左右指令长度均为 4 (AARCH64_INSN_SIZE)
		 */
		nr_inst = alt->orig_len / AARCH64_INSN_SIZE;

		// CPU 支持的特性
		if (alt->cpufeature < ARM64_CB_PATCH)
			// 这是 arm64 alternative 功能的核心函数
			alt_cb = patch_alternative;
		else
			/**
			 * (void *)&(a)->alt_offset + (a)->alt_offset
			 * 这里的意思是，如果超出了内和定义的特性范围，需要自定义回调函数
			 * 所以，如果你在内和中搜索这个回调函数，是搜不到的。
			 */
			alt_cb  = ALT_REPL_PTR(alt);

		// 调用回调函数
		// 如果是支持的 CPU 特性，调用 patch_alternative()
		alt_cb(alt, origptr, updptr, nr_inst);

		/**
		 * 清理缓存
		 */
		if (!is_module) {
			clean_dcache_range_nopatch((u64)origptr,
						   (u64)(origptr + nr_inst));
		}
	}

	/*
	 * The core module code takes care of cache maintenance in
	 * flush_module_icache().
	 */
	if (!is_module) {
		dsb(ish);
		/**
		 * 刷新所有指令缓存
		 */
		__flush_icache_all();
		isb();

		/* Ignore ARM64_CB bit from feature mask */
		bitmap_or(applied_alternatives, applied_alternatives,
			  feature_mask, ARM64_NCAPS);
		bitmap_and(applied_alternatives, applied_alternatives,
			   cpu_hwcaps, ARM64_NCAPS);
	}
}

/*
 * We might be patching the stop_machine state machine, so implement a
 * really simple polling protocol here.
 */
static int __apply_alternatives_multi_stop(void *unused)
{
	struct alt_region region = {
		.begin	= (struct alt_instr *)__alt_instructions,
		.end	= (struct alt_instr *)__alt_instructions_end,
	};

	/* We always have a CPU 0 at this point (__init) */
	if (smp_processor_id()) {
		while (!READ_ONCE(all_alternatives_applied))
			cpu_relax();
		isb();
	} else {
		DECLARE_BITMAP(remaining_capabilities, ARM64_NPATCHABLE);

		bitmap_complement(remaining_capabilities, boot_capabilities,
				  ARM64_NPATCHABLE);

		BUG_ON(all_alternatives_applied);
		__apply_alternatives(&region, false, remaining_capabilities);
		/* Barriers provided by the cache flushing */
		WRITE_ONCE(all_alternatives_applied, 1);
	}

	return 0;
}

void __init apply_alternatives_all(void)
{
	/* better not try code patching on a live SMP system */
	stop_machine(__apply_alternatives_multi_stop, NULL, cpu_online_mask);
}

/*
 * This is called very early in the boot process (directly after we run
 * a feature detect on the boot CPU). No need to worry about other CPUs
 * here.
 *
 * 启动过程调用这个函数，根据 CPU 特性修改指令
 */
void __init apply_boot_alternatives(void)
{
	/**
	 * 见
	 * arch/arm64/kernel/vmlinux.lds.S
	 * arch/x86/kernel/vmlinux.lds.S
	 *
	 * 这将遍历所有在 begin 和 end 之间的 struct alt_instr { ... }
	 */
	struct alt_region region = {
		.begin	= (struct alt_instr *)__alt_instructions,
		.end	= (struct alt_instr *)__alt_instructions_end,
	};

	/* If called on non-boot cpu things could go wrong */
	WARN_ON(smp_processor_id() != 0);

	/* 不是模块 */
	__apply_alternatives(&region, false, &boot_capabilities[0]);
}

#ifdef CONFIG_MODULES
void apply_alternatives_module(void *start, size_t length)
{
	struct alt_region region = {
		.begin	= start,
		.end	= start + length,
	};
	DECLARE_BITMAP(all_capabilities, ARM64_NPATCHABLE);

	/**
	 * 为什么所有的能力都置为 1？
	 * 后面要作出修改
	 */
	bitmap_fill(all_capabilities, ARM64_NPATCHABLE);

	/* true: ko 模块 */
	__apply_alternatives(&region, true, &all_capabilities[0]);
}
#endif
