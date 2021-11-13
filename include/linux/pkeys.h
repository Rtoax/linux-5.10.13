/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PKEYS_H
#define _LINUX_PKEYS_H

#include <linux/mm.h>

#ifdef CONFIG_ARCH_HAS_PKEYS
#include <asm/pkeys.h>
#else /* ! CONFIG_ARCH_HAS_PKEYS */
/*  */
#endif /* ! CONFIG_ARCH_HAS_PKEYS */

#endif /* _LINUX_PKEYS_H */
