/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TEXT_PATCHING_H
#define _ASM_X86_TEXT_PATCHING_H

#include <linux/types.h>
#include <linux/stddef.h>
#include <asm/ptrace.h>

struct paravirt_patch_site;
#ifdef CONFIG_PARAVIRT
void apply_paravirt(struct paravirt_patch_site *start,
		    struct paravirt_patch_site *end);
#else

#endif

/*
 * Currently, the max observed size in the kernel code is
 * JUMP_LABEL_NOP_SIZE/RELATIVEJUMP_SIZE, which are 5.
 * Raise it if needed.
 */
#define POKE_MAX_OPCODE_SIZE	5

extern void text_poke_early(void *addr, const void *opcode, size_t len);

/*
 * Clear and restore the kernel write-protection flag on the local CPU.
 * Allows the kernel to edit read-only pages.
 * Side-effect: any interrupt handler running between save and restore will have
 * the ability to write to read-only pages.
 *
 * Warning:
 * Code patching in the UP case is safe if NMIs and MCE handlers are stopped and
 * no thread can be preempted in the instructions being modified (no iret to an
 * invalid instruction possible) or if the instructions are changed from a
 * consistent state to another consistent state atomically.
 * On the local CPU you need to be protected against NMI or MCE handlers seeing
 * an inconsistent instruction while you patch.
 */
extern void *text_poke(void *addr, const void *opcode, size_t len);
extern void text_poke_sync(void);
extern void *text_poke_kgdb(void *addr, const void *opcode, size_t len);
extern int poke_int3_handler(struct pt_regs *regs);
extern void text_poke_bp(void *addr, const void *opcode, size_t len, const void *emulate);

extern void text_poke_queue(void *addr, const void *opcode, size_t len, const void *emulate);
extern void text_poke_finish(void);

/**
 *  INT3
 */
#define INT3_INSN_SIZE		1
#define INT3_INSN_OPCODE	0xCC

#define RET_INSN_SIZE		1
#define RET_INSN_OPCODE		0xC3

/**
 * @brief Call 指令
 *
 */
#define CALL_INSN_SIZE		5
#define CALL_INSN_OPCODE	0xE8

#define JMP32_INSN_SIZE		5
#define JMP32_INSN_OPCODE	0xE9

#define JMP8_INSN_SIZE		2
#define JMP8_INSN_OPCODE	0xEB

#define DISP32_SIZE		4
/**
 *  获取指令大小
 */
static __always_inline int text_opcode_size(u8 opcode)
{
	int size = 0;

#define __CASE(insn)	\
	case insn##_INSN_OPCODE: size = insn##_INSN_SIZE; break
#if 0
	switch(opcode) {
	__CASE(INT3);   /* INT3_INSN_OPCODE */
	__CASE(RET);    /* RET_INSN_OPCODE */
	__CASE(CALL);   /* CALL_INSN_OPCODE */
	__CASE(JMP32);  /* JMP32_INSN_OPCODE */
	__CASE(JMP8);   /* JMP8_INSN_OPCODE */
	}
#else
	switch(opcode) {
    /**
     *  int3
     */
    case INT3_INSN_OPCODE:  size = INT3_INSN_SIZE; break;
    /**
     *  ret
     */
    case RET_INSN_OPCODE:   size = RET_INSN_SIZE; break;
    /**
     *  call
     */
    case CALL_INSN_OPCODE:  size = CALL_INSN_SIZE; break;
    /**
     *  jmp32
     */
    case JMP32_INSN_OPCODE: size = JMP32_INSN_SIZE; break;
    /**
     *  jmp8
     */
    case JMP8_INSN_OPCODE:  size = JMP8_INSN_SIZE; break;
	}
#endif
#undef __CASE

	return size;
}
/**
 *  一条 x86 指令
 */
union text_poke_insn {
	u8 text[POKE_MAX_OPCODE_SIZE];
	struct {
        /**
         *  操作 指令
         */
		u8 opcode;
        /**
         *	display - 替换的值
         */
		s32 disp;
	} __attribute__((packed));
};

/**
 * @brief 生成指令？
 *
 * @param opcode
 * @param addr
 * @param dest
 * @return __always_inline*
 */
static __always_inline
void *text_gen_insn(u8 opcode, const void *addr, const void *dest)  /* 该函数不可重入?? */
{
    /**
     *  为什么时 static 变量
     */
	static union text_poke_insn insn; /* per instance */
	/**
	 * @brief 指令长度
	 *
	 */
	int size = text_opcode_size(opcode);

    /*  例
    0f 1f 44 00 00 nop
    cc 1f 44 00 00 <bp>nop
    cc 37 2e 00 00 <bp>callq ffffffff810f7430 <ftrace_caller>
    e8 37 2e 00 00 callq ffffffff810f7430 <ftrace_caller>
    */
	insn.opcode = opcode;   /* 操作码-指令 */

    /**
     *
     */
	if (size > 1) {
        /**
		 *  计算替换的值：
         *	替换的值 = 目的地址 - (修改的地址 + 指令长度)		见《Linux二进制分析》
         */
		insn.disp = (long)dest - (long)(addr + size);
		if (size == 2) {
			/*
			 * Ensure that for JMP9 the displacement
			 * actually fits the signed byte.
			 */
			BUG_ON((insn.disp >> 31) != (insn.disp >> 7));
		}
	}

	return &insn.text;
}

extern int after_bootmem;
extern __ro_after_init struct mm_struct *poking_mm;
extern __ro_after_init unsigned long poking_addr;

#ifndef CONFIG_UML_X86
static __always_inline
void int3_emulate_jmp(struct pt_regs *regs, unsigned long ip)
{
	regs->ip = ip;
}

static __always_inline
void int3_emulate_push(struct pt_regs *regs, unsigned long val)
{
	/*
	 * The int3 handler in entry_64.S adds a gap between the
	 * stack where the break point happened, and the saving of
	 * pt_regs. We can extend the original stack because of
	 * this gap. See the idtentry macro's create_gap option.
	 *
	 * Similarly entry_32.S will have a gap on the stack for (any) hardware
	 * exception and pt_regs; see FIXUP_FRAME.
	 */
	regs->sp -= sizeof(unsigned long);
	*(unsigned long *)regs->sp = val;
}

static __always_inline
unsigned long int3_emulate_pop(struct pt_regs *regs)
{
	unsigned long val = *(unsigned long *)regs->sp;
	regs->sp += sizeof(unsigned long);
	return val;
}

static __always_inline
void int3_emulate_call(struct pt_regs *regs, unsigned long func)
{
	int3_emulate_push(regs, regs->ip - INT3_INSN_SIZE + CALL_INSN_SIZE);
	int3_emulate_jmp(regs, func);
}

static __always_inline
void int3_emulate_ret(struct pt_regs *regs)
{
	unsigned long ip = int3_emulate_pop(regs);
	int3_emulate_jmp(regs, ip);
}
#endif /* !CONFIG_UML_X86 */

#endif /* _ASM_X86_TEXT_PATCHING_H */
