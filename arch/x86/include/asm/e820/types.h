/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_E820_TYPES_H
#define _ASM_E820_TYPES_H

#include <uapi/asm/bootparam.h>

/*
 * These are the E820 types known to the kernel:
 *
 *  rtoax 2021年7月22日
 *
 *  Physic Memory (类型见 `enum e820_type`)
 *
 *  |<--16MB-->|<----------64MB--------->|     |<----reserved--->|<----RAM---->|<-----ACPI----->|
 *  +----------+-------------------------+-----+-----------------+-------------+----------------+
 *  |          |                         |     |                 |             |                |
 *  |          |                         | ... |                 |             |                |
 *  |          |                         |     |                 |             |                |
 *  +----------+-------------------------+-----+-----------------+-------------+----------------+
 *  ^          ^                               ^                 ^             ^
 *  |          |                               |                 |             |
 *  | +--------+                               |                 |             |
 *  | |     +----------------------------------+                 |             |
 *  | |     | +--------------------------------------------------+             |
 *  | |     | | +--------------------------------------------------------------+
 *  | |     | | |
 *  | |     | | |
 * +-+-+---+-+-+-+-+-+-+-+-+
 * | | |   | | | | | | | | |
 * | | | . | | | | | | | | |
 * | | | . | | | | | | | | |
 * | | |   | | | | | | | | |
 * +-+-+---+-+-+-+-+-+-+-+-+
 *      e820_table
 */
enum e820_type {
    /**
     *  
     */
	E820_TYPE_RAM		= 1,
	E820_TYPE_RESERVED	= 2,
	E820_TYPE_ACPI		= 3,
	E820_TYPE_NVS		= 4,
	E820_TYPE_UNUSABLE	= 5,
	E820_TYPE_PMEM		= 7,

	/*
	 * This is a non-standardized way to represent ADR or
	 * NVDIMM regions that persist over a reboot.
	 *
	 * The kernel will ignore their special capabilities
	 * unless the CONFIG_X86_PMEM_LEGACY=y option is set.
	 *
	 * ( Note that older platforms also used 6 for the same
	 *   type of memory, but newer versions switched to 12 as
	 *   6 was assigned differently. Some time they will learn... )
	 */
	E820_TYPE_PRAM		= 12,

	/*
	 * Special-purpose memory is indicated to the system via the
	 * EFI_MEMORY_SP attribute. Define an e820 translation of this
	 * memory type for the purpose of reserving this range and
	 * marking it with the IORES_DESC_SOFT_RESERVED designation.
	 */
	E820_TYPE_SOFT_RESERVED	= 0xefffffff,

	/*
	 * Reserved RAM used by the kernel itself if
	 * CONFIG_INTEL_TXT=y is enabled, memory of this type
	 * will be included in the S3 integrity calculation
	 * and so should not include any memory that the BIOS
	 * might alter over the S3 transition:
	 */
	E820_TYPE_RESERVED_KERN	= 128,
};

/*
 * A single E820 map entry, describing a memory range of [addr...addr+size-1],
 * of 'type' memory type:
 *
 * (We pack it because there can be thousands of them on large systems.)
 */
struct e820_entry {
	u64			addr;
	u64			size;
	enum e820_type		type;
} __attribute__((packed));

/*
 * The legacy E820 BIOS limits us to 128 (E820_MAX_ENTRIES_ZEROPAGE) nodes
 * due to the constrained space in the zeropage.
 *
 * On large systems we can easily have thousands of nodes with RAM,
 * which cannot be fit into so few entries - so we have a mechanism
 * to extend the e820 table size at build-time, via the E820_MAX_ENTRIES
 * define below.
 *
 * ( Those extra entries are enumerated via the EFI memory map, not
 *   via the legacy zeropage mechanism. )
 *
 * Size our internal memory map tables to have room for these additional
 * entries, based on a heuristic calculation: up to three entries per
 * NUMA node, plus E820_MAX_ENTRIES_ZEROPAGE for some extra space.
 *
 * This allows for bootstrap/firmware quirks such as possible duplicate
 * E820 entries that might need room in the same arrays, prior to the
 * call to e820__update_table() to remove duplicates.  The allowance
 * of three memory map entries per node is "enough" entries for
 * the initial hardware platform motivating this mechanism to make
 * use of additional EFI map entries.  Future platforms may want
 * to allow more than three entries per node or otherwise refine
 * this size.
 */

#include <linux/numa.h>

#define E820_MAX_ENTRIES	(E820_MAX_ENTRIES_ZEROPAGE + 3*MAX_NUMNODES)

//e820是和BIOS的一个中断相关的，具体说是 int 0x15。
    //之所以叫 e820 是因为在用这个中断时 ax 必须是 0xe820。
    //这个中断的作用是得到系统的内存布局。
    //因为系统内存会有很多段，每段的类型属性也不一样，所以这个查询是“迭代式”的，每次求得一个段。

/*
 * The whole array of E820 entries:
 *
 * [    0.000000] e820: BIOS-provided physical RAM map:
 * [    0.000000] BIOS-e820: [mem 0x0000000000000000-0x000000000009fbff] usable
 * [    0.000000] BIOS-e820: [mem 0x000000000009fc00-0x000000000009ffff] reserved
 * [    0.000000] BIOS-e820: [mem 0x00000000000f0000-0x00000000000fffff] reserved
 * [    0.000000] BIOS-e820: [mem 0x0000000000100000-0x00000000bff7ffff] usable
 * [    0.000000] BIOS-e820: [mem 0x00000000bff80000-0x00000000bfffffff] reserved
 * [    0.000000] BIOS-e820: [mem 0x00000000feffc000-0x00000000feffffff] reserved
 * [    0.000000] BIOS-e820: [mem 0x00000000fffc0000-0x00000000ffffffff] reserved
 * [    0.000000] BIOS-e820: [mem 0x0000000100000000-0x000000023fffffff] usable 
 *
 *  Physic Memory
 *
 *  |<--16MB-->|<----------64MB--------->|     |<----reserved--->|<----RAM---->|<-----ACPI----->|
 *  +----------+-------------------------+-----+-----------------+-------------+----------------+
 *  |          |                         |     |                 |             |                |
 *  |          |                         | ... |                 |             |                |
 *  |          |                         |     |                 |             |                |
 *  +----------+-------------------------+-----+-----------------+-------------+----------------+
 *  ^          ^                               ^                 ^             ^
 *  |          |                               |                 |             |
 *  | +--------+                               |                 |             |
 *  | |     +----------------------------------+                 |             |
 *  | |     | +--------------------------------------------------+             |
 *  | |     | | +--------------------------------------------------------------+
 *  | |     | | |
 *  | |     | | |
 * +-+-+---+-+-+-+-+-+-+-+-+
 * | | |   | | | | | | | | |
 * | | | . | | | | | | | | |
 * | | | . | | | | | | | | |
 * | | |   | | | | | | | | |
 * +-+-+---+-+-+-+-+-+-+-+-+
 *      e820_table
 */
struct e820_table {
	__u32 nr_entries;
	struct e820_entry entries[E820_MAX_ENTRIES];
};

/*
 * Various well-known legacy memory ranges in physical memory:
 */
#define ISA_START_ADDRESS	0x000a0000
#define ISA_END_ADDRESS		0x00100000

/**
 *  BIOS 所使用的的地址空间范围
 */
#define BIOS_BEGIN		0x000a0000
#define BIOS_END		0x00100000

#define HIGH_MEMORY		0x00100000

/**
 *  只读 ROM
 */
#define BIOS_ROM_BASE		0xffe00000
#define BIOS_ROM_END		0xffffffff

#endif /* _ASM_E820_TYPES_H */
