/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KASAN_H
#define _LINUX_KASAN_H

#include <linux/types.h>

struct kmem_cache;
struct page;
struct vm_struct;
struct task_struct;

#ifdef CONFIG_KASAN

#include <linux/pgtable.h>
#include <asm/kasan.h>

/* kasan_data struct is used in KUnit tests for KASAN expected failures */
struct kunit_kasan_expectation {
	bool report_expected;
	bool report_found;
};

/**
 * @brief kasan 零页？？
 *
 */
extern unsigned char kasan_early_shadow_page[PAGE_SIZE];
extern pte_t kasan_early_shadow_pte[PTRS_PER_PTE];
extern pmd_t kasan_early_shadow_pmd[PTRS_PER_PMD];
extern pud_t kasan_early_shadow_pud[PTRS_PER_PUD];
extern p4d_t kasan_early_shadow_p4d[MAX_PTRS_PER_P4D];

int kasan_populate_early_shadow(const void *shadow_start,
				const void *shadow_end);

/**
 * @brief
 *
 * shadow memory检测原理的实现主要就是__asan_load##size()和__asan_store##size()函数的实现。
 * 那么KASAN是如何根据访问的address以及对应的shadow memory的状态值来判断访问是否合法呢？
 * 首先看一种最简单的情况。访问8 bytes内存。
 *
 *	long *addr = (long *)0xffff800012345678;
 *	*addr = 0;
 *
 * 以上代码是访问8 bytes情况，检测原理如下：
 *
 *  long *addr = (long *)0xffff800012345678;
 *  char *shadow = (char *)(((unsigned long)addr >> 3) + KASAN_SHADOW_OFFSE);
 *  if (*shadow)
 *    report_bug();
 *  *addr = 0;
 *
 * +----+ 0xffffffffffffffff
 * |	|
 * |	|
 * |	|
 * |	|
 * |	|
 * |	|
 * |	|
 * |	|
 * +----+ 0xdffffc0000000000UL(KASAN_SHADOW_OFFSET)
 * |	|
 * |	|
 * |....|
 * |	| addr
 * |	|
 * +----+ 0x0000000000000000
 *
 */
static inline void *kasan_mem_to_shadow(const void *addr)
{
	/**
	 * @brief char *shadow = (char *)(((unsigned long)addr >> 3) + KASAN_SHADOW_OFFSE);
	 *
	 */
	return (void *)((unsigned long)addr >> KASAN_SHADOW_SCALE_SHIFT/*3*/)
		+ KASAN_SHADOW_OFFSET/*0xdffffc0000000000UL*/;
}

/* Enable reporting bugs after kasan_disable_current() */
extern void kasan_enable_current(void);

/* Disable reporting bugs for current task */
extern void kasan_disable_current(void);

void kasan_unpoison_shadow(const void *address, size_t size);

void kasan_unpoison_task_stack(struct task_struct *task);

void kasan_alloc_pages(struct page *page, unsigned int order);
void kasan_free_pages(struct page *page, unsigned int order);

void kasan_cache_create(struct kmem_cache *cache, unsigned int *size,
			slab_flags_t *flags);

void kasan_poison_slab(struct page *page);
void kasan_unpoison_object_data(struct kmem_cache *cache, void *object);
void kasan_poison_object_data(struct kmem_cache *cache, void *object);
void * __must_check kasan_init_slab_obj(struct kmem_cache *cache,
					const void *object);

void * __must_check kasan_kmalloc_large(const void *ptr, size_t size,
						gfp_t flags);
void kasan_kfree_large(void *ptr, unsigned long ip);
void kasan_poison_kfree(void *ptr, unsigned long ip);
void * __must_check kasan_kmalloc(struct kmem_cache *s, const void *object,
					size_t size, gfp_t flags);
void * __must_check kasan_krealloc(const void *object, size_t new_size,
					gfp_t flags);

void * __must_check kasan_slab_alloc(struct kmem_cache *s, void *object,
					gfp_t flags);
bool kasan_slab_free(struct kmem_cache *s, void *object, unsigned long ip);

struct kasan_cache {
	int alloc_meta_offset;
	int free_meta_offset;
};

/*
 * These functions provide a special case to support backing module
 * allocations with real shadow memory. With KASAN vmalloc, the special
 * case is unnecessary, as the work is handled in the generic case.
 */
#ifndef CONFIG_KASAN_VMALLOC
int kasan_module_alloc(void *addr, size_t size);
void kasan_free_shadow(const struct vm_struct *vm);
#else
static inline int kasan_module_alloc(void *addr, size_t size) { return 0; }
static inline void kasan_free_shadow(const struct vm_struct *vm) {}
#endif

int kasan_add_zero_shadow(void *start, unsigned long size);
void kasan_remove_zero_shadow(void *start, unsigned long size);

size_t __ksize(const void *);
static inline void kasan_unpoison_slab(const void *ptr)
{
	kasan_unpoison_shadow(ptr, __ksize(ptr));
}
size_t kasan_metadata_size(struct kmem_cache *cache);

bool kasan_save_enable_multi_shot(void);
void kasan_restore_multi_shot(bool enabled);

#else /* CONFIG_KASAN */

#endif /* CONFIG_KASAN */

#ifdef CONFIG_KASAN_GENERIC

#define KASAN_SHADOW_INIT 0

void kasan_cache_shrink(struct kmem_cache *cache);
void kasan_cache_shutdown(struct kmem_cache *cache);
void kasan_record_aux_stack(void *ptr);

#else /* CONFIG_KASAN_GENERIC */

#endif /* CONFIG_KASAN_GENERIC */

#ifdef CONFIG_KASAN_SW_TAGS

#define KASAN_SHADOW_INIT 0xFF

void kasan_init_tags(void);

void *kasan_reset_tag(const void *addr);

bool kasan_report(unsigned long addr, size_t size,
		bool is_write, unsigned long ip);

#else /* CONFIG_KASAN_SW_TAGS */



#endif /* CONFIG_KASAN_SW_TAGS */

#ifdef CONFIG_KASAN_VMALLOC
int kasan_populate_vmalloc(unsigned long addr, unsigned long size);
void kasan_poison_vmalloc(const void *start, unsigned long size);
void kasan_unpoison_vmalloc(const void *start, unsigned long size);
void kasan_release_vmalloc(unsigned long start, unsigned long end,
			   unsigned long free_region_start,
			   unsigned long free_region_end);
#else

#endif

#ifdef CONFIG_KASAN_INLINE
void kasan_non_canonical_hook(unsigned long addr);
#else /* CONFIG_KASAN_INLINE */

#endif /* CONFIG_KASAN_INLINE */

#endif /* LINUX_KASAN_H */
