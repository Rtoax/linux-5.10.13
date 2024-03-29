/* SPDX-License-Identifier: GPL-2.0 */
#ifndef KERNEL_H
#define KERNEL_H
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/bug.h>
#include <errno.h>
#include <unistd.h>
#include <asm/barrier.h>

#define CONFIG_SMP

#define PAGE_SIZE getpagesize()
#define PAGE_MASK (~(PAGE_SIZE-1))
#define PAGE_ALIGN(x) ((x + PAGE_SIZE - 1) & PAGE_MASK)

/* generic data direction definitions */
#define READ                    0
#define WRITE                   1

typedef unsigned long long phys_addr_t;
typedef unsigned long long dma_addr_t;
typedef size_t __kernel_size_t;
typedef unsigned int __wsum;

struct page {
	unsigned long long dummy;
};

/* Physical == Virtual */
#define virt_to_phys(p) ((unsigned long)p)
#define phys_to_virt(a) ((void *)(unsigned long)(a))
/* Page address: Virtual / 4K */
#define page_to_phys(p) ((dma_addr_t)(unsigned long)(p))
#define virt_to_page(p) ((struct page *)((unsigned long)p & PAGE_MASK))

#define offset_in_page(p) (((unsigned long)p) % PAGE_SIZE)

#define __printf(a,b) __attribute__((format(printf,a,b)))

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

extern void *__kmalloc_fake, *__kfree_ignore_start, *__kfree_ignore_end;
static inline void *kmalloc(size_t s, gfp_t gfp)
{
	if (__kmalloc_fake)
		return __kmalloc_fake;
	return malloc(s);
}
static inline void *kmalloc_array(unsigned n, size_t s, gfp_t gfp)
{
	return kmalloc(n * s, gfp);
}

static inline void *kzalloc(size_t s, gfp_t gfp)
{
	void *p = kmalloc(s, gfp);

	memset(p, 0, s);
	return p;
}

static inline void *alloc_pages_exact(size_t s, gfp_t gfp)
{
	return kmalloc(s, gfp);
}

static inline void kfree(void *p)
{
	if (p >= __kfree_ignore_start && p < __kfree_ignore_end)
		return;
	free(p);
}

static inline void free_pages_exact(void *p, size_t s)
{
	kfree(p);
}

static inline void *krealloc(void *p, size_t s, gfp_t gfp)
{
	return realloc(p, s);
}


static inline unsigned long __get_free_page(gfp_t gfp)
{
	void *p;

	posix_memalign(&p, PAGE_SIZE, PAGE_SIZE);
	return (unsigned long)p;
}

static inline void free_page(unsigned long addr)
{
	free((void *)addr);
}

#define pr_err(format, ...) fprintf (stderr, format, ## __VA_ARGS__)
#ifdef DEBUG
#define pr_debug(format, ...) fprintf (stderr, format, ## __VA_ARGS__)
#else
#define pr_debug(format, ...) do {} while (0)
#endif
#define dev_err(dev, format, ...) fprintf (stderr, format, ## __VA_ARGS__)
#define dev_warn(dev, format, ...) fprintf (stderr, format, ## __VA_ARGS__)

#define WARN_ON_ONCE(cond) (unlikely(cond) ? fprintf (stderr, "WARNING\n") : 0)

#endif /* KERNEL_H */
