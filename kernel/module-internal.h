/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Module internals
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/elf.h>
#include <asm/module.h>

/**
 *  模块加载信息
 */
struct load_info {
    /**
     *
     */
	const char *name;

	/* pointer to module in temporary copy, freed at end of load_module() */
	struct module *mod;

    /**
     *  ko 文件在内存中的首地址 和 长度
     */
	Elf_Ehdr *hdr;
	unsigned long len;

    /**
     *
     */
	Elf_Shdr *sechdrs;
	char *secstrings, *strtab;
	unsigned long symoffs, stroffs, init_typeoffs, core_typeoffs;

    /**
     *
     */
	struct _ddebug *debug;
	unsigned int num_debug;
	bool sig_ok;

#ifdef CONFIG_KALLSYMS
	unsigned long mod_kallsyms_init_off;
#endif
    /**
     *  readelf -S xxx.ko 查看
     */
	struct {
		unsigned int
            sym,    /* SHT_SYMTAB */
            str,    /* SHT_SYMTAB */
            mod,    /* ".gnu.linkonce.this_module" */
            vers,   /* "__versions" */
            info,   /* ".modinfo" */
            pcpu;   /* ".data..percpu" */
	} index;
};

extern int mod_verify_sig(const void *mod, struct load_info *info);
