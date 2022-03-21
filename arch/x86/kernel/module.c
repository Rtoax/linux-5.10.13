// SPDX-License-Identifier: GPL-2.0-or-later
/*  Kernel module help for x86.
    Copyright (C) 2001 Rusty Russell.

*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/kasan.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/jump_label.h>
#include <linux/random.h>
#include <linux/memory.h>

#include <asm/text-patching.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/unwind.h>

#if 0
#define DEBUGP(fmt, ...)				\
	printk(KERN_DEBUG fmt, ##__VA_ARGS__)
#else
#define DEBUGP(fmt, ...)				\
do {							\
	if (0)						\
		printk(KERN_DEBUG fmt, ##__VA_ARGS__);	\
} while (0)
#endif

#ifdef CONFIG_RANDOMIZE_BASE
static unsigned long module_load_offset;

/* Mutex protects the module_load_offset. */
static DEFINE_MUTEX(module_kaslr_mutex);

static unsigned long int get_module_load_offset(void)
{
	if (kaslr_enabled()) {
		mutex_lock(&module_kaslr_mutex);
		/*
		 * Calculate the module_load_offset the first time this
		 * code is called. Once calculated it stays the same until
		 * reboot.
		 */
		if (module_load_offset == 0)
			module_load_offset =
				(get_random_int() % 1024 + 1) * PAGE_SIZE;
		mutex_unlock(&module_kaslr_mutex);
	}
	return module_load_offset;
}
#else
//static unsigned long int get_module_load_offset(void)
//{
//	return 0;
//}
#endif

void *module_alloc(unsigned long size)
{
	void *p;

	if (PAGE_ALIGN(size) > MODULES_LEN)
		return NULL;

	p = __vmalloc_node_range(size, MODULE_ALIGN,
				    MODULES_VADDR + get_module_load_offset(),
				    MODULES_END, GFP_KERNEL,
				    PAGE_KERNEL, 0, NUMA_NO_NODE,
				    __builtin_return_address(0));
	if (p && (kasan_module_alloc(p, size) < 0)) {
		vfree(p);
		return NULL;
	}

	return p;
}

#ifdef CONFIG_X86_32
int apply_relocate(Elf32_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	unsigned int i;
	Elf32_Rel *rel = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	uint32_t *location;

	DEBUGP("Applying relocate section %u to %u\n",
	       relsec, sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);

		switch (ELF32_R_TYPE(rel[i].r_info)) {
        //R_386_32	1	word32	S+A
		case R_386_32:
			/* We add the value into the location given */
			*location += sym->st_value;
			break;
        //R_386_PC32	2	word32	S+A-P
		case R_386_PC32:
			/* Add the value, subtract its position */
			*location += sym->st_value - (uint32_t)location;
			break;
		default:
			pr_err("%s: Unknown relocation: %u\n",
			       me->name, ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}
#else /*X86_64*/
/**
 *  rela 重定位
 *  关键的计算公式参考
 *  https://docs.oracle.com/cd/E19120-01/open.solaris/819-0690/6n33n7fct/index.html
 */
static int __apply_relocate_add(Elf64_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me,
		   void *(*write_func)(void *dest, const void *src, size_t len))
{
	unsigned int i;

    /**
     *  在内存中的地址
     */
	Elf64_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf64_Sym *sym;
	void *loc;
	u64 val;

	DEBUGP("Applying relocate section %u to %u\n",
	       relsec, sechdrs[relsec].sh_info);
    /**
     *  节头表中有多少 项，也就是 .rela 中有多少个
     *  遍历所有 rela，如下 readelf -r /usr/bin/ls
     *  Relocation section '.rela.dyn' at offset 0x1968 contains 206 entries:
     *    Offset          Info           Type           Sym. Value    Sym. Name + Addend
     *  000000022fc0  007200000006 R_X86_64_GLOB_DAT 0000000000000000 free@GLIBC_2.2.5 + 0
     *  000000022fc8  000900000006 R_X86_64_GLOB_DAT 0000000000000000 _ITM_deregisterTMClone + 0
     *  000000022fd0  003500000006 R_X86_64_GLOB_DAT 0000000000000000 __libc_start_main@GLIBC_2.2.5 + 0
     *  000000022fd8  004100000006 R_X86_64_GLOB_DAT 0000000000000000 __gmon_start__ + 0
     *  000000022fe0  008800000006 R_X86_64_GLOB_DAT 0000000000000000 malloc@GLIBC_2.2.5 + 0
     */
    for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
        /**
         *  位置,我们将要对这个地址处的代码做出修改
         *  sh_addr = 重定位表的起始地址
         *  r_offset = 该重定位项在重定位表中的偏移
         *  loc = 重定位符号的实际地址，也就是我们要修改的地址
         */
		/* This is where to make the change */
		loc = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;

		/* This is the symbol it is referring to.  Note that all
		 * undefined symbols have been resolved.
		 *  这个 rela 对应的 sym 结构
		 */
		sym = (Elf64_Sym *)sechdrs[symindex].sh_addr
			+ ELF64_R_SYM(rel[i].r_info);

		DEBUGP("type %d st_value %Lx r_addend %Lx loc %Lx\n",
		       (int)ELF64_R_TYPE(rel[i].r_info),
		       sym->st_value, rel[i].r_addend, (u64)loc);
        /**
         *  st_value + r_addend 是我们需要改为的数值
         *
         *  st_value:
         *      关联符号的值。该值可以是绝对值或地址，具体取决于上下文。请参阅符号值。
         *      https://docs.oracle.com/cd/E19120-01/open.solaris/819-0690/6n33n7fcd/index.html
         *
         *      不同对象文件类型的符号表条目对st_value成员的解释略有不同。
         *      https://docs.oracle.com/cd/E19120-01/open.solaris/819-0690/chapter6-35166/index.html
         *
         *      1.在可重定位文件中，st_value保存节索引为SHN_COMMON的符号的对齐约束。
         *      2.在可重定位文件中，st_value保存已定义符号的节偏移量。
         *          st_value是从st_shndx标识部分开始的偏移量。
         *      3.在可执行文件和共享对象文件中，st_value拥有一个虚拟地址。
         *          为了使这些文件的符号对运行时链接器更有用，节偏移（文件解释）
         *          让位于与节号无关的虚拟地址（内存解释）。
         *
         *      这里是 ‘st_value拥有一个虚拟地址’???
         *
         *  r_addend:
         *      此成员指定一个常量加数，用于计算要存储到可重定位字段中的值。
         *
         *  例如： R_X86_64_64 对应 S + A， 那么 val = S + A
         */
		val = sym->st_value + rel[i].r_addend;

        /**
         *  这里就比较繁琐了，因为这里需要看下 gcc 手册，
         *  不同类型的重定位有不同的计算公式，参见
         *  https://docs.oracle.com/cd/E19120-01/open.solaris/819-0690/6n33n7fct/index.html
         */
		switch (ELF64_R_TYPE(rel[i].r_info)) {
        /**
         *
         */
		case R_X86_64_NONE:
			break;
        /**
         *  word64  S + A
         */
		case R_X86_64_64:
			if (*(u64 *)loc != 0)
				goto invalid_relocation;
			write_func(loc, &val, 8);
			break;
        /**
         *  word32  S + A
         */
		case R_X86_64_32:
			if (*(u32 *)loc != 0)
				goto invalid_relocation;
			write_func(loc, &val, 4);
			if (val != *(u32 *)loc)
				goto overflow;
			break;
        /**
         *  word32  S + A
         */
		case R_X86_64_32S:
			if (*(s32 *)loc != 0)
				goto invalid_relocation;
			write_func(loc, &val, 4);
			if ((s64)val != *(s32 *)loc)
				goto overflow;
			break;
        /**
         *  word32  S + A - P
         */
		case R_X86_64_PC32:
        /**
         *  word32  L + A - P
         */
		case R_X86_64_PLT32:
			if (*(u32 *)loc != 0)
				goto invalid_relocation;
			val -= (u64)loc;
			write_func(loc, &val, 4);
#if 0
			if ((s64)val != *(s32 *)loc)
				goto overflow;
#endif
			break;
        /**
         *  word64  S + A - P
         */
		case R_X86_64_PC64:
			if (*(u64 *)loc != 0)
				goto invalid_relocation;
			val -= (u64)loc;
			write_func(loc, &val, 8);
			break;
        /**
         *
         */
		default:
			pr_err("%s: Unknown rela relocation: %llu\n",
			       me->name, ELF64_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;

invalid_relocation:
	pr_err("x86/modules: Skipping invalid relocation target, existing value is nonzero for type %d, loc %p, val %Lx\n",
	       (int)ELF64_R_TYPE(rel[i].r_info), loc, val);
	return -ENOEXEC;

overflow:
	pr_err("overflow in relocation type %d val %Lx\n",
	       (int)ELF64_R_TYPE(rel[i].r_info), val);
	pr_err("`%s' likely not compiled with -mcmodel=kernel\n",
	       me->name);
	return -ENOEXEC;
}

/**
 *  rela 重定位
 *  重定位公式参考
 *  https://docs.oracle.com/cd/E19120-01/open.solaris/819-0690/6n33n7fct/index.html
 */
int apply_relocate_add(Elf64_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	int ret;
	bool early = me->state == MODULE_STATE_UNFORMED;

    /**
     *  我感觉 libcare 就是在这里借鉴的代码啊
     *  默认值为 memcpy
     */
	void *(*write_rtoax_fn)(void *, const void *, size_t) = memcpy;

	if (!early) {
		write_rtoax_fn = text_poke;
		mutex_lock(&text_mutex);
	}
    /**
     *  正经八百的重定位？
     *  重定位公式参考
     *  https://docs.oracle.com/cd/E19120-01/open.solaris/819-0690/6n33n7fct/index.html
     */
	ret = __apply_relocate_add(sechdrs, strtab, symindex, relsec, me,
				   write_rtoax_fn);

	/**
	 *
	 */
	if (!early) {
		text_poke_sync();
		mutex_unlock(&text_mutex);
	}

	return ret;
}

#endif

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	const Elf_Shdr *s, *text = NULL, *alt = NULL, *locks = NULL,
		*para = NULL, *orc = NULL, *orc_ip = NULL;
	char *secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;

	for (s = sechdrs; s < sechdrs + hdr->e_shnum; s++) {
		if (!strcmp(".text", secstrings + s->sh_name))
			text = s;
		if (!strcmp(".altinstructions", secstrings + s->sh_name))
			alt = s;
		if (!strcmp(".smp_locks", secstrings + s->sh_name))
			locks = s;
		if (!strcmp(".parainstructions", secstrings + s->sh_name))
			para = s;
		if (!strcmp(".orc_unwind", secstrings + s->sh_name))
			orc = s;
		if (!strcmp(".orc_unwind_ip", secstrings + s->sh_name))
			orc_ip = s;
	}

	if (alt) {
		/* patch .altinstructions */
		void *aseg = (void *)alt->sh_addr;
		apply_alternatives(aseg, aseg + alt->sh_size);
	}
	if (locks && text) {
		void *lseg = (void *)locks->sh_addr;
		void *tseg = (void *)text->sh_addr;
		alternatives_smp_module_add(me, me->name,
					    lseg, lseg + locks->sh_size,
					    tseg, tseg + text->sh_size);
	}

	if (para) {
		void *pseg = (void *)para->sh_addr;
		apply_paravirt(pseg, pseg + para->sh_size);
	}

	/* make jump label nops */
	jump_label_apply_nops(me);

	if (orc && orc_ip)
		unwind_module_init(me, (void *)orc_ip->sh_addr, orc_ip->sh_size,
				   (void *)orc->sh_addr, orc->sh_size);

	return 0;
}

void module_arch_cleanup(struct module *mod)
{
	alternatives_smp_module_del(mod);
}
