// SPDX-License-Identifier: GPL-2.0
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/personality.h>
#include <linux/random.h>
#include <linux/uaccess.h>
#include <linux/elf.h>

#include <asm/elf.h>
#include <asm/ia32.h>

/*
 * Align a virtual address to avoid aliasing in the I$ on AMD F15h.
 */
static unsigned long get_align_mask(void)
{
	/* handle 32- and 64-bit case with a single conditional */
	if (va_align.flags < 0 || !(va_align.flags & (2 - mmap_is_ia32())))
		return 0;

	if (!(current->flags & PF_RANDOMIZE))
		return 0;

	return va_align.mask;
}

/*
 * To avoid aliasing in the I$ on AMD F15h, the bits defined by the
 * va_align.bits, [12:upper_bit), are set to a random value instead of
 * zeroing them. This random value is computed once per boot. This form
 * of ASLR is known as "per-boot ASLR".
 *
 * To achieve this, the random value is added to the info.align_offset
 * value before calling vm_unmapped_area() or ORed directly to the
 * address.
 */
static unsigned long get_align_bits(void)
{
	return va_align.bits & get_align_mask();
}

unsigned long align_vdso_addr(unsigned long addr)
{
	unsigned long align_mask = get_align_mask();
	addr = (addr + align_mask) & ~align_mask;
	return addr | get_align_bits();
}

static int __init control_va_addr_alignment(char *str)
{
	/* guard against enabling this on other CPU families */
	if (va_align.flags < 0)
		return 1;

	if (*str == 0)
		return 1;

	if (*str == '=')
		str++;

	if (!strcmp(str, "32"))
		va_align.flags = ALIGN_VA_32;
	else if (!strcmp(str, "64"))
		va_align.flags = ALIGN_VA_64;
	else if (!strcmp(str, "off"))
		va_align.flags = 0;
	else if (!strcmp(str, "on"))
		va_align.flags = ALIGN_VA_32 | ALIGN_VA_64;
	else
		return 0;

	return 1;
}
__setup("align_va_addr", control_va_addr_alignment);

/**
 *
 */
long mmap(unsigned long addr, unsigned long len,
                        unsigned long prot, unsigned long flags,
                        unsigned long fd, unsigned long pgoff){/* +++ */}
SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, off)
{
	long error;
	error = -EINVAL;
	if (off & ~PAGE_MASK)
		goto out;
	/**
	 *
	 */
	error = ksys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
out:
	return error;
}

/**
 *  +-------+ end
 *  |       |
 *  |       |
 *  |       | <-- addr
 *  |       |
 *  |       |
 *  +-------+ begin
 *
 *  获取mmap区域的开始、结束地址：
 *      begin   ：mm->mmap_base
 *      end     ：task_size
 */
static void find_start_end(unsigned long addr, unsigned long flags,
		unsigned long *begin, unsigned long *end)
{
	/* 32 位 */
	if (!in_32bit_syscall() && (flags & MAP_32BIT)) {
		/* This is usually used needed to map code in small
		   model, so it needs to be in the first 31bit. Limit
		   it to that.  This means we need to move the
		   unmapped base down for this case. This can give
		   conflicts with the heap, but we assume that glibc
		   malloc knows how to fall back to mmap. Give it 1GB
		   of playground for now. -AK */
		*begin = 0x40000000;
		*end = 0x80000000;
		if (current->flags & PF_RANDOMIZE) {
			*begin = randomize_page(*begin, 0x02000000);
		}
		return;
	}


	*begin	= get_mmap_base(1);
	if (in_32bit_syscall())
		*end = task_size_32bit();
	else
		*end = task_size_64bit(addr > DEFAULT_MAP_WINDOW/*0x0000 7fff ffff f000*/);
}

unsigned long   /* 获取没被映射的内存区域 */
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct vm_unmapped_area_info info;
	unsigned long begin, end;

    //如果是固定映射，则返回原地址
	if (flags & MAP_FIXED)
		return addr;

    /**
     *  +-------+ end
     *  |       |
     *  |       |
     *  |       | <-- addr
     *  |       |
     *  |       |
     *  +-------+ begin
     *
     *  获取mmap区域的开始、结束地址：
     *      begin   ：mm->mmap_base
     *      end     ：task_size
     */
	find_start_end(addr/* x虚拟地址 */, flags, &begin, &end);  /* 查找 */

    /* 超长出错返回 */
	if (len > end)
		return -ENOMEM;

    //If an address is provided, use it for the mapping
    /* 按照用户给出的原始addr和len查看这里是否有空洞 */
	if (addr) {
		addr = PAGE_ALIGN(addr);    /* 对齐 Make sure the address is page aligned */
		vma = find_vma(mm, addr);   /* 查找对应 vma, return the region closest to the requested address*/
        /* Make sure the mapping will not overlap with another region.
           If it does not, return it as it is safe to use. Otherwise it gets ignored */

        /**
         *   https://blog.csdn.net/pwl999/article/details/109188517
         *
         *  这里容易有个误解：
         *  开始以为find_vma()的作用是找到一个vma满足：vma->vm_start <= addr < vma->vm_end
         *  实际find_vma()的作用是找到一个地址最小的vma满足： addr < vma->vm_end (2021年7月8日)
         *
         *  在vma红黑树之外找到空间：
         *  1、如果addr的值在vma红黑树之上：!vma ，且有足够的空间：end - len >= addr
         *  2、如果addr的值在vma红黑树之下，且有足够的空间：addr + len <= vm_start_gap(vma)
         */
		if (end - len >= addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			return addr;
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = begin;
	info.high_limit = end;
	info.align_mask = 0;
	info.align_offset = pgoff << PAGE_SHIFT;
	if (filp) {
		info.align_mask = get_align_mask();
		info.align_offset += get_align_bits();
	}

    /**
     *  否则只能废弃掉用户指定的地址，根据长度重新给他找一个合适的地址
     *    优先在vma红黑树的空洞中找，其次在空白位置找
     */
	return vm_unmapped_area(&info);
}


/**
 *  现代layout模式下，mmap分配是从高到低的，
 *  调用current->mm->get_unmapped_area -> arch_get_unmapped_area_topdown()：
 *
 *  https://rtoax.blog.csdn.net/article/details/118602363
 */
unsigned long
arch_get_unmapped_area_topdown(struct file *filp, const unsigned long addr0,
			  const unsigned long len, const unsigned long pgoff,
			  const unsigned long flags)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	unsigned long addr = addr0;
	struct vm_unmapped_area_info info;

	/* requested length too big for entire address space */
	if (len > TASK_SIZE/*0x0000 7fff ffff f000 约128T*/)
		return -ENOMEM;

	/* No address checking. See comment at mmap_address_hint_valid() */
	if (flags & MAP_FIXED)
		return addr;

	/**
	 *  for MAP_32BIT mappings we force the legacy mmap base
	 *
	 *  强制使用 legacy 老版本的 mmap 布局
	 *  见 https://rtoax.blog.csdn.net/article/details/118602363
	 */
	if (!in_32bit_syscall() && (flags & MAP_32BIT))
		goto bottomup;

	/**
	 *  requesting a specific address
	 *
	 *  如果 mmap 期间填入了 addr，就会使用特殊的地址
	 */
	if (addr) {
		addr &= PAGE_MASK;  /* 清空 页偏移 12bit */
		if (!mmap_address_hint_valid(addr, len))
			goto get_unmapped_area;

        /* 找到对应的 vma */
		vma = find_vma(mm, addr);

        /**
         *  满足下面的条件之一直接返回地址
         *
         *  1. 如果 addr 不在 VMA 树内
         *  2.
         *
         *  这个 if 大概率不会成立
         */
		if (!vma || addr + len <= vm_start_gap(vma))
			return addr;
	}

get_unmapped_area:


	info.flags = VM_UNMAPPED_AREA_TOPDOWN;  /* 默认为 topdown ， modern 模式 */
	info.length = len;  /* 长度 */
	info.low_limit = PAGE_SIZE/* 4096 */; /* 这么低，那代码段和数据段也可以映射吧 */
	info.high_limit = get_mmap_base(0); /* 由于是topdown，最高不能超出基址 */

	/*
	 * If hint address is above DEFAULT_MAP_WINDOW, look for unmapped area
	 * in the full address space.
	 *
	 * !in_32bit_syscall() check to avoid high addresses for x32
	 * (and make it no op on native i386).
	 *
	 *  如果地址填的太高了，将从没有映射的所有区域进行查找
	 */
	if (addr > DEFAULT_MAP_WINDOW && !in_32bit_syscall())
		info.high_limit += TASK_SIZE_MAX - DEFAULT_MAP_WINDOW;  /* pagetable L5 会造成差异 */

	info.align_mask = 0;
	info.align_offset = pgoff << PAGE_SHIFT/* 12 */;

    /* 如果是文件映射 */
	if (filp) {
		info.align_mask = get_align_mask();
		info.align_offset += get_align_bits();
	}

    /**
     * Search for an unmapped address range.
     *
     * We are looking for a range that:
     * - does not intersect with any VMA;
     * - is contained within the [low_limit, high_limit) interval;
     * - is at least the desired size.
     * - satisfies (begin_addr & align_mask) == (align_offset & align_mask)
     *
     * 我们正在寻找以下范围：
     *  -不与任何VMA相交；
     *  -包含在 [low_limit, high_limit) 间隔内；
     *  -至少是所需的大小。
     *  -满足( begin_addr 和 align_mask ) == ( align_offset 和 align_mask)
     */
	addr = vm_unmapped_area(&info);
	if (!(addr & ~PAGE_MASK))
		return addr;

	VM_BUG_ON(addr != -ENOMEM);

bottomup:
	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	return arch_get_unmapped_area(filp, addr0, len, pgoff, flags);
}
