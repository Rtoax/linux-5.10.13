/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_EXPORT_H
#define _LINUX_EXPORT_H

/*
 * Export symbols from the kernel to modules.  Forked from module.h
 * to reduce the amount of pointless cruft we feed to gcc when only
 * exporting a simple symbol or two.
 *
 * Try not to add #includes here.  It slows compilation and makes kernel
 * hackers place grumpy comments in header files.
 */

#ifndef __ASSEMBLY__
#ifdef MODULE
extern struct module __this_module;
#define THIS_MODULE (&__this_module)
#else
#define THIS_MODULE ((struct module *)0)
#endif

#ifdef CONFIG_MODVERSIONS
/* Mark the CRC weak since genksyms apparently decides not to
 * generate a checksums for some symbols */
#if defined(CONFIG_MODULE_REL_CRCS)

#else
#define __CRC_SYMBOL(sym, sec)						\
	asm("	.section \"___kcrctab" sec "+" #sym "\", \"a\"	\n"	\
	    "	.weak	__crc_" #sym "				\n"	\
	    "	.long	__crc_" #sym "				\n"	\
	    "	.previous					\n")
#endif
#else

#endif

#ifdef CONFIG_HAVE_ARCH_PREL32_RELOCATIONS
#include <linux/compiler.h>
/*
 * Emit the ksymtab entry as a pair of relative references: this reduces
 * the size by half on 64-bit architectures, and eliminates the need for
 * absolute relocations that require runtime processing on relocatable
 * kernels.
 */
#define __KSYMTAB_ENTRY(sym, sec)					\
	__ADDRESSABLE(sym)						\
	asm("	.section \"___ksymtab" sec "+" #sym "\", \"a\"	\n"	\
	    "	.balign	4					\n"	\
	    "__ksymtab_" #sym ":				\n"	\
	    "	.long	" #sym "- .				\n"	\
	    "	.long	__kstrtab_" #sym "- .			\n"	\
	    "	.long	__kstrtabns_" #sym "- .			\n"	\
	    "	.previous					\n")

struct kernel_symbol {
	int value_offset;
	int name_offset;
	int namespace_offset;
};
#else

#endif

#ifdef __GENKSYMS__

#else

/*
 * For every exported symbol, do the following:
 *
 * - If applicable, place a CRC entry in the __kcrctab section.
 * - Put the name of the symbol and namespace (empty string "" for none) in
 *   __ksymtab_strings.
 * - Place a struct kernel_symbol entry in the __ksymtab section.
 *
 * note on .section use: we specify progbits since usage of the "M" (SHF_MERGE)
 * section flag requires it. Use '%progbits' instead of '@progbits' since the
 * former apparently works on all arches according to the binutils source.
 */
#define ___EXPORT_SYMBOL(sym, sec, ns)						\
	extern typeof(sym) sym;							\
	extern const char __kstrtab_##sym[];					\
	extern const char __kstrtabns_##sym[];					\
	__CRC_SYMBOL(sym, sec);							\
	asm("	.section \"__ksymtab_strings\",\"aMS\",%progbits,1	\n"	\
	    "__kstrtab_" #sym ":					\n"	\
	    "	.asciz 	\"" #sym "\"					\n"	\
	    "__kstrtabns_" #sym ":					\n"	\
	    "	.asciz 	\"" ns "\"					\n"	\
	    "	.previous						\n");	\
	__KSYMTAB_ENTRY(sym, sec)

#endif

#if !defined(CONFIG_MODULES) || defined(__DISABLE_EXPORTS)

#elif defined(CONFIG_TRIM_UNUSED_KSYMS)

#else

#define __EXPORT_SYMBOL(sym, sec, ns)	___EXPORT_SYMBOL(sym, sec, ns)

#endif /* CONFIG_MODULES */

#ifdef DEFAULT_SYMBOL_NAMESPACE

#else
#define _EXPORT_SYMBOL(sym, sec)	__EXPORT_SYMBOL(sym, sec, "")
#endif

/**
 *
 */
#define EXPORT_SYMBOL(sym)		_EXPORT_SYMBOL(sym, "")
#define EXPORT_SYMBOL_GPL(sym)		_EXPORT_SYMBOL(sym, "_gpl")
#define EXPORT_SYMBOL_GPL_FUTURE(sym)	_EXPORT_SYMBOL(sym, "_gpl_future")
#define EXPORT_SYMBOL_NS(sym, ns)	__EXPORT_SYMBOL(sym, "", #ns)
#define EXPORT_SYMBOL_NS_GPL(sym, ns)	__EXPORT_SYMBOL(sym, "_gpl", #ns)


/**********************************************************************************************************************\
 *  EXPORT_SYMBOL 的展开示例 开始
\**********************************************************************************************************************/
int rtoax()
{

}
EXPORT_SYMBOL(rtoax);

//=================>>

extern typeof(rtoax) rtoax;
extern const char __kstrtab_rtoax[];
extern const char __kstrtabns_rtoax[];
asm("	.section \"___kcrctab+rtoax\", \"a\"	\n"
    "	.weak	__crc_rtoax				\n"
    "	.long	__crc_rtoax				\n"
    "	.previous					\n");

asm("	.section \"__ksymtab_strings\",\"aMS\",%progbits,1	\n"
    "__kstrtab_rtoax:					\n"
    "	.asciz 	\"rtoax\"					\n"
    "__kstrtabns_rtoax:					\n"
    "	.asciz 	\"rtoax\"					\n"
    "	.previous						\n");
static void * __section(".discard.addressable") __used __UNIQUE_ID___addressable_rtoax__COUNTER__ = (void *)&rtoax;
asm("	.section \"___ksymtab+rtoax\", \"a\"	\n"
    "	.balign	4					\n"
    "__ksymtab_rtoax:				\n"
    "	.long	rtoax- .				\n"
    "	.long	__kstrtab_rtoax- .			\n"
    "	.long	__kstrtabns_rtoax- .			\n"
    "	.previous					\n");

/**********************************************************************************************************************\
 *  EXPORT_SYMBOL 的展开示例 结束
\**********************************************************************************************************************/


#ifdef CONFIG_UNUSED_SYMBOLS
#define EXPORT_UNUSED_SYMBOL(sym)	_EXPORT_SYMBOL(sym, "_unused")
#define EXPORT_UNUSED_SYMBOL_GPL(sym)	_EXPORT_SYMBOL(sym, "_unused_gpl")
#else

#endif

#endif /* !__ASSEMBLY__ */

#endif /* _LINUX_EXPORT_H */
