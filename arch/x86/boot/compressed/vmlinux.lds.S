/* SPDX-License-Identifier: GPL-2.0 */
#include <asm-generic/vmlinux.lds.h>

/**
 *  vmlinux.lds.S的链接规则就可得到 protect mode kernel 的文件结构
 */

OUTPUT_FORMAT(CONFIG_OUTPUT_FORMAT)

#undef i386

#include <asm/cache.h>
#include <asm/page_types.h>

#ifdef CONFIG_X86_64
OUTPUT_ARCH(i386:x86-64)
ENTRY(startup_64)
#else
OUTPUT_ARCH(i386)
ENTRY(startup_32)
#endif

/**
 *
 */
SECTIONS
{
	/* Be careful parts of head_64.S assume startup_32 is at
	 * address 0.
	 */
	. = 0;
	.head.text : {  /* __HEAD 包含了`startup_32` */
		_head = . ;
		HEAD_TEXT
		_ehead = . ;
	}
    /**
     *  由mkpiggy.c文件生成的 piggy.S 中定义的
     *  `.rodata..compressed`包含了压缩了的内核镜像
     */
	.rodata..compressed : {
		*(.rodata..compressed)
	}
    /**
     * `.text`节包含解压代码
     */
	.text :	{
		_text = .; 	/* Text */
		*(.text)
		*(.text.*)
		_etext = . ;
	}
    /**
     *  head_64.S在链接的时候会以“relocated:”标号为界，
     *  relocated之前的代码被链接在compressed kernel之前，
     *  relocated标号之后的代码以及decompressing code都被链接在compressed kernel之后。
     */

    /**
     *
     */
	.rodata : {
		_rodata = . ;
		*(.rodata)	 /* read-only data */
		*(.rodata.*)
		_erodata = . ;
	}
	.data :	{
		_data = . ;
		*(.data)
		*(.data.*)
		*(.bss.efistub)
		_edata = . ;
	}
	. = ALIGN(L1_CACHE_BYTES);
	.bss : {
		_bss = . ;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(8);	/* For convenience during zeroing */
		_ebss = .;
	}
#ifdef CONFIG_X86_64
       . = ALIGN(PAGE_SIZE);
       .pgtable : {
		_pgtable = . ;
		*(.pgtable)
		_epgtable = . ;
	}
#endif
	. = ALIGN(PAGE_SIZE);	/* keep ZO size page aligned */
	_end = .;

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	DISCARDS
	/DISCARD/ : {
		*(.dynamic) *(.dynsym) *(.dynstr) *(.dynbss)
		*(.hash) *(.gnu.hash)
		*(.note.*)
	}

	.got.plt (INFO) : {
		*(.got.plt)
	}
	ASSERT(SIZEOF(.got.plt) == 0 ||
#ifdef CONFIG_X86_64
	       SIZEOF(.got.plt) == 0x18,
#else
	       SIZEOF(.got.plt) == 0xc,
#endif
	       "Unexpected GOT/PLT entries detected!")

	/*
	 * Sections that should stay zero sized, which is safer to
	 * explicitly check instead of blindly discarding.
	 */
	.got : {
		*(.got)
	}
	ASSERT(SIZEOF(.got) == 0, "Unexpected GOT entries detected!")

	.plt : {
		*(.plt) *(.plt.*)
	}
	ASSERT(SIZEOF(.plt) == 0, "Unexpected run-time procedure linkages detected!")

	.rel.dyn : {
		*(.rel.*) *(.rel_*)
	}
	ASSERT(SIZEOF(.rel.dyn) == 0, "Unexpected run-time relocations (.rel) detected!")

	.rela.dyn : {
		*(.rela.*) *(.rela_*)
	}
	ASSERT(SIZEOF(.rela.dyn) == 0, "Unexpected run-time relocations (.rela) detected!")
}
