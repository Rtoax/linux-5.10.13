/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ld script for the x86 kernel
 *
 * Historic 32-bit version written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 * Modernisation, unification and other changes and fixes:
 *   Copyright (C) 2007-2009  Sam Ravnborg <sam@ravnborg.org>
 *
 *
 * Don't define absolute symbols until and unless you know that symbol
 * value is should remain constant even if kernel image is relocated
 * at run time. Absolute symbols are not relocated. If symbol value should
 * change if kernel is relocated, make the symbol section relative and
 * put it inside the section definition.
 */

#ifdef CONFIG_X86_32
//#define LOAD_OFFSET __PAGE_OFFSET
#else
#define LOAD_OFFSET __START_KERNEL_map  /* kernel 起始地址 */
#endif

#define RUNTIME_DISCARD_EXIT
#define EMITS_PT_NOTE
#define RO_EXCEPTION_TABLE_ALIGN	16

#include <asm-generic/vmlinux.lds.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/page_types.h>
#include <asm/orc_lookup.h>
#include <asm/cache.h>
#include <asm/boot.h>

#undef i386     /* in case the preprocessor is a 32bit one */

OUTPUT_FORMAT(CONFIG_OUTPUT_FORMAT)

#ifdef CONFIG_X86_32
//OUTPUT_ARCH(i386)
//ENTRY(phys_startup_32)
#else
OUTPUT_ARCH(i386:x86-64)
ENTRY(phys_startup_64)  /* 物理内存起点 0x1000000 */
#endif

jiffies = jiffies_64;

#if defined(CONFIG_X86_64)
/*
 * On 64-bit, align RODATA to 2MB so we retain large page mappings for
 * boundaries spanning kernel text, rodata and data sections.
 *
 * However, kernel identity mappings will have different RWX permissions
 * to the pages mapping to text and to the pages padding (which are freed) the
 * text section. Hence kernel identity mappings will be broken to smaller
 * pages. For 64-bit, kernel text and kernel identity mappings are different,
 * so we can enable protection checks as well as retain 2MB large page
 * mappings for kernel text.
 */
#define X86_ALIGN_RODATA_BEGIN	. = ALIGN(HPAGE_SIZE);

#define X86_ALIGN_RODATA_END					\
		. = ALIGN(HPAGE_SIZE);				\
		__end_rodata_hpage_align = .;			\
		__end_rodata_aligned = .;

#define ALIGN_ENTRY_TEXT_BEGIN	. = ALIGN(PMD_SIZE);
#define ALIGN_ENTRY_TEXT_END	. = ALIGN(PMD_SIZE);

/*
 * This section contains data which will be mapped as decrypted. Memory
 * encryption operates on a page basis. Make this section PMD-aligned
 * to avoid splitting the pages while mapping the section early.
 *
 * Note: We use a separate section so that only this section gets
 * decrypted to avoid exposing more than we wish.
 */
#define BSS_DECRYPTED						\
	. = ALIGN(PMD_SIZE);					\
	__start_bss_decrypted = .;				\
	*(.bss..decrypted);					\
	. = ALIGN(PAGE_SIZE);					\
	__start_bss_decrypted_unused = .;			\
	. = ALIGN(PMD_SIZE);					\
	__end_bss_decrypted = .;				\

#else

#endif

PHDRS {
	text PT_LOAD FLAGS(5);          /* R_E */
	data PT_LOAD FLAGS(6);          /* RW_ */
#ifdef CONFIG_X86_64
#ifdef CONFIG_SMP
	percpu PT_LOAD FLAGS(6);        /* RW_ */
#endif
	init PT_LOAD FLAGS(7);          /* RWE */
#endif
	note PT_NOTE FLAGS(0);          /* ___ */
}

SECTIONS
{
#ifdef CONFIG_X86_32

#else
    /* 0xffffffff81000000 */
	. = __START_KERNEL; /* __START_KERNEL = 0xffffffff81000000 = 0xffffffff80000000 + 0x1000000 */

    /* 0x1000000 */
	phys_startup_64 = ABSOLUTE(startup_64 - LOAD_OFFSET);
    /* phys_startup_64 = 0x1000000 = ffffffff81000000 - 0xffffffff80000000 */
#endif

	/* Text and read-only data  代码段  */
    /* ffffffff81000000 - ffffffff82202137 */
	.text :  AT(ADDR(.text) - LOAD_OFFSET) {
		_text = .;  /* ffffffff81000000 */
		_stext = .; /* ffffffff81000000 */

		/* bootstrapping code */
        /* ffffffff81000000 */

		HEAD_TEXT   /* __HEAD  */
		TEXT_TEXT

        /* ffffffff81ef5fb0 - ffffffff81efde9c */
		SCHED_TEXT  /* __sched */

        /* ffffffff81efdea0 - ffffffff81efe7f8 */
		CPUIDLE_TEXT/* __cpuidle */

        /* ffffffff81efe7f8 - ffffffff81eff401 */
		LOCK_TEXT   /* __lockfunc */

        /* ffffffff81eff408 - ffffffff81eff408 */
		KPROBES_TEXT/* __kprobes */

		ALIGN_ENTRY_TEXT_BEGIN

        /* ffffffff82000000 - ffffffff820017de */
		ENTRY_TEXT              /* arch/x86/entry/entry_64.S */

		ALIGN_ENTRY_TEXT_END

        /* ffffffff82200000 - ffffffff822003d6 */
		SOFTIRQENTRY_TEXT       /* __softirq_entry */

        /* ffffffff822003d8 - ffffffff822018fd */
		STATIC_CALL_TEXT        /* __ARCH_DEFINE_STATIC_CALL_TRAMP static_call */


		*(.fixup)           /* entry_64.S   emulate.c copy_user_64.S */
		*(.gnu.warning)

#ifdef CONFIG_RETPOLINE
		__indirect_thunk_start = .; /* ffffffff82201f56 */
		*(.text.__x86.indirect_thunk)   /* retpoline.S */
		__indirect_thunk_end = .;   /* ffffffff82202137 */
#endif
	} :text =0xcccc

	/* End of text section, which should occupy whole number of pages */
	_etext = .; /* ffffffff82202137 */

	. = ALIGN(PAGE_SIZE/* 4K */);

	X86_ALIGN_RODATA_BEGIN

    /* ffffffff82400000 - ffffffff82b94000 */
	RO_DATA(PAGE_SIZE)      /* 只读 数据段 */

	X86_ALIGN_RODATA_END    /* ffffffff82c00000 */

	/* Data *//* 数据段 */
    /* ffffffff82c00000 - ffffffff83675580 */
	.data : AT(ADDR(.data) - LOAD_OFFSET)
	{
		/* Start of data section */
		_sdata = .; /* ffffffff82c00000 */

		/**     init_task 第一个进程
		 *
         *  ffffffff82c00000 D init_stack
         *  ffffffff82c00000 D init_thread_union
         *
         *
         *
         *
         */
        /* ffffffff82c00000 - ffffffff82c08000 共 32K 大小 */
		INIT_TASK_DATA(THREAD_SIZE/* 32K, 用于对齐 */)

        /**
         *  ffffffff82c08000 d bringup_idt_table
         *  ffffffff82c09000 D __vsyscall_page
         *  ...
         *  ffffffff82c0d380 D vm_node_stat
         *  ffffffff82c0d4c0 D vm_numa_stat
         *  ffffffff82c0d500 D vm_zone_stat
         *  ...
         *  ffffffff82c1c100 D init_task
         *  ffffffff82c1e5a0 d init_sighand
         *  ffffffff82c1ede0 d init_signals
         *  ...
         *  ffffffff836249c0 D initial_code
         *  ffffffff836249c8 D initial_gs
         *  ffffffff836249d0 D initial_vc_handler
         *  ffffffff836249d8 D initial_stack
         *  ...
         *  ffffffff836664e0 D node_data    (struct pglist_data *)
         *  ...
         *
         *
         *
         *
         */

#ifdef CONFIG_X86_32
		/* 32 bit has nosave before _edata */
		NOSAVE_DATA
#endif
		PAGE_ALIGNED_DATA(PAGE_SIZE)

		CACHELINE_ALIGNED_DATA(L1_CACHE_BYTES)

        /*  -  */
		DATA_DATA


		CONSTRUCTORS    /* 构造函数 *//* 将C++的构造函数指针段合并到这里 */

		/* rarely changed data like cpu maps */
		READ_MOSTLY_DATA(INTERNODE_CACHE_BYTES)

		/* End of data section */
		_edata = .; /* ffffffff83675580 */
	} :data

    /* ffffffff83675580 - ffffffff836974b0 */
	BUG_TABLE

    /* ffffffff836974b0 - ffffffff83bda604 */
	ORC_UNWIND_TABLE

	. = ALIGN(PAGE_SIZE);

    /* ffffffff83bdb000 */
	__vvar_page = .;

	.vvar : AT(ADDR(.vvar) - LOAD_OFFSET) {
		/* work around gold bug 13023 */
        /* ffffffff83bdb000 */
		__vvar_beginning_hack = .;

		/* Place all vvars at the offsets in asm/vvar.h. */
#define EMIT_VVAR(name, offset)				\
		. = __vvar_beginning_hack + offset;	\
		*(.vvar_ ## name)

#include <asm/vvar.h>
		/**
		 * DECLARE_VVAR(128, struct vdso_data, _vdso_data)
		 * 展开为：
		 * . = __vvar_beginning_hack + 128
		 * *(.vvar__vdso_data)
		 */
		. = __vvar_beginning_hack + 128;
		*(.vvar__vdso_data)
        /**
         *  ffffffff83bdb080 D _vdso_data
         */
#undef EMIT_VVAR

		/*
		 * Pad the rest of the page with zeros.  Otherwise the loader
		 * can leave garbage here.
		 */
		. = __vvar_beginning_hack + PAGE_SIZE;
	} :data

	. = ALIGN(__vvar_page + PAGE_SIZE, PAGE_SIZE);

	/* Init code and data - will be freed after init */
	. = ALIGN(PAGE_SIZE);
	.init.begin : AT(ADDR(.init.begin) - LOAD_OFFSET) {

        /* ffffffff83bdc000 D __init_begin */
		__init_begin = .; /* paired with __init_end */
	}

#if defined(CONFIG_X86_64) && defined(CONFIG_SMP)
	/*
	 * percpu offsets are zero-based on SMP.  PERCPU_VADDR() changes the
	 * output PHDR, so the next output section - .init.text - should
	 * start another segment - init.
	 */
	/* include/linux/percpu-defs.h __percpu */
    /* ffffffff83bdc000 -  */
	PERCPU_VADDR(INTERNODE_CACHE_BYTES, 0, :percpu)
	ASSERT(SIZEOF(.data..percpu)/* 0x36000 */ < CONFIG_PHYSICAL_START/* 0x1000000 */,
	            "per-CPU data too large - increase CONFIG_PHYSICAL_START")
#endif

    /* TODO rtoax 2021年3月12日 */
    /* ffffffff83c12000 - ffffffff83cb7eb1 */
    /**
     *  ffffffff83c12000 T _sinittext
     *  ...
     *  ffffffff83c12580 T x86_64_start_reservations
     *  ffffffff83c125a0 T x86_64_start_kernel
     *  ffffffff83c1262c T reserve_bios_regions
     *  ffffffff83c126b9 T x86_early_init_platform_quirks
     *  ...
     *  ffffffff83c127e2 t debug_kernel
     *  ffffffff83c12800 t quiet_kernel
     *  ffffffff83c1281e t init_setup
     *  ffffffff83c1285b t rdinit_setup
     *  ffffffff83c12898 t ignore_unknown_bootoption
     *  ffffffff83c128a0 t do_early_param
     *  ...
     *  ffffffff83c12de8 t initcall_blacklist
     *  ffffffff83c12f8f t setup_command_line
     *  ffffffff83c13015 T parse_early_options
     *  ffffffff83c1303d T parse_early_param
     *  ffffffff83c13086 W pgtable_cache_init
     *  ffffffff83c1308c t mm_init
     *  ffffffff83c13132 W arch_call_rest_init
     *  ffffffff83c13142 W smp_setup_processor_id
     *  ffffffff83c13148 W thread_stack_cache_init
     *  ffffffff83c1314e T start_kernel
     *  ...
     *  ffffffff83c25190 T init_IRQ
     *  ...
     *  ffffffff83c27c38 t pci_iommu_init
     *  ffffffff83c27c95 t iommu_setup
     *  ffffffff83c27f33 T pci_iommu_alloc
     *  ...
     *  ffffffff83c2800b t topology_init
     *  ...
     *  ffffffff83c28aa9 T tsc_early_init
     *  ffffffff83c28b31 T tsc_init
     *  ...
     *  ffffffff83c2af00 T early_cpu_init
     *  ...
     *  ffffffff83c2d1b3 T mcheck_init
     *  ...
     *  ffffffff83c323ce T acpi_boot_init
     *  ...
     *  ffffffff83c32c2d t reboot_init
     *  ...
     *  ffffffff83c46e37 t numa_setup
     *  ...
     *  ffffffff83c47caa t numa_init
     *  ffffffff83c480d2 T x86_numa_init
     *  ...
     *  ...
     *  ...
     *  ...
     *  ...
     *  ffffffff83cb7eb1 T _einittext
     */
	{INIT_TEXT_SECTION(PAGE_SIZE);}/*开头的 { 和结尾的 ;} 是我加的*/
#ifdef CONFIG_X86_64
	:init
#endif

    /** 还不知道这个放在那里
     *
     *  ffffffff83eca000   empty_zero_page -> arch/x86/include/asm/pgtable.h
     *                           ZERO_PAGE -> arch/x86/include/asm/pgtable.h
     *  ffffffff83ecb000   dummy_mapping
     *  ...
     *  ffffffff83ecc000  level3_user_vsyscall
     *  ffffffff83ecd000  idt_table
     *  ffffffff83ece000  espfix_pud_page
     *  ffffffff83ecf000  bm_pte
     *  ...
     *  ffffffff83ed6240  ROOT_DEV
     *  ...
     *  ffffffff83edffe0  cpu_devs
     *  ...
     *  ffffffff83f4c1f8  efi_setup
     *  ...
     *  ffffffff83f4cd00  mm_cachep
     *  ffffffff83f4cd40  vm_area_cachep
     *  ffffffff83f4cd80  fs_cachep
     *  ffffffff83f4cdc0  files_cachep
     *  ffffffff83f4ce00  sighand_cachep
     *  ffffffff83f4ce40  signal_cachep
     *  ffffffff83f4ce80  task_struct_cachep
     *  ...
     *  (ffffffff83f54c40)  rootdir
     *
     */

	/*
	 * Section for code used exclusively before alternatives are run. All
	 * references to such code must be patched out by alternatives, normally
	 * by using X86_FEATURE_ALWAYS CPU feature bit.
	 *
	 * See static_cpu_has() for an example.
	 */
	.altinstr_aux : AT(ADDR(.altinstr_aux) - LOAD_OFFSET) {
		*(.altinstr_aux)    /* arch/x86/include/asm/cpufeature.h _static_cpu_has */
	}

    /**
     *
     */
	INIT_DATA_SECTION(16)

	.x86_cpu_dev.init : AT(ADDR(.x86_cpu_dev.init) - LOAD_OFFSET) {
		__x86_cpu_dev_start = .;
		*(.x86_cpu_dev.init)
		__x86_cpu_dev_end = .;
	}

#ifdef CONFIG_X86_INTEL_MID
	.x86_intel_mid_dev.init : AT(ADDR(.x86_intel_mid_dev.init) - \
								LOAD_OFFSET) {
		__x86_intel_mid_dev_start = .;
		*(.x86_intel_mid_dev.init)
		__x86_intel_mid_dev_end = .;
	}
#endif

	/*
	 * start address and size of operations which during runtime
	 * can be patched with virtualization friendly instructions or
	 * baremetal native ones. Think page table operations.
	 * Details in paravirt_types.h
	 */
	. = ALIGN(8);
	.parainstructions : AT(ADDR(.parainstructions) - LOAD_OFFSET) {
		__parainstructions = .;
		*(.parainstructions)
		__parainstructions_end = .;
	}

	/*
	 * struct alt_inst entries. From the header (alternative.h):
	 * "Alternative instructions for different CPU types or capabilities"
	 * Think locking instructions on spinlocks.
	 */
	. = ALIGN(8);
	.altinstructions : AT(ADDR(.altinstructions) - LOAD_OFFSET) {
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}

	/*
	 * And here are the replacement instructions. The linker sticks
	 * them as binary blobs. The .altinstructions has enough data to
	 * get the address and the length of them to patch the kernel safely.
	 */
	.altinstr_replacement : AT(ADDR(.altinstr_replacement) - LOAD_OFFSET) {
		*(.altinstr_replacement)
	}

	/*
	 * struct iommu_table_entry entries are injected in this section.
	 * It is an array of IOMMUs which during run time gets sorted depending
	 * on its dependency order. After rootfs_initcall is complete
	 * this section can be safely removed.
	 */
	.iommu_table : AT(ADDR(.iommu_table) - LOAD_OFFSET) {
		__iommu_table = .;
		*(.iommu_table)
		__iommu_table_end = .;
	}

	. = ALIGN(8);
	.apicdrivers : AT(ADDR(.apicdrivers) - LOAD_OFFSET) {
		__apicdrivers = .;
		*(.apicdrivers);
		__apicdrivers_end = .;
	}

	. = ALIGN(8);
	/*
	 * .exit.text is discarded at runtime, not link time, to deal with
	 *  references from .altinstructions
	 */
	.exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET) {
		EXIT_TEXT
	}

	.exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET) {
		EXIT_DATA
	}

#if !defined(CONFIG_X86_64) || !defined(CONFIG_SMP)
	PERCPU_SECTION(INTERNODE_CACHE_BYTES)
#endif

	. = ALIGN(PAGE_SIZE);

	/* freed after init ends here */
	.init.end : AT(ADDR(.init.end) - LOAD_OFFSET) {
		__init_end = .;
	}

	/*
	 * smp_locks might be freed after init
	 * start/end must be page aligned
	 */
	. = ALIGN(PAGE_SIZE);
	.smp_locks : AT(ADDR(.smp_locks) - LOAD_OFFSET) {
		__smp_locks = .;
		*(.smp_locks)
		. = ALIGN(PAGE_SIZE);
		__smp_locks_end = .;
	}

#ifdef CONFIG_X86_64
	.data_nosave : AT(ADDR(.data_nosave) - LOAD_OFFSET) {
		NOSAVE_DATA
	}
#endif

	/* BSS */
	. = ALIGN(PAGE_SIZE);
	.bss : AT(ADDR(.bss) - LOAD_OFFSET) {
		__bss_start = .;
		*(.bss..page_aligned)
		. = ALIGN(PAGE_SIZE);
		*(BSS_MAIN)
		BSS_DECRYPTED
		. = ALIGN(PAGE_SIZE);
		__bss_stop = .;
	}

	/*
	 * The memory occupied from _text to here, __end_of_kernel_reserve, is
	 * automatically reserved in setup_arch(). Anything after here must be
	 * explicitly reserved using memblock_reserve() or it will be discarded
	 * and treated as available memory.
	 */
	__end_of_kernel_reserve = .;

	. = ALIGN(PAGE_SIZE);
	.brk : AT(ADDR(.brk) - LOAD_OFFSET) {
		__brk_base = .;
		. += 64 * 1024;		/* 64k alignment slop space */
		*(.brk_reservation)	/* areas brk users have reserved */
		__brk_limit = .;
	}

	. = ALIGN(PAGE_SIZE);		/* keep VO_INIT_SIZE page aligned */
	_end = .;

#ifdef CONFIG_AMD_MEM_ENCRYPT
	/*
	 * Early scratch/workarea section: Lives outside of the kernel proper
	 * (_text - _end).
	 *
	 * Resides after _end because even though the .brk section is after
	 * __end_of_kernel_reserve, the .brk section is later reserved as a
	 * part of the kernel. Since it is located after __end_of_kernel_reserve
	 * it will be discarded and become part of the available memory. As
	 * such, it can only be used by very early boot code and must not be
	 * needed afterwards.
	 *
	 * Currently used by SME for performing in-place encryption of the
	 * kernel during boot. Resides on a 2MB boundary to simplify the
	 * pagetable setup used for SME in-place encryption.
	 */
	. = ALIGN(HPAGE_SIZE);
	.init.scratch : AT(ADDR(.init.scratch) - LOAD_OFFSET) {
		__init_scratch_begin = .;
		*(.init.scratch)
		. = ALIGN(HPAGE_SIZE);
		__init_scratch_end = .;
	}
#endif

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	DISCARDS

	/*
	 * Make sure that the .got.plt is either completely empty or it
	 * contains only the lazy dispatch entries.
	 */
	.got.plt (INFO) : { *(.got.plt) }
	ASSERT(SIZEOF(.got.plt) == 0 ||
#ifdef CONFIG_X86_64
	       SIZEOF(.got.plt) == 0x18,
#else
//	       SIZEOF(.got.plt) == 0xc,
#endif
	       "Unexpected GOT/PLT entries detected!")

	/*
	 * Sections that should stay zero sized, which is safer to
	 * explicitly check instead of blindly discarding.
	 */
	.got : {
		*(.got) *(.igot.*)
	}
	ASSERT(SIZEOF(.got) == 0, "Unexpected GOT entries detected!")

	.plt : {
		*(.plt) *(.plt.*) *(.iplt)
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

#ifdef CONFIG_X86_32
///*
// * The ASSERT() sink to . is intentional, for binutils 2.14 compatibility:
// */
//. = ASSERT((_end - LOAD_OFFSET <= KERNEL_IMAGE_SIZE),
//	   "kernel image bigger than KERNEL_IMAGE_SIZE");
#else
/*
 * Per-cpu symbols which need to be offset from __per_cpu_load
 * for the boot processor.
 */
#define INIT_PER_CPU(x) init_per_cpu__##x = ABSOLUTE(x) + __per_cpu_load
INIT_PER_CPU(gdt_page);
INIT_PER_CPU(fixed_percpu_data);
INIT_PER_CPU(irq_stack_backing_store);

/*
 * Build-time check on the image size:
 */
. = ASSERT((_end - _text <= KERNEL_IMAGE_SIZE),
	   "kernel image bigger than KERNEL_IMAGE_SIZE");

#ifdef CONFIG_SMP
. = ASSERT((fixed_percpu_data == 0),
           "fixed_percpu_data is not at start of per-cpu area");
#endif

#endif /* CONFIG_X86_32 */

#ifdef CONFIG_KEXEC_CORE
#include <asm/kexec.h>

. = ASSERT(kexec_control_code_size <= KEXEC_CONTROL_CODE_MAX_SIZE,
           "kexec control code size is too big");
#endif

