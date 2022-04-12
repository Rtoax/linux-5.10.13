/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_KASLR_H_
#define _ASM_KASLR_H_

unsigned long kaslr_get_random_long(const char *purpose);
    /* KALSR - Kernel Address Space Layout Randomization 内核地址空间随机分布 */
#ifdef CONFIG_RANDOMIZE_MEMORY
void kernel_randomize_memory(void);
void init_trampoline_kaslr(void);
#else

#endif /* CONFIG_RANDOMIZE_MEMORY */

#endif
