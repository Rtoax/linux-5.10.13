/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__LINUX_BPF_COMMON_H__
#define _UAPI__LINUX_BPF_COMMON_H__

/* Instruction classes */
#define BPF_CLASS(code) ((code) & 0x07)
/**
 *  表示将数据存入累加器
 */
#define		BPF_LD		0x00
#define		BPF_LDX		0x01
#define		BPF_ST		0x02
#define		BPF_STX		0x03
#define		BPF_ALU		0x04
#define		BPF_JMP		0x05
#define		BPF_RET		0x06
#define		BPF_MISC        0x07

/* ld/ldx fields */
#define BPF_SIZE(code)  ((code) & 0x18)
/**
 *  表示传双字节
 */
#define		BPF_W		0x00 /* 32-bit */
#define		BPF_H		0x08 /* 16-bit */
#define		BPF_B		0x10 /*  8-bit */
/* eBPF		BPF_DW		0x18    64-bit */
#define BPF_MODE(code)  ((code) & 0xe0)
#define		BPF_IMM		0x00
/**
 *
 */
#define		BPF_ABS		0x20
#define		BPF_IND		0x40
#define		BPF_MEM		0x60
#define		BPF_LEN		0x80
#define		BPF_MSH		0xa0

/* alu/jmp fields */
#define BPF_OP(code)    ((code) & 0xf0)
#define		BPF_ADD		0x00
#define		BPF_SUB		0x10
#define		BPF_MUL		0x20
#define		BPF_DIV		0x30
#define		BPF_OR		0x40
#define		BPF_AND		0x50
#define		BPF_LSH		0x60
#define		BPF_RSH		0x70
#define		BPF_NEG		0x80
#define		BPF_MOD		0x90
#define		BPF_XOR		0xa0

#define		BPF_JA		0x00
/**
 *  相等则跳转
 */
#define		BPF_JEQ		0x10
/**
 *  大于则跳转
 */
#define		BPF_JGT     0x20
#define		BPF_JGE     0x30
#define		BPF_JSET    0x40
#define BPF_SRC(code)        ((code) & 0x08)
#define		BPF_K		0x00
#define		BPF_X		0x08

/**
 * BPF 的一般操作是 64 位，以遵循 64 位体系结构的自然模型，以便执行指针算术、传递指针以及
 * 将 64 位值传递到帮助程序函数中，并允许 64 位原子操作。
 *
 * 每个程序的最大指令限制限制为 4096 BPF 指令，根据设计，这意味着任何程序都将快速终止。
 * 对于高于 5.1 的内核，此限制已提升到 100 万条 BPF 指令。尽管指令集包含向前和向后跳转，
 * 但内核内 BPF 验证器将禁止循环，以便始终保证终止。由于 BPF 程序在内核内运行，因此验证器
 * 的工作是确保这些程序可以安全运行，而不会影响系统的稳定性。这意味着从指令集的角度来看，可
 * 以实现循环，但验证器会限制这一点。但是，还有一个尾部调用的概念，它允许一个 BPF 程序跳转
 * 到另一个 BPF 程序。这也附带了 33 次调用的嵌套上限，通常用于将部分程序逻辑解耦，例如，
 * 分成阶段。
 */
#ifndef BPF_MAXINSNS
#define BPF_MAXINSNS 4096
#endif

#endif /* _UAPI__LINUX_BPF_COMMON_H__ */
