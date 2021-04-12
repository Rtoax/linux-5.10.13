/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	header.S
 *
 *	Copyright (C) 1991, 1992 Linus Torvalds
 *
 *	Based on bootsect.S and setup.S
 *	modified by more people than can be counted
 *
 *	Rewritten as a common file by H. Peter Anvin (Apr 2007)
 *
 * BIG FAT NOTE: We're in real mode using 64k segments.  Therefore segment
 * addresses must be multiplied by 16 to obtain their respective linear
 * addresses. To avoid confusion, linear addresses are written using leading
 * hex while segment addresses are written as segment:offset.
 *
 */
#include <linux/pe.h>
#include <asm/segment.h>
#include <asm/boot.h>
#include <asm/page_types.h>
#include <asm/setup.h>
#include <asm/bootparam.h>
#include "boot.h"
#include "voffset.h"
#include "zoffset.h"

BOOTSEG		= 0x07C0		/* original address of boot-sector */
SYSSEG		= 0x1000		/* historical load address >> 4 */

#ifndef SVGA_MODE
#define SVGA_MODE ASK_VGA
#endif

#ifndef ROOT_RDONLY
#define ROOT_RDONLY 1
#endif

	.code16
	.section ".bstext", "ax"

	.global bootsect_start
bootsect_start:
#ifdef CONFIG_EFI_STUB
	//# "MZ", MS-DOS header
	.word	MZ_MAGIC    /* 为了能够作为 bootloader 来使用, 
	                    `header.S` 开始处定义了 [MZ] 
	                    [MZ](https://en.wikipedia.org/wiki/DOS_MZ_executable) 魔术数字 */
#endif

	//# Normalize the start address
	ljmp	$BOOTSEG, $start2

start2:
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	xorw	%sp, %sp
	sti
	cld

	movw	$bugger_off_msg, %si

msg_loop:
	lodsb
	andb	%al, %al
	jz	bs_die
	movb	$0xe, %ah
	movw	$7, %bx
	int	$0x10
	jmp	msg_loop

bs_die:
	//# Allow the user to press a key, then reboot
	xorw	%ax, %ax
	int	$0x16
	int	$0x19

	//# int 0x19 should never return.  In case it does anyway,
	//# invoke the BIOS reset code...
	ljmp	$0xf000,$0xfff0

#ifdef CONFIG_EFI_STUB
	.org	0x3c
	//#
	//# Offset to the PE header.
	//#
	.long	pe_header
#endif /* CONFIG_EFI_STUB */

	.section ".bsdata", "a"
bugger_off_msg:
	.ascii	"Use a boot loader.\r\n"
	.ascii	"\n"
	.ascii	"Remove disk and press any key to reboot...\r\n"
	.byte	0

#ifdef CONFIG_EFI_STUB
pe_header:  /* Portable(轻便的) Executable 头，在 PE 头中定义了输出的字符串
             [PE](https://en.wikipedia.org/wiki/Portable_Executable) */
	.long	PE_MAGIC

coff_header:
#ifdef CONFIG_X86_32
	.set	image_file_add_flags, IMAGE_FILE_32BIT_MACHINE
	.set	pe_opt_magic, PE_OPT_MAGIC_PE32
	.word	IMAGE_FILE_MACHINE_I386
#else
	.set	image_file_add_flags, 0
	.set	pe_opt_magic, PE_OPT_MAGIC_PE32PLUS
	.word	IMAGE_FILE_MACHINE_AMD64
#endif
	.word	section_count			//# nr_sections
	.long	0 				//# TimeDateStamp
	.long	0				//# PointerToSymbolTable
	.long	1				//# NumberOfSymbols
	.word	section_table - optional_header	//# SizeOfOptionalHeader
	.word	IMAGE_FILE_EXECUTABLE_IMAGE	| \
		image_file_add_flags		| \
		IMAGE_FILE_DEBUG_STRIPPED	| \
		IMAGE_FILE_LINE_NUMS_STRIPPED	//# Characteristics

optional_header:
	.word	pe_opt_magic
	.byte	0x02				//# MajorLinkerVersion
	.byte	0x14				//# MinorLinkerVersion

	//# Filled in by build.c
	.long	0				//# SizeOfCode

	.long	0				//# SizeOfInitializedData
	.long	0				//# SizeOfUninitializedData

	//# Filled in by build.c
	.long	0x0000				//# AddressOfEntryPoint   /*  */

	.long	0x0200				//# BaseOfCode
#ifdef CONFIG_X86_32
	.long	0				# data
#endif

extra_header_fields:
	//# PE specification requires ImageBase to be 64k aligned
	.set	image_base, (LOAD_PHYSICAL_ADDR + 0xffff) & ~0xffff
#ifdef CONFIG_X86_32
	.long	image_base			//# ImageBase
#else
	.quad	image_base			//# ImageBase
#endif
	.long	0x20				//# SectionAlignment
	.long	0x20				//# FileAlignment
	.word	0				//# MajorOperatingSystemVersion
	.word	0				//# MinorOperatingSystemVersion
	.word	LINUX_EFISTUB_MAJOR_VERSION	//# MajorImageVersion
	.word	LINUX_EFISTUB_MINOR_VERSION	//# MinorImageVersion
	.word	0				//# MajorSubsystemVersion
	.word	0				//# MinorSubsystemVersion
	.long	0				//# Win32VersionValue

	//#
	//# The size of the bzImage is written in tools/build.c
	//#
	.long	0				//# SizeOfImage

	.long	0x200				//# SizeOfHeaders
	.long	0				//# CheckSum
	.word	IMAGE_SUBSYSTEM_EFI_APPLICATION	//# Subsystem (EFI application)
	.word	0				//# DllCharacteristics
#ifdef CONFIG_X86_32
	.long	0				//# SizeOfStackReserve
	.long	0				//# SizeOfStackCommit
	.long	0				//# SizeOfHeapReserve
	.long	0				//# SizeOfHeapCommit
#else
	.quad	0				//# SizeOfStackReserve
	.quad	0				//# SizeOfStackCommit
	.quad	0				//# SizeOfHeapReserve
	.quad	0				//# SizeOfHeapCommit
#endif
	.long	0				//# LoaderFlags
	.long	(section_table - .) / 8		# NumberOfRvaAndSizes

	.quad	0				//# ExportTable
	.quad	0				//# ImportTable
	.quad	0				//# ResourceTable
	.quad	0				//# ExceptionTable
	.quad	0				//# CertificationTable
	.quad	0				//# BaseRelocationTable

	//# Section table
section_table:
	//#
	//# The offset & size fields are filled in by build.c.
	//#
	.ascii	".setup"
	.byte	0
	.byte	0
	.long	0
	.long	0x0				//# startup_{32,64}
	.long	0				//# Size of initialized data
						//# on disk
	.long	0x0				//# startup_{32,64}
	.long	0				//# PointerToRelocations
	.long	0				//# PointerToLineNumbers
	.word	0				//# NumberOfRelocations
	.word	0				//# NumberOfLineNumbers
	.long	IMAGE_SCN_CNT_CODE		| \
		IMAGE_SCN_MEM_READ		| \
		IMAGE_SCN_MEM_EXECUTE		| \
		IMAGE_SCN_ALIGN_16BYTES		//# Characteristics

	//#
	//# The EFI application loader requires a relocation section
	//# because EFI applications must be relocatable. The .reloc
	//# offset & size fields are filled in by build.c.
	//#
	.ascii	".reloc"
	.byte	0
	.byte	0
	.long	0
	.long	0
	.long	0				//# SizeOfRawData
	.long	0				//# PointerToRawData
	.long	0				//# PointerToRelocations
	.long	0				//# PointerToLineNumbers
	.word	0				//# NumberOfRelocations
	.word	0				//# NumberOfLineNumbers
	.long	IMAGE_SCN_CNT_INITIALIZED_DATA	| \
		IMAGE_SCN_MEM_READ		| \
		IMAGE_SCN_MEM_DISCARDABLE	| \
		IMAGE_SCN_ALIGN_1BYTES		//# Characteristics

#ifdef CONFIG_EFI_MIXED
	//#
	//# The offset & size fields are filled in by build.c.
	//#
	.asciz	".compat"
	.long	0
	.long	0x0
	.long	0				//# Size of initialized data
						//# on disk
	.long	0x0
	.long	0				//# PointerToRelocations
	.long	0				//# PointerToLineNumbers
	.word	0				//# NumberOfRelocations
	.word	0				//# NumberOfLineNumbers
	.long	IMAGE_SCN_CNT_INITIALIZED_DATA	| \
		IMAGE_SCN_MEM_READ		| \
		IMAGE_SCN_MEM_DISCARDABLE	| \
		IMAGE_SCN_ALIGN_1BYTES		//# Characteristics
#endif

	//#
	//# The offset & size fields are filled in by build.c.
	//#
	.ascii	".text"
	.byte	0
	.byte	0
	.byte	0
	.long	0
	.long	0x0				//# startup_{32,64}
	.long	0				//# Size of initialized data
						//# on disk
	.long	0x0				# startup_{32,64}
	.long	0				# PointerToRelocations
	.long	0				# PointerToLineNumbers
	.word	0				# NumberOfRelocations
	.word	0				# NumberOfLineNumbers
	.long	IMAGE_SCN_CNT_CODE		| \
		IMAGE_SCN_MEM_READ		| \
		IMAGE_SCN_MEM_EXECUTE		| \
		IMAGE_SCN_ALIGN_16BYTES		# Characteristics

	.set	section_count, (. - section_table) / 40
#endif /* CONFIG_EFI_STUB */

	# Kernel attributes; used by setup.  This is part 1 of the
	# header, from the old boot sector.

	.section ".header", "a"
	.globl	sentinel
sentinel:	.byte 0xff, 0xff        /* Used to detect broken loaders */

//就像 kernel boot protocol 所描述的，
//引导程序必须填充 kernel setup header （位于 kernel setup code 偏移 `0x01f1` 处）
	.globl	hdr
hdr:
setup_sects:	.byte 0			/* Filled in by build.c */
root_flags:	.word ROOT_RDONLY
syssize:	.long 0			/* Filled in by build.c */
ram_size:	.word 0			/* Obsolete */
vid_mode:	.word SVGA_MODE
root_dev:	.word 0			/* Filled in by build.c */
boot_flag:	.word 0xAA55

	# offset 512, entry point

	.globl	_start
_start: /* 内核设置入口点， 开始了老哥 实际上 `_start` 开始之前的代码是 kenerl 自带的 bootloader*/
        //# Explicitly enter this as bytes, or the assembler
        //# tries to generate a 3-byte jump here, which causes
        //# everything else to push off to the wrong offset.
		.byte	0xeb		//# short (2-byte) jump /* jmp指令操作码（0xeb） */
		.byte	start_of_setup-1f   /* 内核设置入口点是 */
		                            /* 在Nf符号2f中指的是本地标签2:,这里的 1f 对应 1: */
1:

	//# Part 2 of the header, from the old setup.S

		.ascii	"HdrS"		# header signature
		.word	0x020f		# header version number (>= 0x0105)
					# or else old loadlin-1.5 will fail)
		.globl realmode_swtch
realmode_swtch:	.word	0, 0		# default_switch, SETUPSEG
start_sys_seg:	.word	SYSSEG		# obsolete and meaningless, but just
					# in case something decided to "use" it
		.word	kernel_version-512 # pointing to kernel version string
					# above section of header is compatible
					# with loadlin-1.5 (header v1.5). Dont
					# change it.

type_of_loader:	.byte	0		# 0 means ancient bootloader, newer
					# bootloaders know to change this.
					# See Documentation/x86/boot.rst for
					# assigned ids

# flags, unused bits must be zero (RFU) bit within loadflags
loadflags:
		.byte	LOADED_HIGH	# The kernel is to be loaded high

setup_move_size: .word  0x8000		# size to move, when setup is not
					# loaded at 0x90000. We will move setup
					# to 0x90000 then just before jumping
					# into the kernel. However, only the
					# loader knows how much data behind
					# us also needs to be loaded.

code32_start:				# here loaders can put a different
					# start address for 32-bit code.
		.long	0x100000	//# 0x100000 = default for big kernel

ramdisk_image:	.long	0		# address of loaded ramdisk image
					# Here the loader puts the 32-bit
					# address where it loaded the image.
					# This only will be read by the kernel.

ramdisk_size:	.long	0		# its size in bytes

bootsect_kludge:
		.long	0		# obsolete

heap_end_ptr:	.word	_end+STACK_SIZE-512
					# (Header version 0x0201 or later)
					# space from here (exclusive) down to
					# end of setup code can be used by setup
					# for local heap purposes.

ext_loader_ver:
		.byte	0		# Extended boot loader version
ext_loader_type:
		.byte	0		# Extended boot loader type

cmd_line_ptr:	.long	0		# (Header version 0x0202 or later)
					# If nonzero, a 32-bit pointer
					# to the kernel command line.
					# The command line should be
					# located between the start of
					# setup and the end of low
					# memory (0xa0000), or it may
					# get overwritten before it
					# gets read.  If this field is
					# used, there is no longer
					# anything magical about the
					# 0x90000 segment; the setup
					# can be located anywhere in
					# low memory 0x10000 or higher.

initrd_addr_max: .long 0x7fffffff
					# (Header version 0x0203 or later)
					# The highest safe address for
					# the contents of an initrd
					# The current kernel allows up to 4 GB,
					# but leave it at 2 GB to avoid
					# possible bootloader bugs.

kernel_alignment:  .long CONFIG_PHYSICAL_ALIGN	#physical addr alignment
						#required for protected mode
						#kernel
#ifdef CONFIG_RELOCATABLE
relocatable_kernel:    .byte 1
#else
relocatable_kernel:    .byte 0
#endif
min_alignment:		.byte MIN_KERNEL_ALIGN_LG2	# minimum alignment

xloadflags:
#ifdef CONFIG_X86_64
# define XLF0 XLF_KERNEL_64			/* 64-bit kernel */
#else
# define XLF0 0
#endif

#if defined(CONFIG_RELOCATABLE) && defined(CONFIG_X86_64)
   /* kernel/boot_param/ramdisk could be loaded above 4g */
# define XLF1 XLF_CAN_BE_LOADED_ABOVE_4G
#else
# define XLF1 0
#endif

#ifdef CONFIG_EFI_STUB
# ifdef CONFIG_EFI_MIXED
#  define XLF23 (XLF_EFI_HANDOVER_32|XLF_EFI_HANDOVER_64)
# else
#  ifdef CONFIG_X86_64
#   define XLF23 XLF_EFI_HANDOVER_64		/* 64-bit EFI handover ok */
#  else
#   define XLF23 XLF_EFI_HANDOVER_32		/* 32-bit EFI handover ok */
#  endif
# endif
#else
# define XLF23 0
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_EFI) && defined(CONFIG_KEXEC_CORE)
# define XLF4 XLF_EFI_KEXEC
#else
# define XLF4 0
#endif

#ifdef CONFIG_X86_64
#ifdef CONFIG_X86_5LEVEL
#define XLF56 (XLF_5LEVEL|XLF_5LEVEL_ENABLED)
#else
#define XLF56 XLF_5LEVEL
#endif
#else
#define XLF56 0
#endif

			.word XLF0 | XLF1 | XLF23 | XLF4 | XLF56

cmdline_size:   .long   COMMAND_LINE_SIZE-1     #length of the command line,
                                                #added with boot protocol
                                                #version 2.06

hardware_subarch:	.long 0			# subarchitecture, added with 2.07
						# default to 0 for normal x86 PC

hardware_subarch_data:	.quad 0

payload_offset:		.long ZO_input_data
payload_length:		.long ZO_z_input_len

setup_data:		.quad 0			# 64-bit physical pointer to
						# single linked list of
						# struct setup_data

pref_address:		.quad LOAD_PHYSICAL_ADDR	# preferred load addr

//#
//# Getting to provably safe in-place decompression is hard. Worst case
//# behaviours need to be analyzed. Here let us take the decompression of
//# a gzip-compressed kernel as example, to illustrate it:
//#
//# The file layout of gzip compressed kernel is:
//#
//#    magic[2]
//#    method[1]
//#    flags[1]
//#    timestamp[4]
//#    extraflags[1]
//#    os[1]
//#    compressed data blocks[N]
//#    crc[4] orig_len[4]
//#
//# ... resulting in +18 bytes overhead of uncompressed data.
//#
//# (For more information, please refer to RFC 1951 and RFC 1952.)
//#
//# Files divided into blocks
//# 1 bit (last block flag)
//# 2 bits (block type)
//#
//# 1 block occurs every 32K -1 bytes or when there 50% compression
//# has been achieved. The smallest block type encoding is always used.
//#
//# stored:
//#    32 bits length in bytes.
//#
//# fixed:
//#    magic fixed tree.
//#    symbols.
//#
//# dynamic:
//#    dynamic tree encoding.
//#    symbols.
//#
//#
//# The buffer for decompression in place is the length of the uncompressed
//# data, plus a small amount extra to keep the algorithm safe. The
//# compressed data is placed at the end of the buffer.  The output pointer
//# is placed at the start of the buffer and the input pointer is placed
//# where the compressed data starts. Problems will occur when the output
//# pointer overruns the input pointer.
//#
//# The output pointer can only overrun the input pointer if the input
//# pointer is moving faster than the output pointer.  A condition only
//# triggered by data whose compressed form is larger than the uncompressed
//# form.
//#
//# The worst case at the block level is a growth of the compressed data
//# of 5 bytes per 32767 bytes.
//#
//# The worst case internal to a compressed block is very hard to figure.
//# The worst case can at least be bounded by having one bit that represents
//# 32764 bytes and then all of the rest of the bytes representing the very
//# very last byte.
//#
//# All of which is enough to compute an amount of extra data that is required
//# to be safe.  To avoid problems at the block level allocating 5 extra bytes
//# per 32767 bytes of data is sufficient.  To avoid problems internal to a
//# block adding an extra 32767 bytes (the worst case uncompressed block size)
//# is sufficient, to ensure that in the worst case the decompressed data for
//# block will stop the byte before the compressed data for a block begins.
//# To avoid problems with the compressed data's meta information an extra 18
//# bytes are needed.  Leading to the formula:
//#
//# extra_bytes = (uncompressed_size >> 12) + 32768 + 18
//#
//# Adding 8 bytes per 32K is a bit excessive but much easier to calculate.
//# Adding 32768 instead of 32767 just makes for round numbers.
//#
//# Above analysis is for decompressing gzip compressed kernel only. Up to
//# now 6 different decompressor are supported all together. And among them
//# xz stores data in chunks and has maximum chunk of 64K. Hence safety
//# margin should be updated to cover all decompressors so that we don't
//# need to deal with each of them separately. Please check
//# the description in lib/decompressor_xxx.c for specific information.
//#
//# extra_bytes = (uncompressed_size >> 12) + 65536 + 128
//#
//# LZ4 is even worse: data that cannot be further compressed grows by 0.4%,
//# or one byte per 256 bytes. OTOH, we can safely get rid of the +128 as
//# the size-dependent part now grows so fast.
//#
//# extra_bytes = (uncompressed_size >> 8) + 65536
//#
//# ZSTD compressed data grows by at most 3 bytes per 128K, and only has a 22
//# byte fixed overhead but has a maximum block size of 128K, so it needs a
//# larger margin.
//#
//# extra_bytes = (uncompressed_size >> 8) + 131072

#define ZO_z_extra_bytes	((ZO_z_output_len >> 8) + 131072)
#if ZO_z_output_len > ZO_z_input_len
# define ZO_z_extract_offset	(ZO_z_output_len + ZO_z_extra_bytes - \
				 ZO_z_input_len)
#else
# define ZO_z_extract_offset	ZO_z_extra_bytes
#endif

/*
 * The extract_offset has to be bigger than ZO head section. Otherwise when
 * the head code is running to move ZO to the end of the buffer, it will
 * overwrite the head code itself.
 */
#if (ZO__ehead - ZO_startup_32) > ZO_z_extract_offset
# define ZO_z_min_extract_offset ((ZO__ehead - ZO_startup_32 + 4095) & ~4095)
#else
# define ZO_z_min_extract_offset ((ZO_z_extract_offset + 4095) & ~4095)
#endif

#define ZO_INIT_SIZE	(ZO__end - ZO_startup_32 + ZO_z_min_extract_offset)

#define VO_INIT_SIZE	(VO__end - VO__text)
#if ZO_INIT_SIZE > VO_INIT_SIZE
# define INIT_SIZE ZO_INIT_SIZE
#else
# define INIT_SIZE VO_INIT_SIZE
#endif

init_size:		.long INIT_SIZE		# kernel initialization size
handover_offset:	.long 0			# Filled in by build.c
kernel_info_offset:	.long 0			# Filled in by build.c

//# End of setup header #####################################################
//参见：https://0xax.gitbooks.io/linux-insides/content/Booting/linux-bootstrap-1.html
//
//从 `start_of_setup` 标号开始的代码需要完成下面这些事情：
//
//* 将所有段寄存器的值设置成一样的内容
//* 设置堆栈
//* 设置 [bss](https://en.wikipedia.org/wiki/.bss) （静态变量区）
//* 跳转到 [main.c](http://lxr.free-electrons.com/source/arch/x86/boot/main.c?v=3.18) 开始执行代码

	.section ".entrytext", "ax"
start_of_setup:
    //# Force %es = %ds
    /* 内核确保ds和es段寄存器指向相同的地址 */
	movw	%ds, %ax    /* ax = ds */
	movw	%ax, %es    /* es = ax */
	cld /* 使用cld指令清除方向标志 */

    //# Apparently some ancient versions of LILO invoked the kernel with %ss != %ds,
    //# which happened to work by accident for the old code.  Recalculate the stack
    //# pointer if %ss is invalid.  Otherwise leave it alone, LOADLIN sets up the
    //# stack behind its own code, so we can not blindly put it directly past the heap.

    //几乎所有的安装代码都用于在实模式下准备C语言环境。接下来的步骤是检查ss寄存器的值，并建立正确的堆栈
	movw	%ss, %dx
	cmpw	%ax, %dx	//# %ds == %ss?
	movw	%sp, %dx
	je	2f		//# -> assume %sp is reasonably set
    //当进入这段代码的时候， `ss` 寄存器的值可能是一下三种情况之一：
    //
    //* `ss` 寄存器的值是 0x10000 ( 和其他除了 `cs` 寄存器之外的所有寄存器的一样）
    //* `ss` 寄存器的值不是 0x10000，但是 `CAN_USE_HEAP` 标志被设置了
    //* `ss` 寄存器的值不是 0x10000，同时 `CAN_USE_HEAP` 标志没有被设置
        
    //`ss` != `ds`的情况
    //	# Invalid %ss, make up a new stack
	movw	$_end, %dx      //将 setup code 的结束地址 [_end]写入 `dx` 寄存器
	testb	$CAN_USE_HEAP/*CAN_USE_HEAP=(1<<7)*/, loadflags //检查 `loadflags` 中是否设置了 `CAN_USE_HEAP` 标志
	jz	1f
	movw	heap_end_ptr, %dx   //如果 `CAN_USE_HEAP` 被置位，那么将 `heap_end_ptr` 放入 `dx` 寄存器
1:	addw	$STACK_SIZE/*STACK_SIZE=1024*/, %dx //然后加上 `STACK_SIZE` （最小堆栈大小是 512 bytes）
	jnc	2f  //如果结果没有溢出（CF flag 没有置位，如果置位那么程序就出错了）就跳转到标号为 `2`
	xorw	%dx, %dx	//# Prevent wraparound

2:	//# Now %dx should point to the end of our stack space
	andw	$~3, %dx	//# dword align (might as well...)将 `dx` 寄存器的值（就是当前`sp` 寄存器的值）4字节对齐
	jnz	3f              //检查是否为0（如果是0，堆栈就不对了，因为堆栈是从大地址向小地址发展的）
	movw	$0xfffc, %dx	//# Make sure we are not zero
	                        //如果是0，那么就将 `dx` 寄存器的值设置成 `0xfffc` （64KB地址段的最后一个4字节地址）
3:	movw	%ax, %ss    //如果不是0，那么就保持当前值不变。将 `ax` 寄存器的值（ 0x10000 ）设置到 `ss` 寄存器
	movzwl	%dx, %esp	//# Clear upper half of %esp 根据 `dx` 寄存器的值设置正确的 `sp`
	sti			//# Now we should have a working stack 这样我们就得到了正确的堆栈设置

    //# We will have entered with %cs = %ds+0x20, normalize %cs so
    //# it is on par with the other segments.
    //设置堆栈
    //这会将 ds 的值压入堆栈，然后压入 6个标签 的地址并执行 lretw 指令。
    //当 lretw 指令被调用时，它加载标签的地址6到指令指针寄存器和 load cs 与 value ds 。
    //之后，ds和cs将具有相同的值。
    //将 `cs` 段寄存器的值和其他段寄存器一致
	pushw	%ds
	pushw	$6f
	lretw
    //上面的代码使用了一个小小的技巧来重置 `cs` 寄存器的内容，下面我们就来仔细分析。
    //这段代码首先将 `ds`寄存器的值入栈，然后将标号为 [6]的代码段地址入栈 ，接着执行 
    //`lretw` 指令，这条指令，将把标号为 `6` 的内存地址放入 `ip` 寄存器 （[instruction pointer]），
    //将 `ds` 寄存器的值放入 `cs` 寄存器。 这样一来 `ds` 和 `cs` 段寄存器就拥有了相同的值。

    
6:

    //# Check signature at end of setup
    //在我们跳到主要的C代码之前，需要完成的最后两个步骤是设置BSS区域并检查“魔术”签名
    //这只是将setup_sig与幻数进行比较0x5a5aaa55,如果它们不相等，则会报告致命错误。
    //如果魔术数字匹配，则在知道我们有一组正确的段寄存器和一个堆栈的情况下，
    //我们只需要在跳入C代码之前设置BSS部分即可。
    //arch/x86/boot/setup.ld 
    //如果 `magic` 签名是对的， 那么我们只要设置好 `BSS` 段，就可以开始执行 C 代码了。
	cmpl	$0x5a5aaa55, setup_sig
	jne	setup_bad

    //# Zero the bss
    //BSS部分用于存储静态分配的未初始化数据。
    //Linux会仔细确保使用以下代码首先将该内存区域清零：
        //BSS 段用来存储那些没有被初始化的静态变量
    //对于这个段使用的内存， Linux 首先使用下面的代码将其全部清零：
	movw	$__bss_start/*__bss_start=ffffffff83eca000*/, %di   //将__bss_start地址移入di
	movw	$_end+3, %cx    //将_end + 3地址（+3-对齐4个字节）移入cx
	xorl	%eax, %eax      //eax寄存器清零（使用xor指令） 使用 `xor` 指令将 `ax` 寄存器清零
	subw	%di, %cx        //BSS部分的尺寸（cx - di）被计算并投入cx ,计算 BSS 段的大小 （ `cx` - `di` ）
	shrw	$2, %cx         //cx将其除以四（“字”的大小）- 右移 ,将大小放入 `cx` 寄存器,将 `cx` 寄存器除4
	rep; stosl              //最后使用 `rep; stosl` 指令将 `ax` 寄存器的值（0）写入 寄存器整个 BSS 段

    //# Jump to C code (should not return)
    //有堆栈和BSS之后，跳到main()C函数
	calll	main    /* 跳转到 main 函数: arch/x86/boot/main.c */

    //# Setup corrupt somehow...
setup_bad:
	movl	$setup_corrupt, %eax
	calll	puts
    //	# Fall through...

	.globl	die
	.type	die, @function
die:
	hlt
	jmp	die

	.size	die, .-die

	.section ".initdata", "a"
setup_corrupt:
	.byte	7
	.string	"No setup signature found...\n"
