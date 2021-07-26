// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 * This file contains the setup_arch() code, which handles the architecture-dependent
 * parts of early kernel initialization.
 */
#include <linux/console.h>
#include <linux/crash_dump.h>
#include <linux/dma-map-ops.h>
#include <linux/dmi.h>
#include <linux/efi.h>
#include <linux/init_ohci1394_dma.h>
#include <linux/initrd.h>
#include <linux/iscsi_ibft.h>
#include <linux/memblock.h>
#include <linux/pci.h>
#include <linux/root_dev.h>
#include <linux/sfi.h>
#include <linux/hugetlb.h>
#include <linux/tboot.h>
#include <linux/usb/xhci-dbgp.h>
#include <linux/static_call.h>
#include <linux/swiotlb.h>

#include <uapi/linux/mount.h>

#include <xen/xen.h>

#include <asm/apic.h>
#include <asm/numa.h>
#include <asm/bios_ebda.h>
#include <asm/bugs.h>
#include <asm/cpu.h>
#include <asm/efi.h>
#include <asm/gart.h>
#include <asm/hypervisor.h>
#include <asm/io_apic.h>
#include <asm/kasan.h>
#include <asm/kaslr.h>
#include <asm/mce.h>
#include <asm/mtrr.h>
#include <asm/realmode.h>
#include <asm/olpc_ofw.h>
#include <asm/pci-direct.h>
#include <asm/prom.h>
#include <asm/proto.h>
#include <asm/unwind.h>
#include <asm/vsyscall.h>
#include <linux/vmalloc.h>

/*
 * max_low_pfn_mapped: highest directly mapped pfn < 4 GB
 * max_pfn_mapped:     highest directly mapped pfn > 4 GB
 *
 * The direct mapping only covers E820_TYPE_RAM regions, so the ranges and gaps are
 * represented by pfn_mapped[].
 */
unsigned long max_low_pfn_mapped;
unsigned long max_pfn_mapped;

#ifdef CONFIG_DMI
RESERVE_BRK(dmi_alloc, 65536);
#endif


/*
 * Range of the BSS area. The size of the BSS area is determined
 * at link time, with RESERVE_BRK*() facility reserving additional
 * chunks.
 */
unsigned long _brk_start = (unsigned long)__brk_base;   /* brk() 开始 */
unsigned long _brk_end   = (unsigned long)__brk_base;   /* brk() 结束 */

struct boot_parameters boot_params;

/*
 * These are the four main kernel memory regions, we put them into
 * the resource tree so that kdump tools and other debugging tools
 * recover it:
 */

static struct resource rodata_resource = {
	.name	= "Kernel rodata",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};

static struct resource data_resource = {
	.name	= "Kernel data",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};

static struct resource code_resource = {/*  */
	.name	= "Kernel code",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};

static struct resource bss_resource = {/*  */
	.name	= "Kernel bss",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};


#ifdef CONFIG_X86_32
/* CPU data as detected by the assembly code in head_32.S */
struct cpuinfo_x86 new_cpu_data;/*  */

/* Common CPU data for all CPUs */
struct cpuinfo_x86 __read_mostly boot_cpu_data ;
EXPORT_SYMBOL(boot_cpu_data);

unsigned int def_to_bigsmp;

/* For MCA, but anyone else can use it if they want */
unsigned int machine_id;
unsigned int machine_submodel_id;
unsigned int BIOS_revision;

struct apm_info apm_info;
EXPORT_SYMBOL(apm_info);

#if defined(CONFIG_X86_SPEEDSTEP_SMI) || \
	defined(CONFIG_X86_SPEEDSTEP_SMI_MODULE)
//struct ist_info ist_info;
//EXPORT_SYMBOL(ist_info);
#else
struct ist_info ist_info;
#endif

#else
struct cpuinfo_x86 __read_mostly boot_cpu_data ;
EXPORT_SYMBOL(boot_cpu_data);
#endif


#if !defined(CONFIG_X86_PAE) || defined(CONFIG_X86_64)
__visible unsigned long __ro_after_init mmu_cr4_features ;
#else
//__visible unsigned long __ro_after_init mmu_cr4_features  = X86_CR4_PAE;
#endif

/* Boot loader ID and version as integers, for the benefit of proc_dointvec */
int bootloader_type, bootloader_version;

/*
 * Setup options
 */
struct screen_info screen_info;
EXPORT_SYMBOL(screen_info);
struct edid_info edid_info;
EXPORT_SYMBOL_GPL(edid_info);

extern int root_mountflags;

unsigned long saved_video_mode;

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char __initdata command_line[COMMAND_LINE_SIZE];
#ifdef CONFIG_CMDLINE_BOOL
static char __initdata builtin_cmdline[COMMAND_LINE_SIZE] = CONFIG_CMDLINE;
#endif

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
struct edd edd;
#ifdef CONFIG_EDD_MODULE
EXPORT_SYMBOL(edd);
#endif
/**
 * copy_edd() - Copy the BIOS EDD information
 *              from boot_params into a safe place.
 *
 */
static inline void __init copy_edd(void) /* BIOS Enhanced Disk Drive Specification */
{
     memcpy(edd.mbr_signature, boot_params.edd_mbr_sig_buffer,
	    sizeof(edd.mbr_signature));
     memcpy(edd.edd_info, boot_params.eddbuf, sizeof(edd.edd_info));
     edd.mbr_signature_nr = boot_params.edd_mbr_sig_buf_entries;
     edd.edd_info_nr = boot_params.eddbuf_entries;
}
#else
/*  */
#endif

//会在 `brk` 段中预留给定大小的空间
//`brk` 区段在内存中的位置恰好就在 [BSS](http://en.wikipedia.org/wiki/.bss) 区段后面
void * __init extend_brk(size_t size, size_t align)
{
	size_t mask = align - 1;
	void *ret;

	BUG_ON(_brk_start == 0);
	BUG_ON(align & mask);

	_brk_end = (_brk_end + mask) & ~mask;
	BUG_ON((char *)(_brk_end + size) > __brk_limit);

	ret = (void *)_brk_end;
	_brk_end += size;

	memset(ret, 0, size);

	return ret;
}

#ifdef CONFIG_X86_32
//static void __init cleanup_highmap(void)
//{
//}
#endif

/**
 *  
 */
static void __init reserve_brk(void)    /*  */
{
	if (_brk_end > _brk_start)
		memblock_reserve(__pa_symbol(_brk_start),
				 _brk_end - _brk_start);

	/* Mark brk area as locked down and no longer taking any
	   new allocations 
	   把 `_brk_start` 赋值为0,因为在这之后我们不会再为 `brk` 分配内存了*/
	_brk_start = 0;
}

u64 relocated_ramdisk;

#ifdef CONFIG_BLK_DEV_INITRD

static u64 __init get_ramdisk_image(void)   /* RAM */
{
	u64 ramdisk_image = boot_params.hdr.ramdisk_image;

	ramdisk_image |= (u64)boot_params.ext_ramdisk_image << 32;

	if (ramdisk_image == 0)
		ramdisk_image = phys_initrd_start;  /*  */

	return ramdisk_image;
}
static u64 __init get_ramdisk_size(void)
{
	u64 ramdisk_size = boot_params.hdr.ramdisk_size;

	ramdisk_size |= (u64)boot_params.ext_ramdisk_size << 32;

	if (ramdisk_size == 0)
		ramdisk_size = phys_initrd_size;

	return ramdisk_size;
}

static void __init relocate_initrd(void)
{
	/* Assume only end is not page aligned */
	u64 ramdisk_image = get_ramdisk_image();
	u64 ramdisk_size  = get_ramdisk_size();
	u64 area_size     = PAGE_ALIGN(ramdisk_size);

	/* We need to move the initrd down into directly mapped mem */
	relocated_ramdisk = memblock_phys_alloc_range(area_size, PAGE_SIZE, 0,
						      PFN_PHYS(max_pfn_mapped));
	if (!relocated_ramdisk)
		panic("Cannot find place for new RAMDISK of size %lld\n",
		      ramdisk_size);

	initrd_start = relocated_ramdisk + PAGE_OFFSET;
	initrd_end   = initrd_start + ramdisk_size;

    //# sudo dmesg | grep RAM
    //[    0.000000] RAMDISK: [mem 0x345ae000-0x362cefff]
	printk(KERN_INFO "Allocated new RAMDISK: [mem %#010llx-%#010llx]\n",
	       relocated_ramdisk, relocated_ramdisk + ramdisk_size - 1);

    /* 从 early_reserve_initrd() 预留的地方拷贝 */
	copy_from_early_mem((void *)initrd_start, ramdisk_image, ramdisk_size);

	printk(KERN_INFO "Move RAMDISK from [mem %#010llx-%#010llx] to"
		" [mem %#010llx-%#010llx]\n",
		ramdisk_image, ramdisk_image + ramdisk_size - 1,
		relocated_ramdisk, relocated_ramdisk + ramdisk_size - 1);
}

/* Linux初始RAM磁盘（initrd）是在系统引导过程中挂载的一个临时根文件系统，用来支持两阶段的引导过程。 
        根文件系统就是通过这方式来进行初始化, 此函数获取RAM DISK的基地址以及大小以及大小加偏移*/
static void __init early_reserve_initrd(void)
{
    /**
     *  //Documentation/x86/zero-page 中有
     * 0C0/004		ALL	ext_ramdisk_image	ramdisk_image high 32bits
     * 0C4/004		ALL	ext_ramdisk_size	ramdisk_size high 32bits
     */
    //所有的这些啊参数都来自于`boot_params`
	/* Assume only end is not page aligned */
	u64 ramdisk_image = get_ramdisk_image();    /* struct setup_header */
	u64 ramdisk_size  = get_ramdisk_size();     /* struct setup_header */
	u64 ramdisk_end   = PAGE_ALIGN(ramdisk_image + ramdisk_size);

    //检查bootloader 提供的ramdisk信息
	if (!boot_params.hdr.type_of_loader ||
	    !ramdisk_image || !ramdisk_size)
		return;		/* No initrd provided by bootloader */

    //保留内存块将ramdisk传输到最终的内存地址
	memblock_reserve(ramdisk_image, ramdisk_end - ramdisk_image);/* 预留 */
}

/**
 *  1.starts from the definition of the base address and end address of the `initrd`;
 *  2.check that `initrd` is provided by a bootloader
 */
/**
 *  Linux初始RAM磁盘(initrd)是在系统引导过程中挂载的一个临时根文件系统，用来支持两阶段的引导过程。
 *  根文件系统就是通过这方式来进行初始化, 此函数获取RAM DISK的基地址以及大小以及大小加偏移
 *
 *  Ram Disk - initRD - initrd
 */
static void __init reserve_initrd(void)
{
	/* Assume only end is not page aligned */
	u64 ramdisk_image = get_ramdisk_image();    /* struct setup_header */
	u64 ramdisk_size  = get_ramdisk_size();     /* struct setup_header */
	u64 ramdisk_end   = PAGE_ALIGN(ramdisk_image + ramdisk_size);

    //检查bootloader 提供的ramdisk信息
	if (!boot_params.hdr.type_of_loader ||
	    !ramdisk_image || !ramdisk_size)
		return;		/* No initrd provided by bootloader */

	initrd_start = 0;

	printk(KERN_INFO "RAMDISK: [mem %#010llx-%#010llx]\n", ramdisk_image,
			ramdisk_end - 1);

	if (pfn_range_is_mapped(PFN_DOWN(ramdisk_image),
				PFN_DOWN(ramdisk_end))) {
		/* All are mapped, easy case */
		initrd_start = ramdisk_image + PAGE_OFFSET;
		initrd_end = initrd_start + ramdisk_size;
		return;
	}

    /* see early_reserve_initrd() */
	relocate_initrd();

    /* 对应将   early_reserve_initrd 中的 memblock_reserve 释放*/
	memblock_free(ramdisk_image, ramdisk_end - ramdisk_image);
}

#else
/*  */
#endif /* CONFIG_BLK_DEV_INITRD */

static void __init parse_setup_data(void) /*  */
{
	struct setup_data *data;
	u64 pa_data, pa_next;

	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		u32 data_len, data_type;

		data = early_memremap(pa_data, sizeof(*data));
		data_len = data->len + sizeof(struct setup_data);
		data_type = data->type;
		pa_next = data->next;
		early_memunmap(data, sizeof(*data));

		switch (data_type) {
		case SETUP_E820_EXT:/* 内存布局 */
			e820__memory_setup_extended(pa_data, data_len);
			break;
		case SETUP_DTB: //设备树
			add_dtb(pa_data);
			break;
		case SETUP_EFI: //EFI（可扩展固件接口的缩写）
			parse_efi_setup(pa_data, data_len);
			break;
		default:
			break;
		}
		pa_data = pa_next;
	}
}

//为 `setup_data` 重新映射内存并保留内存块
static void __init memblock_x86_reserve_range_setup_data(void)
{
	struct setup_data *data;
	u64 pa_data;

	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		memblock_reserve(pa_data, sizeof(*data) + data->len);

		if (data->type == SETUP_INDIRECT &&
		    ((struct setup_indirect *)data->data)->type != SETUP_INDIRECT)
			memblock_reserve(((struct setup_indirect *)data->data)->addr,
					 ((struct setup_indirect *)data->data)->len);

		pa_data = data->next;
		early_memunmap(data, sizeof(*data));
	}
}

/*
 * --------- Crashkernel reservation ------------------------------
 */

#ifdef CONFIG_KEXEC_CORE

/* 16M alignment for crash kernel regions */
#define CRASH_ALIGN		SZ_16M

/*
 * Keep the crash kernel below this limit.
 *
 * Earlier 32-bits kernels would limit the kernel to the low 512 MB range
 * due to mapping restrictions.
 *
 * 64-bit kdump kernels need to be restricted to be under 64 TB, which is
 * the upper limit of system RAM in 4-level paging mode. Since the kdump
 * jump could be from 5-level paging to 4-level paging, the jump will fail if
 * the kernel is put above 64 TB, and during the 1st kernel bootup there's
 * no good way to detect the paging mode of the target kernel which will be
 * loaded for dumping.
 */
#ifdef CONFIG_X86_32
//# define CRASH_ADDR_LOW_MAX	SZ_512M
//# define CRASH_ADDR_HIGH_MAX	SZ_512M
#else
# define CRASH_ADDR_LOW_MAX	SZ_4G
# define CRASH_ADDR_HIGH_MAX	SZ_64T
#endif

static int __init reserve_crashkernel_low(void)
{
#ifdef CONFIG_X86_64
	unsigned long long base, low_base = 0, low_size = 0;
	unsigned long low_mem_limit;
	int ret;

	low_mem_limit = min(memblock_phys_mem_size(), CRASH_ADDR_LOW_MAX);

	/* crashkernel=Y,low */
	ret = parse_crashkernel_low(boot_command_line, low_mem_limit, &low_size, &base);
	if (ret) {
		/*
		 * two parts from kernel/dma/swiotlb.c:
		 * -swiotlb size: user-specified with swiotlb= or default.
		 *
		 * -swiotlb overflow buffer: now hardcoded to 32k. We round it
		 * to 8M for other buffers that may need to stay low too. Also
		 * make sure we allocate enough extra low memory so that we
		 * don't run out of DMA buffers for 32-bit devices.
		 */
		low_size = max(swiotlb_size_or_default() + (8UL << 20), 256UL << 20);
	} else {
		/* passed with crashkernel=0,low ? */
		if (!low_size)
			return 0;
	}

	low_base = memblock_phys_alloc_range(low_size, CRASH_ALIGN, 0, CRASH_ADDR_LOW_MAX);
	if (!low_base) {
		pr_err("Cannot reserve %ldMB crashkernel low memory, please try smaller size.\n",
		       (unsigned long)(low_size >> 20));
		return -ENOMEM;
	}

	pr_info("Reserving %ldMB of low memory at %ldMB for crashkernel (low RAM limit: %ldMB)\n",
		(unsigned long)(low_size >> 20),
		(unsigned long)(low_base >> 20),
		(unsigned long)(low_mem_limit >> 20));

	crashk_low_res.start = low_base;
	crashk_low_res.end   = low_base + low_size - 1;
	insert_resource(&iomem_resource, &crashk_low_res);
#endif
	return 0;
}

static void __init reserve_crashkernel(void)
{
	unsigned long long crash_size, crash_base, total_mem;
	bool high = false;
	int ret;

	total_mem = memblock_phys_mem_size();

	/* crashkernel=XM */
	ret = parse_crashkernel(boot_command_line, total_mem, &crash_size, &crash_base);
	if (ret != 0 || crash_size <= 0) {
		/* crashkernel=X,high */
		ret = parse_crashkernel_high(boot_command_line, total_mem,
					     &crash_size, &crash_base);
		if (ret != 0 || crash_size <= 0)
			return;
		high = true;
	}

	if (xen_pv_domain()) {
		pr_info("Ignoring crashkernel for a Xen PV domain\n");
		return;
	}

	/* 0 means: find the address automatically */
	if (!crash_base) {
		/*
		 * Set CRASH_ADDR_LOW_MAX upper bound for crash memory,
		 * crashkernel=x,high reserves memory over 4G, also allocates
		 * 256M extra low memory for DMA buffers and swiotlb.
		 * But the extra memory is not required for all machines.
		 * So try low memory first and fall back to high memory
		 * unless "crashkernel=size[KMG],high" is specified.
		 */
		if (!high)
			crash_base = memblock_phys_alloc_range(crash_size,
						CRASH_ALIGN, CRASH_ALIGN,
						CRASH_ADDR_LOW_MAX);
		if (!crash_base)
			crash_base = memblock_phys_alloc_range(crash_size,
						CRASH_ALIGN, CRASH_ALIGN,
						CRASH_ADDR_HIGH_MAX);
		if (!crash_base) {
			pr_info("crashkernel reservation failed - No suitable area found.\n");
			return;
		}
	} else {
		unsigned long long start;

		start = memblock_phys_alloc_range(crash_size, SZ_1M, crash_base,
						  crash_base + crash_size);
		if (start != crash_base) {
			pr_info("crashkernel reservation failed - memory is in use.\n");
			return;
		}
	}

	if (crash_base >= (1ULL << 32) && reserve_crashkernel_low()) {
		memblock_free(crash_base, crash_size);
		return;
	}

	pr_info("Reserving %ldMB of memory at %ldMB for crashkernel (System RAM: %ldMB)\n",
		(unsigned long)(crash_size >> 20),
		(unsigned long)(crash_base >> 20),
		(unsigned long)(total_mem >> 20));

	crashk_res.start = crash_base;
	crashk_res.end   = crash_base + crash_size - 1;
	insert_resource(&iomem_resource, &crashk_res);
}
#else
/*  */
#endif

/**
 *  
 */
//reserve standard I/O resources like `DMA`, `TIMER`, `FPU`, etc
static struct resource standard_io_resources[] = {
	{ .name = "dma1", .start = 0x00, .end = 0x1f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic1", .start = 0x20, .end = 0x21,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer0", .start = 0x40, .end = 0x43,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer1", .start = 0x50, .end = 0x53,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x60, .end = 0x60,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x64, .end = 0x64,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma page reg", .start = 0x80, .end = 0x8f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic2", .start = 0xa0, .end = 0xa1,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma2", .start = 0xc0, .end = 0xdf,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "fpu", .start = 0xf0, .end = 0xff,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO }
};

//reserve standard I/O resources like `DMA`, `TIMER`, `FPU`, etc
void __init reserve_standard_io_resources(void)
{
	int i;

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < ARRAY_SIZE(standard_io_resources); i++)
		request_resource(&ioport_resource, &standard_io_resources[i]);
}

static __init void reserve_ibft_region(void)
{
	unsigned long addr, size = 0;

	addr = find_ibft_region(&size);

	if (size)
		memblock_reserve(addr, size);
}

static bool __init snb_gfx_workaround_needed(void)
{
#ifdef CONFIG_PCI
	int i;
	u16 vendor, devid;
	static const __initconst u16 snb_ids[] = {
		0x0102,
		0x0112,
		0x0122,
		0x0106,
		0x0116,
		0x0126,
		0x010a,
	};

	/* Assume no if something weird is going on with PCI */
	if (!early_pci_allowed())
		return false;

	vendor = read_pci_config_16(0, 2, 0, PCI_VENDOR_ID);
	if (vendor != 0x8086)
		return false;

	devid = read_pci_config_16(0, 2, 0, PCI_DEVICE_ID);
	for (i = 0; i < ARRAY_SIZE(snb_ids); i++)
		if (devid == snb_ids[i])
			return true;
#endif

	return false;
}

/*
 * Sandy Bridge graphics has trouble with certain ranges, exclude
 * them from allocation.
 *
 * 用于清除掉以 `0x20050000`, `0x20110000` 等地址开头的内存空间
 */
static void __init trim_snb_memory(void)
{
	static const __initconst unsigned long bad_pages[] = {
		0x20050000,
		0x20110000,
		0x20130000,
		0x20138000,
		0x40004000,
	};
	int i;

	if (!snb_gfx_workaround_needed())
		return;

	printk(KERN_DEBUG "reserving inaccessible SNB gfx pages\n");

	/*
	 * Reserve all memory below the 1 MB mark that has not
	 * already been reserved.
	 */
	memblock_reserve(0, 1<<20);
	
	for (i = 0; i < ARRAY_SIZE(bad_pages); i++) {
		if (memblock_reserve(bad_pages[i], PAGE_SIZE))
			printk(KERN_WARNING "failed to reserve 0x%08lx\n",
			       bad_pages[i]);
	}
}

/*
 * Here we put platform-specific memory range workarounds, i.e.
 * memory known to be corrupt or otherwise in need to be reserved on
 * specific platforms.
 *
 * If this gets used more widely it could use a real dispatch mechanism.
 *
 * 用于清除掉以 `0x20050000`, `0x20110000` 等地址开头的内存空间
 */
static void __init trim_platform_memory_ranges(void)
{
	trim_snb_memory();
}

/**
 *  从 e820 中移除 BIOS   的部分
 */
static void __init trim_bios_range(void)
{
	/*
	 * special case: Some BIOSes report the PC BIOS
	 * area (640Kb -> 1Mb) as RAM even though it is not.
	 * take them out.
	 */
	e820__range_remove(BIOS_BEGIN, BIOS_END - BIOS_BEGIN, E820_TYPE_RAM, 1);

    /**
     *  更新一波
     */
	e820__update_table(e820_table);
}

/**
 *  将内核映射到 RAM 中
 */
/* called before trim_bios_range() to spare extra sanitize */
static void __init e820_add_kernel_range(void)
{
    /**
     *  获取内核代码段 的起始地址 ~ 内核 结束
     */
	u64 start = __pa_symbol(_text);
	u64 size = __pa_symbol(_end) - start;

	/*
	 * Complain if .text .data and .bss are not marked as E820_TYPE_RAM and
	 * attempt to fix it by adding the range. We may have a confused BIOS,
	 * or the user may have used memmap=exactmap or memmap=xxM$yyM to
	 * exclude kernel range. If we really are running on top non-RAM,
	 * we will crash later anyways.
	 *
	 * 将 内核 镜像 映射到 物理内存（RAM）中
     *  遍历整个 table *
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
	if (e820__mapped_all(start, start + size, E820_TYPE_RAM))
		return;

	pr_warn(".text .data .bss are not marked as E820_TYPE_RAM!\n");

    /**
     *  出错流程
     */
	e820__range_remove(start, size, E820_TYPE_RAM, 0);
	e820__range_add(start, size, E820_TYPE_RAM);
}

static unsigned reserve_low = CONFIG_X86_RESERVE_LOW << 10;

static int __init parse_reservelow(char *p)
{
	unsigned long long size;

	if (!p)
		return -EINVAL;

	size = memparse(p, &p);

	if (size < 4096)
		size = 4096;

	if (size > 640*1024)
		size = 640*1024;

	reserve_low = size;

	return 0;
}

early_param("reservelow", parse_reservelow);

    //用于保留 `memblock` 中的前4KB页面
static void __init trim_low_memory_range(void)
{
	/*
	 * A special case is the first 4Kb of memory;
	 * This is a BIOS owned area, not kernel ram, but generally
	 * not listed as such in the E820 table.
	 *
	 * This typically reserves additional memory (64KiB by default)
	 * since some BIOSes are known to corrupt low memory.  See the
	 * Kconfig help text for X86_RESERVE_LOW.
	 */
	memblock_reserve(0, ALIGN(reserve_low, PAGE_SIZE));
}
	
/*
 * Dump out kernel offset information on panic.
 */
static int
dump_kernel_offset(struct notifier_block *self, unsigned long v, void *p)
{
	if (kaslr_enabled()) {
		pr_emerg("Kernel Offset: 0x%lx from 0x%lx (relocation range: 0x%lx-0x%lx)\n",
			 kaslr_offset(),
			 __START_KERNEL,
			 __START_KERNEL_map,
			 MODULES_VADDR-1);
	} else {
		pr_emerg("Kernel Offset: disabled\n");
	}

	return 0;
}

/*
 * Determine if we were loaded by an EFI loader.  If so, then we have also been
 * passed the efi memmap, systab, etc., so we should use these data structures
 * for initialization.  Note, the efi init code path is determined by the
 * global efi_enabled. This allows the same kernel image to be used on existing
 * systems (with a traditional BIOS) as well as on EFI systems.
 */
/*
 * setup_arch - architecture-specific boot-time initializations
 *
 * Note: On x86_64, fixmaps are ready for use even before this is called.
 */

void __init setup_arch(char **cmdline_p)/* 初始化 */
{
    /**
     *  此函数解析内核的段`_text`和`_data`来自于`_text`符号和`_bss_stop`,
     *  arch/x86/kernel/head_64.S
     */
	/*
	 * Reserve the memory occupied by the kernel between _text and
	 * __end_of_kernel_reserve symbols. Any kernel sections after the
	 * __end_of_kernel_reserve symbol must be explicitly reserved with a
	 * separate memblock_reserve() or they will be discarded.
	 *//* 在代码段 和 __end_of_kernel_reserve 之间预留内存，
	    后面 的 kernel section 都需要调用此函数预留 */
	memblock_reserve(__pa_symbol(_text), (unsigned long)__end_of_kernel_reserve - (unsigned long)_text);

	/*
	 * Make sure page 0 is always reserved because on systems with
	 * L1TF its contents can be leaked to user processes. 
	 */
	memblock_reserve(0, PAGE_SIZE);/* 确保 page 0 被预留，因为可能会泄漏到用户进程 */
    
    /* Linux初始RAM磁盘(initrd)是在系统引导过程中挂载的一个临时根文件系统，用来支持两阶段的引导过程。
        根文件系统就是通过这方式来进行初始化, 此函数获取RAM DISK的基地址以及大小以及大小加偏移*/
	early_reserve_initrd();/* Ram Disk - initRD - initrd */

	/*
	 * At this point everything still needed from the boot loader
	 * or BIOS or kernel text should be early reserved or marked not
	 * RAM in e820. All other memory is free game.
	 */

#ifdef CONFIG_X86_32/* 32位这部分代码 */
	memcpy(&boot_cpu_data, &new_cpu_data, sizeof(new_cpu_data));

	/*
	 * copy kernel address range established so far and switch
	 * to the proper swapper page table
	 *//*  */
	clone_pgd_range(swapper_pg_dir/* init_task 以及页表 */     + KERNEL_PGD_BOUNDARY,
			initial_page_table + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

    /* 保存全局页表到 CR3 寄存器 */
	load_cr3(swapper_pg_dir);/* CR3 寄存器 */
	/*
	 * Note: Quark X1000 CPUs advertise PGE incorrectly and require
	 * a cr3 based tlb flush, so the following __flush_tlb_all()
	 * will not flush anything because the CPU quirk which clears
	 * X86_FEATURE_PGE has not been invoked yet. Though due to the
	 * load_cr3() above the TLB has been flushed already. The
	 * quirk is invoked before subsequent calls to __flush_tlb_all()
	 * so proper operation is guaranteed.
	 */
	__flush_tlb_all();/* 刷新 TLB */
#else /* 64 位 */
	printk(KERN_INFO "Command line: %s\n", boot_command_line/* 启动参数 在 /var/log/message 中*/);
	boot_cpu_data.x86_phys_bits = MAX_PHYSMEM_BITS;/*  */
#endif

	/*
	 * If we have OLPC OFW, we might end up relocating the fixmap due to
	 * reserve_top(), so do this before touching the ioremap area.
	 * 
	 * 检测系统是否支持 [One Laptop Per Child support](http://wiki.laptop.org/go/OFW_FAQ)
	 */
	olpc_ofw_detect();/* 专为低端机型设计 每个孩子应该有个自己的笔记本??? TODO */

    /**
     *  中断描述符表
     */
	idt_setup_early_traps();/* 中断描述符表 */

    /**
     *  
     */
	early_cpu_init();       /* 收集 `CPU` 和其供应商的信息 */
	arch_init_ideal_nops(); /*  */
	jump_label_init();      /*  */
	static_call_init();     /*  */
	early_ioremap_init();   /* 初始化早期的 `ioremap`(`ioremap` 就是用来把设备内存映射到内核地址空间的) */

	setup_olpc_ofw_pgd();   /*  */

    //获取根设备的主次设备号
    //后面 `initrd` 会通过 `do_mount_root` 函数挂载到这个根设备上
    //其中    主设备号用来识别和这个设备有关的驱动
    //      次设备号用来表示使用该驱动的各设备
	ROOT_DEV = old_decode_dev(boot_params.hdr.root_dev);/*  */

    /**
     *  设置与显示屏有关的参数
     *      目前有行、列，视频页
     */
	screen_info = boot_params.screen_info;
	edid_info = boot_params.edid_info;
#ifdef CONFIG_X86_32
//	apm_info.bios = boot_params.apm_bios_info;
//	ist_info = boot_params.ist_info;
#endif
	saved_video_mode = boot_params.hdr.vid_mode;
	bootloader_type = boot_params.hdr.type_of_loader;
	if ((bootloader_type >> 4) == 0xe) {
		bootloader_type &= 0xf;
		bootloader_type |= (boot_params.hdr.ext_loader_type+0x10) << 4;
	}
	bootloader_version  = bootloader_type & 0xf;
	bootloader_version |= boot_params.hdr.ext_loader_ver << 4;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = boot_params.hdr.ram_size & RAMDISK_IMAGE_START_MASK;
#endif
#ifdef CONFIG_EFI
	if (!strncmp((char *)&boot_params.efi_info.efi_loader_signature,
		     EFI32_LOADER_SIGNATURE, 4)) {
		set_bit(EFI_BOOT, &efi.flags);
	} else if (!strncmp((char *)&boot_params.efi_info.efi_loader_signature,
		     EFI64_LOADER_SIGNATURE, 4)) {
		set_bit(EFI_BOOT, &efi.flags);
		set_bit(EFI_64BIT, &efi.flags);
	}
#endif

	x86_init.oem.arch_setup();  /*  */

    //* /proc/iomem - 提供每个物理设备的系统内存映射地址
    //根据不同属性划分为以十六进制符号表示的一段地址范围
        //cat /proc/iomem 
        //00000000-00000fff : reserved
        //00001000-0009fbff : System RAM
        //0009fc00-0009ffff : reserved
        //000a0000-000bffff : PCI Bus 0000:00
        //000c0000-000c95ff : Video ROM
        //000c9800-000ca5ff : Adapter ROM
        //000ca800-000ccbff : Adapter ROM
        //000f0000-000fffff : reserved
        //000f0000-000fffff : System ROM
        //00100000-bff7ffff : System RAM
        //2a000000-340fffff : Crash kernel
        //bff80000-bfffffff : reserved
        //c0000000-febfffff : PCI Bus 0000:00
    //设置 `iomem` 的结束地址，我们需要这样做
	iomem_resource.end = (1ULL << boot_cpu_data.x86_phys_bits) - 1;

    /**
     *  设置完根 `iomem` 的资源地址范围的结束地址后，下一步就是 设置内存映射
     *
     *  获取内存布局，从 BIOS 中拷贝一份新的 到 e802_table 中
     */
	e820__memory_setup();   /* 设置内存映射 */
    
	parse_setup_data();     /* 解析 */

	copy_edd(); /* Enhanced Disk Drive Specification */

	if (!boot_params.hdr.root_flags)
		root_mountflags &= ~MS_RDONLY;

    /**
     *  在初始化阶段完成内存描述符中内核代码段，数据段和 `brk` 段的初始化
     */
	init_mm.start_code = (unsigned long) _text; /* 开始 代码段 */
	init_mm.end_code = (unsigned long) _etext;  /* 结束 代码段 */
	init_mm.end_data = (unsigned long) _edata;  /* 结束 数据段 */
	init_mm.brk = _brk_end;                     /* brk 结束 */

    /**
     *  在初始化阶段完成内存描述符的设置后，下一步是完成 `Intel` 内存保护扩展的初始化
     * 
     *    $ sudo cat /proc/iomem | grep "Kernel " -B 1
     *
     *    100000000-23fffffff : System RAM
     *      164800000-164f9325e : Kernel code
     *      164f9325f-16556817f : Kernel data
     *      165762000-165a6efff : Kernel bss
     */
	code_resource.start = __pa_symbol(_text);   /* 代码段 */
	code_resource.end = __pa_symbol(_etext)-1;
	rodata_resource.start = __pa_symbol(__start_rodata);    /* 只读区域 */
	rodata_resource.end = __pa_symbol(__end_rodata)-1;
	data_resource.start = __pa_symbol(_sdata);      /* 数据段 */
	data_resource.end = __pa_symbol(_edata)-1;
	bss_resource.start = __pa_symbol(__bss_start);  /* bss 存放静态全局变量的区域 */
	bss_resource.end = __pa_symbol(__bss_stop)-1;

#ifdef CONFIG_CMDLINE_BOOL
#ifdef CONFIG_CMDLINE_OVERRIDE
	strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
#else
	if (builtin_cmdline[0]) {
		/* append boot loader cmdline to builtin */
		strlcat(builtin_cmdline, " ", COMMAND_LINE_SIZE);
		strlcat(builtin_cmdline, boot_command_line, COMMAND_LINE_SIZE);
		strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
	}
#endif
#endif/* 命令行 */

	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;/* BOOT_IMAGE=" ... " */

	/*
	 * x86_configure_nx() is called before parse_early_param() to detect
	 * whether hardware doesn't support NX (so that the early EHCI debug
	 * console setup can safely call set_fixmap()). It may then be called
	 * again from within noexec_setup() during parsing early parameters
	 * to honor the respective command line option.
	 *
	 * `NX` 配置。`NX-bit` 或者 `no-execute` 位是页目录条目的第 63 比特位。
	 * 它的作用是控制被映射的物理页面是否具有执行代码的能力。
     * 这个比特位只会在通过把 `EFER.NXE` 置为1使能 `no-execute` 页保护机制
     * 的时候被使用/设置。在 `x86_configure_nx` 函数中会检查 `CPU` 是否支持
     * `NX-bit`，以及是否被禁用。
     *
     *  see also x86_report_nx()
	 */
	x86_configure_nx(); /* No Exec */

    /* 解析内核命令行并且基于给定的参数创建不同的服务 */
	parse_early_param();    /* 解析 启动 cmdline */

    /**
     *  在这之后所有基于早期命令行参数的服务都已经被创建
     */
    
	if (efi_enabled(EFI_BOOT))  /* 如果是 EFI启动的 */
		efi_memblock_x86_reserve_range();   /*  */
#ifdef CONFIG_MEMORY_HOTPLUG    /* TODO */
	/*
	 * Memory used by the kernel cannot be hot-removed because Linux
	 * cannot migrate the kernel pages. When memory hotplug is
	 * enabled, we should prevent memblock from allocating memory
	 * for the kernel.
	 *
	 * ACPI SRAT records all hotpluggable memory ranges. But before
	 * SRAT is parsed, we don't know about it.
	 *
	 * The kernel image is loaded into memory at very early time. We
	 * cannot prevent this anyway. So on NUMA system, we set any
	 * node the kernel resides in as un-hotpluggable.
	 *
	 * Since on modern servers, one node could have double-digit
	 * gigabytes memory, we can assume the memory around the kernel
	 * image is also un-hotpluggable. So before SRAT is parsed, just
	 * allocate memory near the kernel image to try the best to keep
	 * the kernel away from hotpluggable memory.
	 */
	if (movable_node_is_enabled())  /*  */
		memblock_set_bottom_up(true);
#endif

    /**
     *  使用  `x86_report_nx`函数打印出关于 `NX` 的信息, see also x86_configure_nx()
     *
     * 注意`x86_report_nx` 函数不一定在 `x86_configure_nx` 函数之后调用，但是一定
     * 在 `parse_early_param` 之后调用。答案很简单: 因为内核支持 `noexec` 参数，所
     * 以我们一定在 `parse_early_param` 调用并且解析  `noexec` 参数之后才能调用 
     * `x86_report_nx`
     *
     * # sudo cat /var/log/messages | grep Execute
     *  Mar  2 08:58:32 localhost kernel: NX (Execute Disable) protection: active
     */
	x86_report_nx();    /* No Exec */

	/**
	 *  after early param, so could get panic from serial 
     *  为 `setup_data` 重新映射内存并保留内存块
     */
	memblock_x86_reserve_range_setup_data();

    /**
     *  检查内置的 `MPS` 又称 [多重处理器规范]
     *  
     * 传递给内核的命令行选项中有 `acpi=off`、`acpi=noirq` 或者 `pci=noacpi` 参数,
     * 就会输出警告信息, see parse_acpi()
     *
     * 返回1，表示禁用了本地 [APIC]
     */
	if (acpi_mps_check()) {
#ifdef CONFIG_X86_LOCAL_APIC
		disable_apic = 1;
#endif
        //清除了当前CPU中的 `X86_FEATURE_APIC` 位
		setup_clear_cpu_cap(X86_FEATURE_APIC);
	}

    /**
     *  处理不同内存区域和 `e820` 槽
     */
	e820__reserve_setup_data();     /* e820是和BIOS相关的中断(int 0x15)*/
	e820__finish_early_params();    /*  */

	if (efi_enabled(EFI_BOOT))
		efi_init();

    /**
     *  DMI（直接媒体接口）
     *
     *  收集与 [桌面管理接口] 有关的所有信息(http://en.wikipedia.org/wiki/Desktop_Management_Interface)
     */
	dmi_setup();

	/*
	 * VMware detection requires dmi to be available, so this
	 * needs to be done after dmi_setup(), for the boot CPU.
	 *
	 * 虚拟机
	 */
	init_hypervisor_platform();

    /* Time Stamp Counter */
	tsc_early_init();

    /*  */
	x86_init.resources.probe_roms();    /* probe BIOS roms */

	/* after parse_early_param, so could debug it */
    //+-------------+      +-------------+
    //|    parent   |------|    sibling  |
    //+-------------+      +-------------+
    //       |
    //+-------------+
    //|    child    | 
    //+-------------+
	insert_resource(&iomem_resource, &code_resource);
	insert_resource(&iomem_resource, &rodata_resource);
	insert_resource(&iomem_resource, &data_resource);
	insert_resource(&iomem_resource, &bss_resource);

    /**
     *  将内核 的 _text ~ _end 与 RAM 建立联系
     */
	e820_add_kernel_range(); /*  */

    /**
     *  从 e820 中移除 BIOS   的部分
     */
    trim_bios_range();      /*  */

    
#ifdef CONFIG_X86_32
//	if (ppro_with_ram_bug()) {
//		e820__range_update(0x70000000ULL, 0x40000ULL, E820_TYPE_RAM,
//				  E820_TYPE_RESERVED);
//		e820__update_table(e820_table);
//		printk(KERN_INFO "fixed physical RAM map:\n");
//		e820__print_table("bad_ppro");
//	}
#else
    /**
     *  
     */
	early_gart_iommu_check();   /*  */
#endif

	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	max_pfn = e820__end_of_ram_pfn();   /*  */

	/**
	 *  update e820 for memory not covered by WB MTRRs 
	 *
	 *  在 启动的 CPU 初始化 MTRR
	 */
	mtrr_bp_init(); /*  */

    /**
     *  
     */
	if (mtrr_trim_uncached_memory(max_pfn))
		max_pfn = e820__end_of_ram_pfn();

    /**
     *  
     */
	max_possible_pfn = max_pfn; /*  */

	/*
	 * This call is required when the CPU does not support PAT. If
	 * mtrr_bp_init() invoked it already via pat_init() the call has no
	 * effect.
	 */
	init_cache_modes(); /*  */

	/*
	 * Define random base addresses for memory sections after max_pfn is
	 * defined and before each memory section base is used.
	 *
	 * 随机化 内存 节 的 基址 - ASLR 地址空间布局随机化
	 */
	kernel_randomize_memory();  /*  */

#ifdef CONFIG_X86_32
	/* max_low_pfn get updated here */
//	find_low_pfn_range();   /*  */
#else

    /* 我估计这是 CPU 内部集成的APIC吧 - 荣涛 2021年7月1日 */
	check_x2apic(); /* APIC */

	/* How many end-of-memory variables you have, grandma! */
	/* need this before calling reserve_initrd */
	if (max_pfn > (1UL<<(32 - PAGE_SHIFT)))
		max_low_pfn = e820__end_of_low_ram_pfn();
	else
		max_low_pfn = max_pfn;

    /**
     *  
     */
	high_memory = (void *)__va(max_pfn * PAGE_SIZE - 1) + 1;
    
#endif

	/*
	 * Find and reserve possible boot-time SMP configuration:
	 *
	 * 解析 [SMP](http://en.wikipedia.org/wiki/Symmetric_multiprocessing) 的配置信息
	 */
	find_smp_config();

    /**
     *  
     */
	reserve_ibft_region();

    /**
     *  在早期阶段分配页表缓冲区
     *
     *  在 brk 区域 申请 6页
     */
	early_alloc_pgt_buf();

	/*
	 * Need to conclude brk, before e820__memblock_setup()
	 *  it could use memblock_find_in_range, could overlap with
	 *  brk area.
	 *
	 * 因为我们之前已经创建好了页面缓冲区，所以现在我们使用 `reserve_brk` 函数为 `brk` 区段保留内存块
	 */
	reserve_brk();

    /**
     *  释放内核映射中越界的内存区域
     */
	cleanup_highmap();

    /**
     *  为 `memblock` 分配内存设置一个界限
     *  这个界限可以是 `ISA_END_ADDRESS` 或者 `0x100000` 
     */
	memblock_set_current_limit(ISA_END_ADDRESS);

    /**
     *  将 e820 物理内存添加至 memblock
     *
     *  下面是 e820 table 格式
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
	e820__memblock_setup();

    /**
     *  
     */
	reserve_bios_regions(); /* 在这之后我们就在 `.meminit.data` 区段拥有了为扩展BIOS数据区域预留的第一个  `memblock` */

    /**
     *  
     */
	efi_fake_memmap();
	efi_find_mirror();
	efi_esrt_init();
	efi_mokvar_table_init();

	/*
	 * The EFI specification says that boot service code won't be
	 * called after ExitBootServices(). This is, in fact, a lie.
	 */
	efi_reserve_boot_services();

	/* preallocate 4k for mptable mpc */
	e820__memblock_alloc_reserved_mpc_new();

#ifdef CONFIG_X86_CHECK_BIOS_CORRUPTION
	setup_bios_corruption_check();
#endif

#ifdef CONFIG_X86_32
//	printk(KERN_DEBUG "initial memory mapped: [mem 0x00000000-%#010lx]\n",
//			(max_pfn_mapped<<PAGE_SHIFT) - 1);
#endif

    /**
     *  用于保留从 `0x0` 到1M的低端内存用作到实模式的跳板(用于重启等...)
     */
	reserve_real_mode();

    /**
     * 用于清除掉以 `0x20050000`, `0x20110000` 等地址开头的内存空间
     *
     * 这些内存区域必须被排除在外，因为 [Sandy Bridge](http://en.wikipedia.org/wiki/Sandy_Bridge) 
     * 会在这些内存区域出现一些问题
     */
	trim_platform_memory_ranges();

    /**
     *  用于保留 `memblock` 中的前4KB页面
     */
	trim_low_memory_range();

    /**
     *  用于在 `PAGE_OFFSET` 处重建物理内存的直接映射
     */
	init_mem_mapping(); /*  */

    /**
     *  用于建立 `#PF` 处理函数 (Page Fault)
     */
	idt_setup_early_pf();

	/*
	 * Update mmu_cr4_features (and, indirectly, trampoline_cr4_features)
	 * with the current CR4 value.  This may not be necessary, but
	 * auditing all the early-boot CR4 manipulation would be needed to
	 * rule it out.
	 *
	 * Mask off features that don't work outside long mode (just
	 * PCIDE for now).
	 *
	 * CR4
	 *  <Control Registers(CR0, CR1, CR2, CR3, and CR4)>
	 *  https://rtoax.blog.csdn.net/article/details/118164142
	 */
	mmu_cr4_features = __read_cr4() & ~X86_CR4_PCIDE;

    /**
     *  为 `memblock` 分配内存设置一个界限
     */
	memblock_set_current_limit(get_max_mapped());

	/*
	 * NOTE: On x86-32, only from this point on, fixmaps are ready for use.
	 */

#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	if (init_ohci1394_dma_early)
		init_ohci1394_dma_on_all_controllers();
#endif
    
	/* Allocate bigger log buffer 日志 buffer 大小 */
	setup_log_buf(1);   /*  */

	if (efi_enabled(EFI_BOOT)) {
		switch (boot_params.secure_boot) {
		case efi_secureboot_mode_disabled:
			pr_info("Secure boot disabled\n");
			break;
		case efi_secureboot_mode_enabled:
			pr_info("Secure boot enabled\n");
			break;
		default:
			pr_info("Secure boot could not be determined\n");
			break;
		}
	}
    /**
     *  Linux初始RAM磁盘(initrd)是在系统引导过程中挂载的一个临时根文件系统，用来支持两阶段的引导过程。
     *  根文件系统就是通过这方式来进行初始化, 此函数获取RAM DISK的基地址以及大小以及大小加偏移
     *
     *  Ram Disk - initRD - initrd
     */
	reserve_initrd();

    /**
     *  
     */
	acpi_table_upgrade();   /* 电源管理 */

    /**
     *  VSMP - Virtual Symmetric Multiprocessing ??[No**]
     *  VSMP - Versatile SMP 多功能SMP ??[No**]
     *  VSMP - ScaleMP vSMP [Yes**]: techniques of parallel computing, TODO  2021年3月22日
     */
	vsmp_init();

    /**
     *  This function allows to override  default I/O delay `0x80` port
     */
	io_delay_init();    /*  */

    /**
     *  quirks: 怪癖
     */
	early_platform_quirks();

	/*
	 * Parse the ACPI tables for possible boot-time SMP configuration.
	 * we will not cover [ACPI] 在内核初始化部分，TODO 2021年3月22日
	 */
	acpi_boot_table_init();

    /**
     *  
     */
	early_acpi_boot_init();

    /**
     *  
     */
	initmem_init(); /* x86_numa_init(); */

    /**
     *  Allocate area for DMA
     *  `DMA` is a special mode when devices communicate with memory without CPU
     *
     *  reserve area(s) for contiguous memory handling
     */
	dma_contiguous_reserve(max_pfn_mapped << PAGE_SHIFT);

    /*  */
	if (boot_cpu_has(X86_FEATURE_GBPAGES))
		hugetlb_cma_reserve(PUD_SHIFT - PAGE_SHIFT);

	/*
	 * Reserve memory for crash kernel after SRAT is parsed so that it
	 * won't consume hotpluggable memory.
	 */
	reserve_crashkernel();

	memblock_find_dma_reserve();

	if (!early_xdbc_setup_hardware())
		early_xdbc_register_console();

    /**
     *  Initialization of the sparse(稀疏) memory
     *
     *  struct x86_init_ops __initdata x86_init  = {
     *     ...
     *     .paging = {
     *      .pagetable_init		= native_pagetable_init,
     *     },
     *  ...
     *  };
     *  #define native_pagetable_init        paging_init
     *
     * The `paging_init` function initializes sparse memory and zone sizes
     */
	x86_init.paging.pagetable_init(); //== paging_init()

    /* Kernel Address Sanitizer,动态检测内存错误 */
	kasan_init();

	/*
	 * Sync back kernel address range.
	 *
	 * FIXME: Can the later sync in setup_cpu_entry_areas() replace
	 * this call?
	 */
	sync_initial_page_table();

	tboot_probe();

    /**
     *  vsyscall mapping 
     *  映射 vsyscall
     */
	map_vsyscall();

    /**
     *  x86-64 为 空
     */
	generic_apic_probe();

    /**
     *  
     * quirks: 怪异的性格(或行为); 怪癖; (尤指偶发的)怪事，奇事;
     */
	early_quirks();

	/*
	 * Read APIC and some other early information from ACPI tables.
	 */
	acpi_boot_init();
	sfi_init();
	x86_dtb_init();

	/*
	 * Getting the SMP configuration 对称多处理
	 * get boot-time SMP configuration:
	 */
	get_smp_config();

	/*
	 * Systems w/o ACPI and mptables might not have it mapped the local
	 * APIC yet, but prefill_possible_map() might need to access it.
	 *
	 * sets the address of the local [APIC]
	 */
	init_apic_mappings();   /* 中断控制器 映射 */

    /**
     *  makes preliminary(初步的) filling of the possible CPU's `cpumask`
     */ 
	prefill_possible_map(); /*  */

    /**
     *
     */ 
	init_cpu_to_node(); /*  */

    /**
     *
     */ 
	init_gi_nodes();    /* 通用启动器 */

    /**
     *
     */ 
	io_apic_init_mappings();    /*  */

    /* 
        初始值: x86_init_noop()
        后期可能为: xen_pv_guest_late_init()
    */
	x86_init.hyper.guest_late_init();

    /**
     *  E820 内存管理器 
     *  memblock 添加到 iomem_resource
     */
	e820__reserve_resources();  /*  */

    /**
     *  
     */
	e820__register_nosave_regions(max_pfn);

    /**
     *  reserve standard I/O resources like `DMA`, `TIMER`, `FPU`, etc
     * 
     *  实际调用: reserve_standard_io_resources()
     *
     *  ioport_resource <=(添加)==- standard_io_resources 
     */
	x86_init.resources.reserve_resources();

    /**
     *  
     */
	e820__setup_pci_gap();

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	if (!efi_enabled(EFI_BOOT) || (efi_mem_type(0xa0000) != EFI_CONVENTIONAL_MEMORY))
		conswitchp = &vga_con;
#endif
#endif

    /**
     *  
     */
	x86_init.oem.banner();  /* 内核+编译器 版本 */

    /**
     *  
     */
	x86_init.timers.wallclock_init();

    //initializes `Machine check Exception`
	mcheck_init();  /*  */

    //registers [jiffy]
	register_refined_jiffies(CLOCK_TICK_RATE/* The clock frequency of the i8253/i8254 PIT */);

#ifdef CONFIG_EFI
	if (efi_enabled(EFI_BOOT))      /*  */
		efi_apply_memmap_quirks();  /*  */
#endif

    /* 回溯 */
	unwind_init();  /*  */
}

#ifdef CONFIG_X86_32

//static struct resource video_ram_resource = {
//	.name	= "Video RAM area",
//	.start	= 0xa0000,
//	.end	= 0xbffff,
//	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
//};
//
//void __init i386_reserve_resources(void)
//{
//	request_resource(&iomem_resource, &video_ram_resource);
//	reserve_standard_io_resources();
//}

#endif /* CONFIG_X86_32 */

static struct notifier_block kernel_offset_notifier = {
	.notifier_call = dump_kernel_offset
};

static int __init register_kernel_offset_dumper(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
					&kernel_offset_notifier);
	return 0;
}
__initcall(register_kernel_offset_dumper);
