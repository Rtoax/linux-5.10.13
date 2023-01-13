/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_X86_BOOTPARAM_H
#define _ASM_X86_BOOTPARAM_H

/* setup_data/setup_indirect types */
#define SETUP_NONE			0
#define SETUP_E820_EXT			1
//e820是和BIOS的一个中断相关的，具体说是int 0x15。之所以叫e820是因为在用这个中断时ax必须是0xe820。
//这个中断的作用是得到系统的内存布局。
//因为系统内存会有很多段，每段的类型属性也不一样，所以这个查询是“迭代式”的，每次求得一个段。

#define SETUP_DTB			2   //设备树
#define SETUP_PCI			3
#define SETUP_EFI			4   //EFI（可扩展固件接口的缩写）
#define SETUP_APPLE_PROPERTIES		5
#define SETUP_JAILHOUSE			6

#define SETUP_INDIRECT			(1<<31)

/* SETUP_INDIRECT | max(SETUP_*) */
#define SETUP_TYPE_MAX			(SETUP_INDIRECT | SETUP_JAILHOUSE)

/* ram_size flags */
#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

/* loadflags */
#define LOADED_HIGH	(1<<0)
#define KASLR_FLAG	(1<<1)  /* KALSR - Kernel Address Space Layout Randomization 内核地址空间随机分布 */
#define QUIET_FLAG	(1<<5)
#define KEEP_SEGMENTS	(1<<6)
#define CAN_USE_HEAP	(1<<7)

/* xloadflags */
#define XLF_KERNEL_64			(1<<0)
#define XLF_CAN_BE_LOADED_ABOVE_4G	(1<<1)
#define XLF_EFI_HANDOVER_32		(1<<2)
#define XLF_EFI_HANDOVER_64		(1<<3)
#define XLF_EFI_KEXEC			(1<<4)
#define XLF_5LEVEL			(1<<5)
#define XLF_5LEVEL_ENABLED		(1<<6)

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/screen_info.h>
#include <linux/apm_bios.h>
#include <linux/edd.h>
#include <asm/ist.h>
#include <video/edid.h>

/* extensible setup data list node */
struct setup_data {
	__u64 next;
	__u32 type;
	__u32 len;
	__u8 data[0];
};

/* extensible setup indirect data node */
struct setup_indirect {
	__u32 type;
	__u32 reserved;  /* Reserved, must be set to zero. */
	__u64 len;
	__u64 addr;
};

/**
 *  Documentation/x86/zero-page.txt
 *  该结构包含与linux引导协议中定义的字段相同的字段，
 *  并由引导加载程序以及内核编译/构建时填充
 */
struct setup_header {
    /**
     *  header.S中专门开辟了一部分空间用于存储boot params参数
     *  这些参数都是linux boot protocol协议定义的只读或可写参数，
     *  bootloader程序(例如grub)会根据boot params中定义的setup_sects参数，
     *  将bzImage分割成实模式代码和保护模式代码两个部分，
     *  其中保护模式代码会被bootloader加载到>=0x100000(1M)内存处，
     *  具体的位置有boot params中定义的code32_start参数决定，
     *  实模式代码被bootloader加载到x~x+0x8000这32K低地址区间内，
     *  其具体位置由bootloader决定，这个协议没有规定。
     */
	__u8	setup_sects;
	__u16	root_flags;
	__u32	syssize;
	__u16	ram_size;
	__u16	vid_mode;
	__u16	root_dev;   /* 根设备 */
                        //可以从 x86 linux 内核启动协议中查到：
                        //
                        //Field name: root_dev
                        //Type:       modify (optional)
                        //Offset/size:    0x1fc/2
                        //Protocol:   ALL
                        //
                        //  The default root device device number.  The use of this field is
                        //  deprecated, use the "root=" option on the command line instead
	__u16	boot_flag;
	__u16	jump;
	__u32	header;
	__u16	version;
	__u32	realmode_swtch;
	__u16	start_sys_seg;
	__u16	kernel_version;
	__u8	type_of_loader;
	__u8	loadflags;
	__u16	setup_move_size;
    /**
     * https://zhuanlan.zhihu.com/p/99557658
     *  当设置.config文件中的CONFIG_RANDOMIZE_BASE=n后，
     *  内核代码中引用的虚拟地址不需要 relocate，
     *  加载内核的物理地址由 boot params 中的参数 code32_start（default:0x100000，1M）参数
     *  和#define CONFIG_PHYSICAL_START 0x1000000(16M)这个编译时定义的默认值决定。
     *
     *  该参数定义在arch/x86/boot/header.S中，
     *  仅被 bootloader（grub） 使用， 用于将内核的保护模式代码加载到1M内存地址处，
     *  当内核的实地址代码运行完各种寄存器，
     *  CPU check和某些boot params的初始化操作后，
     *  会进入保护模式并跳转到1M地址处执行head_64.S中start_32，
     */
	__u32	code32_start;
	/**
	 * initramfs 加载地址和大小
	 */
	__u32	ramdisk_image;  /* get_ramdisk_image() */
	__u32	ramdisk_size;   /* get_ramdisk_size() */
	__u32	bootsect_kludge;
	__u16	heap_end_ptr;
	__u8	ext_loader_ver;
	__u8	ext_loader_type;
	__u32	cmd_line_ptr;
	__u32	initrd_addr_max;
	__u32	kernel_alignment;
	__u8	relocatable_kernel;
	__u8	min_alignment;
	__u16	xloadflags;
	__u32	cmdline_size;
	__u32	hardware_subarch;   /* 枚举值 enum x86_hardware_subarch */
	__u64	hardware_subarch_data;
	__u32	payload_offset;
	__u32	payload_length;
	__u64	setup_data;     /* after early param, so could get panic from serial +++ */
	__u64	pref_address;
	__u32	init_size;
	__u32	handover_offset;
	__u32	kernel_info_offset;
} __attribute__((packed));

struct sys_desc_table {
	__u16 length;
	__u8  table[14];
};

/* Gleaned from OFW's set-parameters in cpu/x86/pc/linux.fth */
struct olpc_ofw_header {
	__u32 ofw_magic;	/* OFW signature */
	__u32 ofw_version;
	__u32 cif_handler;	/* callback into OFW */
	__u32 irq_desc_table;
} __attribute__((packed));

struct efi_info {
	__u32 efi_loader_signature;
	__u32 efi_systab;
	__u32 efi_memdesc_size;
	__u32 efi_memdesc_version;
	__u32 efi_memmap;
	__u32 efi_memmap_size;
	__u32 efi_systab_hi;
	__u32 efi_memmap_hi;
};

/*
 * This is the maximum number of entries in struct boot_params::e820_table
 * (the zeropage), which is part of the x86 boot protocol ABI:
 */
#define E820_MAX_ENTRIES_ZEROPAGE 128

/*
 * The E820 memory region entry of the boot protocol ABI:
 * 内存段的起始地址
 * 内存段的大小
 * 内存段的类型（类型可以是reserved, usable等等)。
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
struct boot_e820_entry {
	__u64 addr;
	__u64 size;
	__u32 type;
} __attribute__((packed));

/*
 * Smallest compatible version of jailhouse_setup_data required by this kernel.
 */
#define JAILHOUSE_SETUP_REQUIRED_VERSION	1

/*
 * The boot loader is passing platform information via this Jailhouse-specific
 * setup data structure.
 */
struct jailhouse_setup_data {
	struct {
		__u16	version;
		__u16	compatible_version;
	} __attribute__((packed)) hdr;
	struct {
		__u16	pm_timer_address;
		__u16	num_cpus;
		__u64	pci_mmconfig_base;
		__u32	tsc_khz;
		__u32	apic_khz;
		__u8	standard_ioapic;
		__u8	cpu_ids[255];
	} __attribute__((packed)) v1;
	struct {
		__u32	flags;
	} __attribute__((packed)) v2;
} __attribute__((packed));

/**
 * The so-called "zeropage" - 所谓的零 页 page0
 * 我把 `boot_params` 改名为 `boot_parameters`
 *
 * https://www.kernel.org/doc/Documentation/x86/boot.txt
 */
struct boot_parameters {
	struct screen_info screen_info;			/* 0x000 */
	struct apm_bios_info apm_bios_info;		/* 0x040 */
	__u8  _pad2[4];					/* 0x054 */
	__u64  tboot_addr;				/* 0x058 */
	struct ist_info ist_info;			/* 0x060 */
	__u64 acpi_rsdp_addr;				/* 0x070 */
	__u8  _pad3[8];					/* 0x078 */
	__u8  hd0_info[16];	/* obsolete! */		/* 0x080 */
	__u8  hd1_info[16];	/* obsolete! */		/* 0x090 */
	struct sys_desc_table sys_desc_table; /* obsolete! */	/* 0x0a0 */
	struct olpc_ofw_header olpc_ofw_header;		/* 0x0b0 */
	__u32 ext_ramdisk_image;			/* 0x0c0 */
	__u32 ext_ramdisk_size;				/* 0x0c4 */
	__u32 ext_cmd_line_ptr;				/* 0x0c8 */
	__u8  _pad4[116];				/* 0x0cc */
	struct edid_info edid_info;			/* 0x140 */
	struct efi_info efi_info;			/* 0x1c0 */
	__u32 alt_mem_k;				/* 0x1e0 */
	__u32 scratch;		/* Scratch field! */	/* 0x1e4 = BP_scratch */
    /**
     *  e820_table 的个数， 在 detect_memory_e820() 中被初始化
     */
	__u8  e820_entries;				/* 0x1e8 */
	__u8  eddbuf_entries;				/* 0x1e9 */
	__u8  edd_mbr_sig_buf_entries;			/* 0x1ea */
	__u8  kbd_status;				/* 0x1eb */
	__u8  secure_boot;				/* 0x1ec */
	__u8  _pad5[2];					/* 0x1ed */
	/*
	 * The sentinel is set to a nonzero value (0xff) in header.S.
	 *
	 * A bootloader is supposed to only take setup_header and put
	 * it into a clean boot_params buffer. If it turns out that
	 * it is clumsy or too generous with the buffer, it most
	 * probably will pick up the sentinel variable too. The fact
	 * that this variable then is still 0xff will let kernel
	 * know that some variables in boot_params are invalid and
	 * kernel should zero out certain portions of boot_params.
	 */
	__u8  sentinel;					/* 0x1ef *//* 哨兵 */
	__u8  _pad6[1];					/* 0x1f0 */
	struct setup_header hdr;    /* setup header */	/* 0x1f1 */
	__u8  _pad7[0x290-0x1f1-sizeof(struct setup_header)];
	__u32 edd_mbr_sig_buffer[EDD_MBR_SIG_MAX];	/* 0x290 */

    /**
     *  启动 bios 获取 的  e820 table，个数用 `e820_entries`记录
     *  将在 detect_memory_e820() 中被初始化
     *
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
     *      e820_table 遍历
     */
	struct boot_e820_entry e820_table[E820_MAX_ENTRIES_ZEROPAGE/*128*/]; /* 0x2d0 */

	__u8  _pad8[48];				/* 0xcd0 */

	struct edd_info eddbuf[EDDMAXNR];		/* 0xd00 */

	__u8  _pad9[276];				/* 0xeec */
} __attribute__((packed));

/**
 * enum x86_hardware_subarch - x86 hardware subarchitecture
 *
 * The x86 hardware_subarch and hardware_subarch_data were added as of the x86
 * boot protocol 2.07 to help distinguish and support custom x86 boot
 * sequences. This enum represents accepted values for the x86
 * hardware_subarch.  Custom x86 boot sequences (not X86_SUBARCH_PC) do not
 * have or simply *cannot* make use of natural stubs like BIOS or EFI, the
 * hardware_subarch can be used on the Linux entry path to revector to a
 * subarchitecture stub when needed. This subarchitecture stub can be used to
 * set up Linux boot parameters or for special care to account for nonstandard
 * handling of page tables.
 *
 * These enums should only ever be used by x86 code, and the code that uses
 * it should be well contained and compartamentalized.
 *
 * KVM and Xen HVM do not have a subarch as these are expected to follow
 * standard x86 boot entries. If there is a genuine need for "hypervisor" type
 * that should be considered separately in the future. Future guest types
 * should seriously consider working with standard x86 boot stubs such as
 * the BIOS or EFI boot stubs.
 *
 * WARNING: this enum is only used for legacy hacks, for platform features that
 *	    are not easily enumerated or discoverable. You should not ever use
 *	    this for new features.
 *
 * @X86_SUBARCH_PC: Should be used if the hardware is enumerable using standard
 *	PC mechanisms (PCI, ACPI) and doesn't need a special boot flow.
 * @X86_SUBARCH_LGUEST: Used for x86 hypervisor demo, lguest, deprecated
 * @X86_SUBARCH_XEN: Used for Xen guest types which follow the PV boot path,
 * 	which start at asm startup_xen() entry point and later jump to the C
 * 	xen_start_kernel() entry point. Both domU and dom0 type of guests are
 * 	currently supportd through this PV boot path.
 * @X86_SUBARCH_INTEL_MID: Used for Intel MID (Mobile Internet Device) platform
 *	systems which do not have the PCI legacy interfaces.
 * @X86_SUBARCH_CE4100: Used for Intel CE media processor (CE4100) SoC
 * 	for settop boxes and media devices, the use of a subarch for CE4100
 * 	is more of a hack...
 */
enum x86_hardware_subarch { /* X86 子架构  */
	X86_SUBARCH_PC = 0,     /* 个人计算机 */
	X86_SUBARCH_LGUEST,     /* 虚拟机管理程序 */
	X86_SUBARCH_XEN,        /* Xen guest */
	X86_SUBARCH_INTEL_MID,  /* 手机网络设备 */
	X86_SUBARCH_CE4100,     /* CE media processor (CE4100) SoC */
	X86_NR_SUBARCHS,
};

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_BOOTPARAM_H */
