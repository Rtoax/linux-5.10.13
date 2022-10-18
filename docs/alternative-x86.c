/**
 * 测试 ALTERNATIVE() 在 x86 下是如何使用
 *
 * 编译方法: gcc -E -m32 docs/alternative-x86.c
 * Copyright (C) 荣涛 CESTC
 *
 * 2022-10-18	Rong Tao	Create this.
 */

#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)

#define b_replacement(num)	"664"#num
#define e_replacement(num)	"665"#num

#define alt_end_marker		"663"
#define alt_slen		"662b-661b"
#define alt_pad_len		alt_end_marker"b-662b"
#define alt_total_slen		alt_end_marker"b-661b"
#define alt_rlen(num)		e_replacement(num)"f-"b_replacement(num)"f"

#define OLDINSTR(oldinstr, num)						\
	"# ALT: oldnstr\n"						\
	"661:\n\t" oldinstr "\n662:\n"					\
	"# ALT: padding\n"						\
	".skip -(((" alt_rlen(num) ")-(" alt_slen ")) > 0) * "		\
		"((" alt_rlen(num) ")-(" alt_slen ")),0x90\n"		\
	alt_end_marker ":\n"

#define ALTINSTR_ENTRY(feature, num)					      \
	" .long 661b - .\n"				/* label           */ \
	" .long " b_replacement(num)"f - .\n"		/* new instruction */ \
	" .word " __stringify(feature) "\n"		/* feature bit     */ \
	" .byte " alt_total_slen "\n"			/* source len      */ \
	" .byte " alt_rlen(num) "\n"			/* replacement len */ \
	" .byte " alt_pad_len "\n"			/* pad len */

#define ALTINSTR_REPLACEMENT(newinstr, feature, num)	/* replacement */	\
	"# ALT: replacement " #num "\n"						\
	b_replacement(num)":\n\t" newinstr "\n" e_replacement(num) ":\n"

/* arch/x86/include/asm/alternative.h */
#define ALTERNATIVE(oldinstr, newinstr, feature)			\
	OLDINSTR(oldinstr, 1)						\
	".pushsection .altinstructions,\"a\"\n"				\
	ALTINSTR_ENTRY(feature, 1)					\
	".popsection\n"							\
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	ALTINSTR_REPLACEMENT(newinstr, feature, 1)			\
	".popsection\n"

/* arch/x86/include/asm/barrier.h */
#define mb() asm volatile(ALTERNATIVE("lock; addl $0,-4(%%esp)", "mfence", \
				      X86_FEATURE_XMM2) ::: "memory", "cc")

int main(void)
{
	mb();
	return 0;
}

#ifdef _DO_NOT_COMPILE_
/**
 * 示例结果
 * gcc -E -m32 docs/alternative-x86.c
 */
int main(void)
{
	asm volatile(
		"# ALT: oldnstr\n"
		"661:\n\t"
		"lock; addl $0,-4(%%esp)\n"
		"662:\n"
		"# ALT: padding\n"
		".skip -(((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")) > 0) * " "((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")),0x90\n"
		"663" ":\n"
		".pushsection .altinstructions,\"a\"\n"
		" .long 661b - .\n"
		" .long " "664""1""f - .\n"
		" .word " "X86_FEATURE_XMM2" "\n"
		" .byte " "663""b-661b" "\n"
		" .byte " "665""1""f-""664""1""f" "\n"
		" .byte " "663""b-662b" "\n"
		".popsection\n"
		".pushsection .altinstr_replacement, \"ax\"\n"
		"# ALT: replacement " "1" "\n"
		"664""1"":\n\t"
		"mfence" "\n"
		"665""1" ":\n"
		".popsection\n" ::: "memory", "cc");

	/**
	 * 上述两个 pushsection 用结构 struct alt_instr 描述，且这个结构
	 * 将放入 section .altinstructions ELF 节中
	 */
	struct alt_instr altinstructions = {
		.instr_offset   = addr(oldinstr) - addr(altinstructions),
		.repl_offset    = addr(mfence) - addr(altinstructions),
		.cpuid          = X86_FEATURE_XMM2,
		.instrlen       = addr(altinstructions) - addr(oldinstr),
		.replacementlen = addr(end of mfence) - addr(mfence),
		.padlen         = addr(altinstructions) - addr(end of oldinstr),
	};

	return 0;
}
#endif
