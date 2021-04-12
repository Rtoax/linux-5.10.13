/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LCM_H
#define _LCM_H

#include <linux/compiler.h>

unsigned long __attribute_const__ lcm(unsigned long a, unsigned long b) ;
unsigned long __attribute_const__ lcm_not_zero(unsigned long a, unsigned long b) ;

#endif /* _LCM_H */
