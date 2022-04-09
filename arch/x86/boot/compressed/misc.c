// SPDX-License-Identifier: GPL-2.0
/*
 * misc.c
 *
 * This is a collection of several routines used to extract the kernel
 * which includes KASLR relocation, decompression, ELF parsing, and
 * relocation processing. Additionally included are the screen and serial
 * output functions and related debugging support functions.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 * puts by Nick Holloway 1993, better puts by Martin Mares 1995
 * High loaded stuff by Hans Lermen & Werner Almesberger, Feb. 1996
 */

#include "misc.h"
#include "error.h"
#include "pgtable.h"
#include "../string.h"
#include "../voffset.h"
#include <asm/bootparam_utils.h>

/*
 * WARNING!!
 * This code is compiled with -fPIC and it is relocated dynamically at
 * run time, but no relocation processing is performed. This means that
 * it is not safe to place pointers in static structures.
 */

/* Macros used by the included decompressor code below. */
#define STATIC		static

/*
 * Provide definitions of memzero and memmove as some of the decompressors will
 * try to define their own functions if these are not defined as macros.
 */
#define memzero(s, n)	memset((s), 0, (n))
#define memmove		memmove

/* Functions used by the included decompressor code below. */
void *memmove(void *dest, const void *src, size_t n);

/*
 * This is set up by the setup-routine at boot-time
 */
struct boot_parameters *boot_params;

memptr free_mem_ptr;
memptr free_mem_end_ptr;

static char *vidmem;
static int vidport;
static int lines, cols;

#ifdef CONFIG_KERNEL_GZIP
#include "../../../../lib/decompress_inflate.c"
#endif

#ifdef CONFIG_KERNEL_BZIP2
#include "../../../../lib/decompress_bunzip2.c"
#endif

#ifdef CONFIG_KERNEL_LZMA
#include "../../../../lib/decompress_unlzma.c"
#endif

#ifdef CONFIG_KERNEL_XZ
#include "../../../../lib/decompress_unxz.c"
#endif

#ifdef CONFIG_KERNEL_LZO
#include "../../../../lib/decompress_unlzo.c"
#endif

#ifdef CONFIG_KERNEL_LZ4
#include "../../../../lib/decompress_unlz4.c"
#endif

#ifdef CONFIG_KERNEL_ZSTD
#include "../../../../lib/decompress_unzstd.c"
#endif
/*
 * NOTE: When adding a new decompressor, please update the analysis in
 * ../header.S.
 */

static void scroll(void)
{
	int i;

	memmove(vidmem, vidmem + cols * 2, (lines - 1) * cols * 2);
	for (i = (lines - 1) * cols * 2; i < lines * cols * 2; i += 2)
		vidmem[i] = ' ';
}

#define XMTRDY          0x20

#define TXR             0       /*  Transmit register (WRITE) */
#define LSR             5       /*  Line Status               */
static void serial_putchar(int ch)
{
	unsigned timeout = 0xffff;

	while ((inb(early_serial_base + LSR) & XMTRDY) == 0 && --timeout)
		cpu_relax();

	outb(ch, early_serial_base + TXR);
}

void __putstr(const char *s)
{
	int x, y, pos;
	char c;

	if (early_serial_base) {
		const char *str = s;
		while (*str) {
			if (*str == '\n')
				serial_putchar('\r');
			serial_putchar(*str++);
		}
	}

	if (lines == 0 || cols == 0)
		return;

	x = boot_params->screen_info.orig_x;
	y = boot_params->screen_info.orig_y;

	while ((c = *s++) != '\0') {
		if (c == '\n') {
			x = 0;
			if (++y >= lines) {
				scroll();
				y--;
			}
		} else {
			vidmem[(x + cols * y) * 2] = c;
			if (++x >= cols) {
				x = 0;
				if (++y >= lines) {
					scroll();
					y--;
				}
			}
		}
	}

	boot_params->screen_info.orig_x = x;
	boot_params->screen_info.orig_y = y;

	pos = (x + cols * y) * 2;	/* Update cursor position */
	outb(14, vidport);
	outb(0xff & (pos >> 9), vidport+1);
	outb(15, vidport);
	outb(0xff & (pos >> 1), vidport+1);
}

void __puthex(unsigned long value)
{
	char alpha[2] = "0";
	int bits;

	for (bits = sizeof(value) * 8 - 4; bits >= 0; bits -= 4) {
		unsigned long digit = (value >> bits) & 0xf;

		if (digit < 0xA)
			alpha[0] = '0' + digit;
		else
			alpha[0] = 'a' + (digit - 0xA);

		__putstr(alpha);
	}
}

#if CONFIG_X86_NEED_RELOCS
/**
 *  调整内核映像中的地址
 *
 *  此函数减去值 LOAD_PHYSICAL_ADDR ,从内核的基本加载地址的值中可以得出内核链接到加载的位置
 *  与内核实际加载的位置之间的差。
 *  此后，由于我们知道内核的实际加载地址，链接运行的地址以及位于内核映像末尾的重定位表，
 *  因此可以对内核进行重定位。
 */
static void handle_relocations(void *output, unsigned long output_len,
			       unsigned long virt_addr)
{
	int *reloc;
	unsigned long delta, map, ptr;
	unsigned long min_addr = (unsigned long)output;

    /**
     *
     */
	unsigned long max_addr = min_addr + (VO___bss_start - VO__text);

	/*
	 * Calculate the delta between where vmlinux was linked to load
	 * and where it was actually loaded.
	 *
	 * 获得内核链接后要加载的地址和实际加载地址的差值
	 */
	delta = min_addr - LOAD_PHYSICAL_ADDR/* 1000000 */;

	/*
	 * The kernel contains a table of relocation addresses. Those
	 * addresses have the final load address of the kernel in virtual
	 * memory. We are currently working in the self map. So we need to
	 * create an adjustment for kernel memory addresses to the self map.
	 * This will involve subtracting out the base address of the kernel.
	 */
	map = delta - __START_KERNEL_map/* 0xffffffff80000000 */;

	/*
	 * 32-bit always performs relocations. 64-bit relocations are only
	 * needed if KASLR has chosen a different starting address offset
	 * from __START_KERNEL_map.
	 *
	 * LOAD_PHYSICAL_ADDR=1000000, 获得内核链接后要加载的地址和实际加载地址的差值
	 */
	if (IS_ENABLED(CONFIG_X86_64))
		delta = virt_addr - LOAD_PHYSICAL_ADDR;

	if (!delta) {
		debug_putstr("No relocation needed... ");
		return;
	}
	debug_putstr("Performing relocations... ");

	/*
	 * Process relocations: 32 bit relocations first then 64 bit after.
	 * Three sets of binary relocations are added to the end of the kernel
	 * before compression. Each relocation table entry is the kernel
	 * address of the location which needs to be updated stored as a
	 * 32-bit value which is sign extended to 64 bits.
	 *
	 * Format is:
	 *
	 * kernel bits...
	 * 0 - zero terminator for 64 bit relocations
	 * 64 bit relocation repeated
	 * 0 - zero terminator for inverse 32 bit relocations
	 * 32 bit inverse relocation repeated
	 * 0 - zero terminator for 32 bit relocations
	 * 32 bit relocation repeated
	 *
	 * So we work backwards from the end of the decompressed image.
	 */
	for (reloc = output + output_len - sizeof(*reloc); *reloc; reloc--) {
		long extended = *reloc;
		extended += map;

		ptr = (unsigned long)extended;
		if (ptr < min_addr || ptr > max_addr)
			error("32-bit relocation outside of kernel!\n");

		*(uint32_t *)ptr += delta;
	}
#ifdef CONFIG_X86_64
	while (*--reloc) {
		long extended = *reloc;
		extended += map;

		ptr = (unsigned long)extended;
		if (ptr < min_addr || ptr > max_addr)
			error("inverse 32-bit relocation outside of kernel!\n");

		*(int32_t *)ptr -= delta;
	}
	for (reloc--; *reloc; reloc--) {
		long extended = *reloc;
		extended += map;

		ptr = (unsigned long)extended;
		if (ptr < min_addr || ptr > max_addr)
			error("64-bit relocation outside of kernel!\n");

		*(uint64_t *)ptr += delta;
	}
#endif
}
#else
/*  */
#endif

/**
 *  parse_elf 功能的主要目标是将可加载的段移动到正确的地址
 *
 *
 * $ readelf -l vmlinux(内核文件)
 *
 * Elf 文件类型为 EXEC (可执行文件)
 * 入口点 0x1000000
 * 共有 5 个程序头，开始于偏移量64
 *
 * 程序头：
 *   Type           Offset             VirtAddr           PhysAddr
 *                  FileSiz            MemSiz              Flags  Align
 *   LOAD           0x0000000000200000 0xffffffff81000000 0x0000000001000000
 *                  0x0000000001b93dc0 0x0000000001b93dc0  R E    200000
 *   LOAD           0x0000000001e00000 0xffffffff82c00000 0x0000000002c00000
 *                  0x0000000000fdc000 0x0000000000fdc000  RW     200000
 *   LOAD           0x0000000002e00000 0x0000000000000000 0x0000000003bdc000
 *                  0x0000000000036000 0x0000000000036000  RW     200000
 *   LOAD           0x0000000003012000 0xffffffff83c12000 0x0000000003c12000
 *                  0x0000000001dee000 0x0000000001dee000  RWE    200000
 *   NOTE           0x0000000001d93bec 0xffffffff82b93bec 0x0000000002b93bec
 *                  0x00000000000001d4 0x00000000000001d4         4
 *
 *  Section to Segment mapping:
 *   段节...
 *    00     .text .rodata .pci_fixup .tracedata __ksymtab __ksymtab_gpl __kcrctab __kcrctab_gpl __ksymtab_strings __param __modver __ex_table .notes  *    01     .data __bug_table .orc_unwind_ip .orc_unwind .orc_lookup .vvar
 *    02     .data..percpu
 *    03     .init.text .altinstr_aux .init.data .x86_cpu_dev.init .parainstructions .altinstructions .altinstr_replacement .iommu_table .apicdrivers .exit.text .exit.data .smp_locks .data_nosave .bss .brk .init.scratch  *    04     .notes
 *
 *  parse_elf函数的目标是将这些段加载到output(从choose_random_location函数获得的地址)中.
 *
 *  parse_elf功能的主要目标是将可加载的段移动到正确的地址. 该方法负责将未雅座的vmlinux.bin
 *  可执行文件中的 load segments 移动到正确的位置，因为这是内核的入口初始化代码，
 *  还没有进程的概念，所以要将 load segment 加载到指定为u里地址处并跳转到那里执行。
 *  这里要区分链接视图和执行视图的区别。
 */
static void parse_elf(void *output)
{
#ifdef CONFIG_X86_64
	Elf64_Ehdr ehdr;
	Elf64_Phdr *phdrs, *phdr;
#else
	Elf32_Ehdr ehdr;
	Elf32_Phdr *phdrs, *phdr;
#endif
	void *dest;
	int i;

	memcpy(&ehdr, output, sizeof(ehdr));

    /**
     *  首先检查ELF签名
     */
	if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
	   ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
	   ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
	   ehdr.e_ident[EI_MAG3] != ELFMAG3) {
	    /**
	     *  如果ELF标头无效，则会显示一条错误消息并暂停
	     */
		error("Kernel is not a valid ELF file");
		return;
	}

	debug_putstr("Parsing ELF... ");

	phdrs = malloc(sizeof(*phdrs) * ehdr.e_phnum);
	if (!phdrs)
		error("Failed to allocate space for phdrs");

	memcpy(phdrs, output + ehdr.e_phoff, sizeof(*phdrs) * ehdr.e_phnum);

    /**
     *  我们将遍历给定ELF文件中的所有程序头
     */
	for (i = 0; i < ehdr.e_phnum; i++) {
		phdr = &phdrs[i];

		switch (phdr->p_type) {
		case PT_LOAD:
#ifdef CONFIG_X86_64
			if ((phdr->p_align % 0x200000) != 0)
				error("Alignment of LOAD segment isn't multiple of 2MB");
#endif
#ifdef CONFIG_RELOCATABLE
            /**
             *  假设 随机选取的物理加载地址 output=32M，virt_addr=16M
             *
             *  已知 LOAD_PHYSICAL_ADDR=default_value=16M
             *  所以
             *  第一个 load segment 将被重新加载到
             *      output + p_paddr - 16M = 32M
             *  第二个 load segment 将被重新加载到
             *      ????
             */
			dest = output;
			dest += (phdr->p_paddr - LOAD_PHYSICAL_ADDR);
#else
			dest = (void *)(phdr->p_paddr);
#endif
            /**
             * 将所有具有正确2 MB对齐地址的可加载段复制到输出缓冲区中
             */
			memmove(dest, output + phdr->p_offset, phdr->p_filesz);
			break;
		default: /* Ignore other PT_* */ break;
		}
	}

    /**
     *  从这一刻起，所有可加载的段都位于正确的位置
     */

	free(phdrs);
}

/*
 * The compressed kernel image (ZO), has been moved so that its position
 * is against the end of the buffer used to hold the uncompressed kernel
 * image (VO) and the execution environment (.bss, .brk), which makes sure
 * there is room to do the in-place decompression. (See header.S for the
 * calculations.)
 *
 *                             |-----compressed kernel image------|
 *                             V                                  V
 * 0                       extract_offset                      +INIT_SIZE
 * |-----------|---------------|-------------------------|--------|
 *             |               |                         |        |
 *           VO__text      startup_32 of ZO          VO__end    ZO__end
 *             ^                                         ^
 *             |-------uncompressed kernel image---------|
 *
 *
 * 在relocated中会调用extract_kernel将compressed kernel解压到16M地址开始处，
 * 随后跳转到arch/x86/kernel/head_64.S的开始处执行内核早期的内核初始化操作，
 * 其中包括物理加载地址的relocate操作
 */
asmlinkage __visible void *extract_kernel(void *rmode, memptr heap,
				  unsigned char *input_data,
				  unsigned long input_len,
				  unsigned char *output,
				  unsigned long output_len)
{
	const unsigned long kernel_total_size = VO__end - VO__text;

    /**
     *  默认都是 16M
     */
	unsigned long virt_addr = LOAD_PHYSICAL_ADDR;
	unsigned long needed_size;

	/* Retain x86 boot parameters pointer passed from startup_32/64. */
	boot_params = rmode;

	/* Clear flags intended for solely in-kernel use. */
	boot_params->hdr.loadflags &= ~KASLR_FLAG;

    /**
     *  再次执行此操作，因为我们不知道是否以实模式启动，或者是否使用了引导加载程序，
     *  32或者64-bit引导加载程序是否使用或引导协议
     */
	sanitize_boot_params(boot_params);

	if (boot_params->screen_info.orig_video_mode == 7) {
		vidmem = (char *) 0xb0000;
		vidport = 0x3b4;
	} else {
		vidmem = (char *) 0xb8000;
		vidport = 0x3d4;
	}

	lines = boot_params->screen_info.orig_video_lines;
	cols = boot_params->screen_info.orig_video_cols;

    /**
     *  再次执行此操作，因为我们不知道是否以实模式启动，或者是否使用了引导加载程序，
     *  32或者64-bit引导加载程序是否使用或引导协议
     */
	console_init();

	/*
	 * Save RSDP address for later use. Have this after console_init()
	 * so that early debugging output from the RSDP parsing code can be
	 * collected.
	 */
	boot_params->acpi_rsdp_addr = get_rsdp_addr();

	debug_putstr("early console in extract_kernel\n");

    /**
     *  将指针存储到空闲内存的开始和结尾
     *  保存空闲内存的起始和末尾地址
     *
     *  Heap: leaq    boot_heap(%rip), %rsi(arch/x86/boot/compressed/head_64.S)
     */
	free_mem_ptr     = heap;
	free_mem_end_ptr = heap + BOOT_HEAP_SIZE;

	/*
	 * The memory hole needed for the kernel is the larger of either
	 * the entire decompressed kernel plus relocation table, or the
	 * entire decompressed kernel plus .bss and .brk sections.
	 *
	 * On X86_64, the memory is mapped with PMD pages. Round the
	 * size up so that the full extent of PMD pages mapped is
	 * included in the check against the valid memory table
	 * entries. This ensures the full mapped area is usable RAM
	 * and doesn't include any reserved areas.
	 */
	needed_size = max(output_len, kernel_total_size);
#ifdef CONFIG_X86_64
	needed_size = ALIGN(needed_size, MIN_KERNEL_ALIGN);
#endif

	/* Report initial kernel position details. */
	debug_putaddr(input_data);
	debug_putaddr(input_len);
	debug_putaddr(output);
	debug_putaddr(output_len);
	debug_putaddr(kernel_total_size);
	debug_putaddr(needed_size);

#ifdef CONFIG_X86_64
	/* Report address of 32-bit trampoline */
	debug_putaddr(trampoline_32bit);
#endif

    /**
     *  选择一个存储位置来写入解压缩的内核
     *
     *  该函数 为 output 和 virt_addr 分别分配随机值 (2M 对齐)，
     *  他们的随机值可以相同，如果 kaslr=false 直接返回
     */
	choose_random_location((unsigned long)input_data, input_len,
				(unsigned long *)&output,
				needed_size,
				&virt_addr);

	/**
	 *  Validate memory location choices.
	 *  检查所获取的随机地址是否正确对齐
	 */
	if ((unsigned long)output & (MIN_KERNEL_ALIGN - 1))
		error("Destination physical address inappropriately aligned");
	if (virt_addr & (MIN_KERNEL_ALIGN - 1))
		error("Destination virtual address inappropriately aligned");
#ifdef CONFIG_X86_64
	if (heap > 0x3fffffffffffUL)
		error("Destination address too large");
	if (virt_addr + max(output_len, kernel_total_size) > KERNEL_IMAGE_SIZE)
		error("Destination virtual address is beyond the kernel mapping area");
#else
	if (heap > ((-__PAGE_OFFSET-(128<<20)-1) & 0x7fffffff))
		error("Destination address too large");
#endif
#ifndef CONFIG_RELOCATABLE
	if ((unsigned long)output != LOAD_PHYSICAL_ADDR)
		error("Destination address does not match LOAD_PHYSICAL_ADDR");
	if (virt_addr != LOAD_PHYSICAL_ADDR)
		error("Destination virtual address changed when not relocatable");
#endif

    /**
     *  开始解压内核
     */
	debug_putstr("\nDecompressing Linux... ");

    /**
     *  解压缩内核: 取决于内核编译过程采用哪种编译方法
     *
     *  将解压后的 内核 放在output 指定的物理地址处。
     */
	__decompress(input_data, input_len, NULL, NULL, output, output_len,
			NULL, error);

    /**
     *  将解压缩的内核映像移至其在内存中的正确位置, 因为解压缩是就地完成的，
     *  我们仍然需要将内核移到正确的地址. 内核映像是ELF可执行文件。
     *
     */
    /**
     *  parse_elf()
     *  功能的主要目标是将可加载的段移动到正确的地址. 该方法负责将未雅座的vmlinux.bin
     *  可执行文件中的 load segments 移动到正确的位置，因为这是内核的入口初始化代码，
     *  还没有进程的概念，所以要将 load segment 加载到指定为u里地址处并跳转到那里执行。
     *  这里要区分链接视图和执行视图的区别。
     */
	parse_elf(output);

    /**
     *  handle_relocations()
     *  调整内核映像中的地址
     *  该方法根据 vmlinux.relocs 文件中记录的需要被更新的虚拟的位置和随机分配的
     *  virt_addr ，去更新 kernel 中的绝对虚拟地址引用。
     */
	handle_relocations(output, output_len, virt_addr);

	debug_putstr("done.\nBooting the kernel.\n");

	/*
	 * Flush GHCB from cache and map it encrypted again when running as
	 * SEV-ES guest.
	 */
	sev_es_shutdown_ghcb();

	return output;
}

void fortify_panic(const char *name)
{
	error("detected buffer overflow");
}
