/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_INIT_H
#define _ASM_X86_INIT_H
typedef void * pvoid_t;
struct x86_mapping_info {   /* 此结构提供有关内存映射的信息 */
	pvoid_t (*alloc_pgt_page)(void *); /* allocate buf for page table 为页表项分配空间 
                                    回调函数检查有一个新页的空间，从缓冲区分配新页并返回新页的地址*/
	void *context;			 /* context for alloc_pgt_page 跟踪分配的页表 */
	unsigned long page_flag;	 /* page flag for PMD or PUD entry, PMD或PUD条目的标志*/
	unsigned long offset;		 /* ident mapping offset 内核的虚拟地址与其实际地址之间的偏移量*/
	bool direct_gbpages;		 /* PUD level 1GB page support 检查是否支持大页面*/
	unsigned long kernpg_flag;	 /* kernel pagetable flag override 内核页面的可覆盖标志*/
};

int kernel_ident_mapping_init(struct x86_mapping_info *info, pgd_t *pgd_page,
				unsigned long pstart, unsigned long pend);

#endif /* _ASM_X86_INIT_H */
