/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/lib/libgcc.h
 */

#ifndef __LIB_LIBGCC_H
#define __LIB_LIBGCC_H

#include <asm/byteorder.h>

typedef int word_type __attribute__ ((mode (__word__)));

struct DWstruct {
	int low, high;
};

typedef union {
	struct DWstruct s;
	long long ll;
} DWunion;

#endif /* __ASM_LIBGCC_H */
