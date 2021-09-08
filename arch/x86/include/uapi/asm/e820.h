/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_E820_H
#define _UAPI_ASM_X86_E820_H
#define E820MAP	0x2d0		/* our map */
#define E820MAX	128		/* number of entries in E820MAP */

/*
 * Legacy E820 BIOS limits us to 128 (E820MAX) nodes due to the
 * constrained space in the zeropage.  If we have more nodes than
 * that, and if we've booted off EFI firmware, then the EFI tables
 * passed us from the EFI firmware can list more nodes.  Size our
 * internal memory map tables to have room for these additional
 * nodes, based on up to three entries per node for which the
 * kernel was built: MAX_NUMNODES == (1 << CONFIG_NODES_SHIFT),
 * plus E820MAX, allowing space for the possible duplicate E820
 * entries that might need room in the same arrays, prior to the
 * call to sanitize_e820_map() to remove duplicates.  The allowance
 * of three memory map entries per node is "enough" entries for
 * the initial hardware platform motivating this mechanism to make
 * use of additional EFI map entries.  Future platforms may want
 * to allow more than three entries per node or otherwise refine
 * this size.
 */

#ifndef __KERNEL__
#define E820_X_MAX E820MAX
#endif

#define E820NR	0x1e8		/* # entries in E820MAP */

#define E820_RAM	1
#define E820_RESERVED	2
#define E820_ACPI	3
#define E820_NVS	4
#define E820_UNUSABLE	5
#define E820_PMEM	7

/*
 * This is a non-standardized way to represent ADR or NVDIMM regions that
 * persist over a reboot.  The kernel will ignore their special capabilities
 * unless the CONFIG_X86_PMEM_LEGACY option is set.
 *
 * ( Note that older platforms also used 6 for the same type of memory,
 *   but newer versions switched to 12 as 6 was assigned differently.  Some
 *   time they will learn... )
 */
#define E820_PRAM	12

/*
 * reserved RAM used by kernel itself
 * if CONFIG_INTEL_TXT is enabled, memory of this type will be
 * included in the S3 integrity calculation and so should not include
 * any memory that BIOS might alter over the S3 transition
 */
#define E820_RESERVED_KERN        128

#ifndef __ASSEMBLY__
#include <linux/types.h>
/**
 *  对于使用 E820 方式获取内存而言，0x15 号中断处理函数使用结构体e820entry 描述每个段
 *  包括 内存段的其实地址、内存段的大小以及内存段的类型
 *
 *  [    0.000000] BIOS-provided physical RAM map:
 *  [    0.000000] BIOS-e820: [mem 0x0000000000000000-0x000000000009e7ff] usable
 *  [    0.000000] BIOS-e820: [mem 0x000000000009e800-0x000000000009ffff] reserved
 *  [    0.000000] BIOS-e820: [mem 0x00000000000dc000-0x00000000000fffff] reserved
 *  [    0.000000] BIOS-e820: [mem 0x0000000000100000-0x000000007fedffff] usable
 *  [    0.000000] BIOS-e820: [mem 0x000000007fee0000-0x000000007fefefff] ACPI data
 *  [    0.000000] BIOS-e820: [mem 0x000000007feff000-0x000000007fefffff] ACPI NVS
 *  [    0.000000] BIOS-e820: [mem 0x000000007ff00000-0x000000007fffffff] usable
 *  [    0.000000] BIOS-e820: [mem 0x00000000f0000000-0x00000000f7ffffff] reserved
 *  [    0.000000] BIOS-e820: [mem 0x00000000fec00000-0x00000000fec0ffff] reserved
 *  [    0.000000] BIOS-e820: [mem 0x00000000fee00000-0x00000000fee00fff] reserved
 *  [    0.000000] BIOS-e820: [mem 0x00000000fffe0000-0x00000000ffffffff] reserved
 */ 
struct e820entry {
    /**
     *  内存段的起始地址
     */
	__u64 addr;	/* start of memory segment */
    /**
     *  内存段的大小
     */
	__u64 size;	/* size of memory segment */
    /**
     *  内存段的类型
     */
	__u32 type;	/* type of memory segment */
} __attribute__((packed));

struct e820map {
	__u32 nr_map;
	struct e820entry map[E820_X_MAX];
};

#define ISA_START_ADDRESS	0xa0000
#define ISA_END_ADDRESS		0x100000

#define BIOS_BEGIN		0x000a0000
#define BIOS_END		0x00100000

#define BIOS_ROM_BASE		0xffe00000
#define BIOS_ROM_END		0xffffffff

#endif /* __ASSEMBLY__ */


#endif /* _UAPI_ASM_X86_E820_H */
