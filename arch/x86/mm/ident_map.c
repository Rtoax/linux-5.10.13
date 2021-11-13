// SPDX-License-Identifier: GPL-2.0
/*
 * Helper routines for building identity mapping page tables. This is
 * included by both the compressed kernel and the regular kernel.
 */

static void ident_pmd_init(struct x86_mapping_info *info, pmd_t *pmd_page,
			   unsigned long addr, unsigned long end)
{
	addr &= PMD_MASK;
	for (; addr < end; addr += PMD_SIZE) {
		pmd_t *pmd = pmd_page + pmd_index(addr);

		if (pmd_present(*pmd))
			continue;

		set_pmd(pmd, __pmd((addr - info->offset) | info->page_flag));
	}
}

static int ident_pud_init(struct x86_mapping_info *info, pud_t *pud_page,
			  unsigned long addr, unsigned long end)
{
	unsigned long next;

	for (; addr < end; addr = next) {
		pud_t *pud = pud_page + pud_index(addr);
		pmd_t *pmd;

		next = (addr & PUD_MASK) + PUD_SIZE;
		if (next > end)
			next = end;

		if (info->direct_gbpages) {
			pud_t pudval;

			if (pud_present(*pud))
				continue;

			addr &= PUD_MASK;
			pudval = __pud((addr - info->offset) | info->page_flag);
			set_pud(pud, pudval);
			continue;
		}

		if (pud_present(*pud)) {
			pmd = pmd_offset(pud, 0);
			ident_pmd_init(info, pmd, addr, next);
			continue;
		}
		pmd = (pmd_t *)info->alloc_pgt_page(info->context);
		if (!pmd)
			return -ENOMEM;
		ident_pmd_init(info, pmd, addr, next);
		set_pud(pud, __pud(__pa(pmd) | info->kernpg_flag));
	}

	return 0;
}

static int ident_p4d_init(struct x86_mapping_info *info, p4d_t *p4d_page,
			  unsigned long addr, unsigned long end)
{
	unsigned long next;
	int result;

	for (; addr < end; addr = next) {
		p4d_t *p4d = p4d_page + p4d_index(addr);
		pud_t *pud;

		next = (addr & P4D_MASK) + P4D_SIZE;
		if (next > end)
			next = end;

		if (p4d_present(*p4d)) {
			pud = pud_offset(p4d, 0);
			result = ident_pud_init(info, pud, addr, next);
			if (result)
				return result;

			continue;
		}
		pud = (pud_t *)info->alloc_pgt_page(info->context);
		if (!pud)
			return -ENOMEM;

		result = ident_pud_init(info, pud, addr, next);
		if (result)
			return result;

		set_p4d(p4d, __p4d(__pa(pud) | info->kernpg_flag));
	}

	return 0;
}

//传入以上初始化好的`mapping_info`实例、顶层页表的地址和建立新的恒等映射的内存区域的地址

int kernel_ident_mapping_init(struct x86_mapping_info *info, pgd_t *pgd_page,
			      unsigned long pstart, unsigned long pend)
{
	unsigned long addr = pstart + info->offset;
	unsigned long end = pend + info->offset;
	unsigned long next;
	int result;

	/* Set the default pagetable flags if not supplied */
    //`kernel_ident_mapping_init`函数为新页设置默认的标志，如果它们没有被给出
	if (!info->kernpg_flag)
		info->kernpg_flag = _KERNPG_TABLE;

    /**
     *  开始建立新的2MB (因为`mapping_info.page_flag`中的`PSE`位) 给定地址相关的
     *  页表项（[五级页表]中的`PGD -> P4D -> PUD -> PMD`
     *      或者[四级页表]中的`PGD -> PUD -> PMD`）
     */

	/* Filter out unsupported __PAGE_KERNEL_* bits: */
	info->kernpg_flag &= __default_kernel_pte_mask;

	for (; addr < end; addr = next) {
		pgd_t *pgd = pgd_page + pgd_index(addr);
		p4d_t *p4d;

        //首先我们找给定地址在 `页全局目录` 的下一项
		next = (addr & PGDIR_MASK) + PGDIR_SIZE;

        //如果它大于给定的内存区域的末地址`end`
		if (next > end)
			next = end; //设为`end`

		if (pgd_present(*pgd)) {
			p4d = p4d_offset(pgd, 0);
			result = ident_p4d_init(info, p4d, addr, next);
			if (result)
				return result;
			continue;
		}

        //用`x86_mapping_info`回调函数分配一个新页,为页表项分配空间 
		p4d = (p4d_t *)info->alloc_pgt_page(info->context);
		if (!p4d)
			return -ENOMEM;

        //然后调用`ident_p4d_init`函数。
        //`ident_p4d_init`函数做同样的事情，但是用于低层的页目录 (`p4d` -> `pud` -> `pmd`)
		result = ident_p4d_init(info, p4d, addr, next);
		if (result)
			return result;
		if (pgtable_l5_enabled()) {
			set_pgd(pgd, __pgd(__pa(p4d) | info->kernpg_flag));
		} else {
			/*
			 * With p4d folded, pgd is equal to p4d.
			 * The pgd entry has to point to the pud page table in this case.
			 */
			pud_t *pud = pud_offset(p4d, 0);
			set_pgd(pgd, __pgd(__pa(pud) | info->kernpg_flag));
		}
	}

    //和保留地址相关的新页表项已经在我们的页表中

	return 0;
}
