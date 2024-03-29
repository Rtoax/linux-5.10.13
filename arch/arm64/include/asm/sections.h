/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 ARM Limited
 */
#ifndef __ASM_SECTIONS_H
#define __ASM_SECTIONS_H

#include <asm-generic/sections.h>

extern char __alt_instructions[], __alt_instructions_end[];
extern char __hibernate_exit_text_start[], __hibernate_exit_text_end[];
extern char __hyp_idmap_text_start[], __hyp_idmap_text_end[];
extern char __hyp_text_start[], __hyp_text_end[];
extern char __idmap_text_start[], __idmap_text_end[];
extern char __initdata_begin[], __initdata_end[];
extern char __inittext_begin[], __inittext_end[];
extern char __exittext_begin[], __exittext_end[];
extern char __irqentry_text_start[], __irqentry_text_end[];
extern char __mmuoff_data_start[], __mmuoff_data_end[];

/**
 *  在vmlinux 的代码段中新增一个名为 .entry.tramp.text 的段
 *  他的链接地址为 __entry_tramp_text_start
 */
extern char __entry_tramp_text_start[], __entry_tramp_text_end[];

#endif /* __ASM_SECTIONS_H */
