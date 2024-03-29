/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TOOLS_LINUX_KERNEL_H
#define __TOOLS_LINUX_KERNEL_H

#include <stdarg.h>
#include <stddef.h>
#include <assert.h>
#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <endian.h>
#include <byteswap.h>

#ifndef UINT_MAX
#define UINT_MAX	(~0U)
#endif

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

#define PERF_ALIGN(x, a)	__PERF_ALIGN_MASK(x, (typeof(x))(a)-1)
#define __PERF_ALIGN_MASK(x, mask)	(((x)+(mask))&~(mask))

//#ifndef roundup
//#define roundup(x, y) (                                \
//{                                                      \
//	const typeof(y) __y = y;		       \
//	(((x) + (__y - 1)) / __y) * __y;	       \
//}                                                      \
//)
//#endif

//#define BUG()	BUG_ON(1)

//#define cpu_to_le16
//#define cpu_to_le32
//#define cpu_to_le64
//#define le16_to_cpu
//#define le32_to_cpu
//#define le64_to_cpu
//#define cpu_to_be16 bswap_16
//#define cpu_to_be32 bswap_32
//#define cpu_to_be64 bswap_64
//#define be16_to_cpu bswap_16
//#define be32_to_cpu bswap_32
//#define be64_to_cpu bswap_64

int vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
int scnprintf(char * buf, size_t size, const char * fmt, ...);
int scnprintf_pad(char * buf, size_t size, const char * fmt, ...);

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

/*
 * This looks more complex than it should be. But we need to
 * get the type for the ~ right in round_down (it needs to be
 * as wide as the result!), and we want to evaluate the macro
 * arguments just once each.
 */
//#define __round_mask(x, y) ((__typeof__(x))((y)-1))
//#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
//#define round_down(x, y) ((x) & ~__round_mask(x, y))

//#define current_gfp_context(k) 0
//#define synchronize_rcu()

#endif
