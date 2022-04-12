// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/mmzone.h>
#include <linux/proc_fs.h>
#include <linux/percpu.h>
#include <linux/seq_file.h>
#include <linux/swap.h>
#include <linux/vmstat.h>
#include <linux/atomic.h>
#include <linux/vmalloc.h>
#ifdef CONFIG_CMA
#include <linux/cma.h>
#endif
#include <asm/page.h>
#include "internal.h"

void __attribute__((weak)) arch_report_meminfo(struct seq_file *m)
{
}

static void show_val_kb(struct seq_file *m, const char *s, unsigned long num)
{
	seq_put_decimal_ull_width(m, s, num << (PAGE_SHIFT - 10), 8);
	seq_write(m, " kB\n", 4);
}

/**
 *  /proc/meminfo
 */
static int meminfo_proc_show(struct seq_file *m, void *v)   /* /proc/meminfo */
{
	struct sysinfo i;
	unsigned long committed;
	long cached;
	long available;
	unsigned long pages[NR_LRU_LISTS];
	unsigned long sreclaimable, sunreclaim;
	int lru;

	si_meminfo(&i);
	si_swapinfo(&i);
	committed = vm_memory_committed();

	cached = global_node_page_state(NR_FILE_PAGES) - total_swapcache_pages() - i.bufferram;
	if (cached < 0)
		cached = 0;

	for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
		pages[lru] = global_node_page_state(NR_LRU_BASE + lru);

	available = si_mem_available();
	sreclaimable = global_node_page_state_pages(NR_SLAB_RECLAIMABLE_B);
	sunreclaim = global_node_page_state_pages(NR_SLAB_UNRECLAIMABLE_B);

    /**
     *  物理内存总量
     *
     *[rongtao@toa linux-5.10.13]$ cat /proc/meminfo
     *MemTotal:         998660 kB
     *[rongtao@toa linux-5.10.13]$ dmesg | grep reserved
     *[    0.000000] Memory: 967124k/1048576k available
     *                        (6916k kernel code, 524k absent, 80928k reserved, 4551k data, 1800k init)
     *[rongtao@toa linux-5.10.13]$ dmesg | grep Freeing
     *[    3.143139] Freeing unused kernel memory: 1800k freed
     *
     * (MemTotal:998660 kB) + (80928k reserved - 1800k freed) =
     */
	show_val_kb(m, "MemTotal:       ", i.totalram);
    /**
     *  剩余空闲物理内存
     */
	show_val_kb(m, "MemFree:        ", i.freeram);
    /**
     *  可使用的页面数量
     */
	show_val_kb(m, "MemAvailable:   ", available);
    /**
     *  用于块层的缓存
     */
	show_val_kb(m, "Buffers:        ", i.bufferram);
    /**
     *  用于页面高速缓存
     */
	show_val_kb(m, "Cached:         ", cached);
    /**
     *  交换缓存的数量
     */
	show_val_kb(m, "SwapCached:     ", total_swapcache_pages());
    /**
     *  活跃的匿名页面+活跃的文件映射页面
     */
	show_val_kb(m, "Active:         ", pages[LRU_ACTIVE_ANON] + pages[LRU_ACTIVE_FILE]);
    /**
     *  不活跃的
     */
	show_val_kb(m, "Inactive:       ", pages[LRU_INACTIVE_ANON] + pages[LRU_INACTIVE_FILE]);
    /**
     *  活跃的匿名页面，下略
     */
	show_val_kb(m, "Active(anon):   ", pages[LRU_ACTIVE_ANON]);
	show_val_kb(m, "Inactive(anon): ", pages[LRU_INACTIVE_ANON]);
	show_val_kb(m, "Active(file):   ", pages[LRU_ACTIVE_FILE]);
	show_val_kb(m, "Inactive(file): ", pages[LRU_INACTIVE_FILE]);
	show_val_kb(m, "Unevictable:    ", pages[LRU_UNEVICTABLE]);
    /**
     *  不会被交换到交换分区的页面
     */
	show_val_kb(m, "Mlocked:        ", global_zone_page_state(NR_MLOCK));

#ifdef CONFIG_HIGHMEM
//	show_val_kb(m, "HighTotal:      ", i.totalhigh);
//	show_val_kb(m, "HighFree:       ", i.freehigh);
//	show_val_kb(m, "LowTotal:       ", i.totalram - i.totalhigh);
//	show_val_kb(m, "LowFree:        ", i.freeram - i.freehigh);
#endif

#ifndef CONFIG_MMU
//	show_val_kb(m, "MmapCopy:       ", (unsigned long)atomic_long_read(&mmap_pages_allocated));
#endif

    /**
     *  交换分区大小
     */
	show_val_kb(m, "SwapTotal:      ", i.totalswap);
    /**
     *  交换分区的空闲大小
     */
	show_val_kb(m, "SwapFree:       ", i.freeswap);
    /**
     *  脏页的数量
     */
	show_val_kb(m, "Dirty:          ", global_node_page_state(NR_FILE_DIRTY));
    /**
     *  正在回写的数量
     */
	show_val_kb(m, "Writeback:      ", global_node_page_state(NR_WRITEBACK));
    /**
     *  统计有反向映射RMAP的页面，这些页面都是匿名页面，并且都映射到了用户空间
     */
	show_val_kb(m, "AnonPages:      ", global_node_page_state(NR_ANON_MAPPED));
    /**
     *  所有映射到用户地址空间的内容缓存页面(文件映射页面)
     */
	show_val_kb(m, "Mapped:         ", global_node_page_state(NR_FILE_MAPPED));
    /**
     *  共享内存(shmem,devtmfs等)页面数量
     */
	show_val_kb(m, "Shmem:          ", i.sharedram);
    /**
     *  内核可回收的内存，包括可回收的slab
     */
	show_val_kb(m, "KReclaimable:   ", sreclaimable + global_node_page_state(NR_KERNEL_MISC_RECLAIMABLE));
    /**
     *  所有slab页面
     */
	show_val_kb(m, "Slab:           ", sreclaimable + sunreclaim);
    /**
     *  可回收的slab
     */
	show_val_kb(m, "SReclaimable:   ", sreclaimable);
    /**
     *  不可回收的slab
     */
	show_val_kb(m, "SUnreclaim:     ", sunreclaim);
    /**
     *  所有进程内核栈的总大小
     */
	seq_printf(m, "KernelStack:    %8lu kB\n", global_node_page_state(NR_KERNEL_STACK_KB));
#ifdef CONFIG_SHADOW_CALL_STACK
	seq_printf(m, "ShadowCallStack:%8lu kB\n", global_node_page_state(NR_KERNEL_SCS_KB));
#endif
    /**
     *  用于页表的页面数量
     */
	show_val_kb(m, "PageTables:     ", global_zone_page_state(NR_PAGETABLE));

    /**
     *  NFS中，发送到服务器端但是还没有写入磁盘的页面
     */
	show_val_kb(m, "NFS_Unstable:   ", 0);
    /**
     *
     */
	show_val_kb(m, "Bounce:         ", global_zone_page_state(NR_BOUNCE));
    /**
     *  回写过程中使用的临时页面
     */
	show_val_kb(m, "WritebackTmp:   ", global_node_page_state(NR_WRITEBACK_TEMP));
    /**
     *
     */
	show_val_kb(m, "CommitLimit:    ", vm_commit_limit());
	show_val_kb(m, "Committed_AS:   ", committed);
    /**
     *  vmalloc 区的总大小
     */
	seq_printf(m, "VmallocTotal:   %8lu kB\n", (unsigned long)VMALLOC_TOTAL >> 10);
    /**
     *  vmalloc 区已使用的大小
     */
	show_val_kb(m, "VmallocUsed:    ", vmalloc_nr_pages());
	show_val_kb(m, "VmallocChunk:   ", 0ul);
    /**
     *  per-CPU 机制使用的页面
     */
	show_val_kb(m, "Percpu:         ", pcpu_nr_pages());

#ifdef CONFIG_MEMORY_FAILURE
	seq_printf(m, "HardwareCorrupted: %5lu kB\n", atomic_long_read(&num_poisoned_pages) << (PAGE_SHIFT - 10));
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
    /**
     *  透明大页的数量
     */
	show_val_kb(m, "AnonHugePages:  ", global_node_page_state(NR_ANON_THPS) * HPAGE_PMD_NR);
    /**
     *  在shmem和tmpfs中使用的透明巨页的数量
     */
	show_val_kb(m, "ShmemHugePages: ", global_node_page_state(NR_SHMEM_THPS) * HPAGE_PMD_NR);
    /**
     *  使用透明巨页，并映射到用户空间的shmem和tmpfs的页面数量
     */
	show_val_kb(m, "ShmemPmdMapped: ", global_node_page_state(NR_SHMEM_PMDMAPPED) * HPAGE_PMD_NR);
	show_val_kb(m, "FileHugePages:  ", global_node_page_state(NR_FILE_THPS) * HPAGE_PMD_NR);
	show_val_kb(m, "FilePmdMapped:  ", global_node_page_state(NR_FILE_PMDMAPPED) * HPAGE_PMD_NR);
#endif

#ifdef CONFIG_CMA
    /**
     *  CMA机制使用的内存总数
     */
	show_val_kb(m, "CmaTotal:       ", totalcma_pages);

    /**
     *  CMA机制中空闲的内存
     */
	show_val_kb(m, "CmaFree:        ", global_zone_page_state(NR_FREE_CMA_PAGES));
#endif

    /**
     *  巨页
     *  HugePages_Total:       0
     *  HugePages_Free:        0
     *  HugePages_Rsvd:        0
     *  HugePages_Surp:        0
     *  Hugepagesize:       2048 kB
     */
	hugetlb_report_meminfo(m);

    /**
     *
     */
	arch_report_meminfo(m);

	return 0;
}

/**
 *  /proc/meminfo
 */
static int __init proc_meminfo_init(void)
{
    /**
     *
     */
	proc_create_single("meminfo", 0, NULL, meminfo_proc_show);
	return 0;
}
fs_initcall(proc_meminfo_init); /* /proc/meminfo */