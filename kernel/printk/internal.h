/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * internal.h - printk internal definitions
 */
#include <linux/percpu.h>

#ifdef CONFIG_PRINTK

#define PRINTK_SAFE_CONTEXT_MASK	0x007ffffff
#define PRINTK_NMI_DIRECT_CONTEXT_MASK	0x008000000
#define PRINTK_NMI_CONTEXT_MASK		0xff0000000

#define PRINTK_NMI_CONTEXT_OFFSET	0x010000000

extern raw_spinlock_t logbuf_lock;


int vprintk_store(int facility, int level,
		  const struct dev_printk_info *dev_info,
		  const char *fmt, va_list args);

 int vprintk_default(const char *fmt, va_list args);
 int vprintk_deferred(const char *fmt, va_list args);
 int vprintk_func(const char *fmt, va_list args);
void __printk_safe_enter(void);
void __printk_safe_exit(void);

void printk_safe_init(void);
bool printk_percpu_data_ready(void);

#define printk_safe_enter_irqsave(flags)	\
	do {					\
		local_irq_save(flags);		\
		__printk_safe_enter();		\
	} while (0)

#define printk_safe_exit_irqrestore(flags)	\
	do {					\
		__printk_safe_exit();		\
		local_irq_restore(flags);	\
	} while (0)

#define printk_safe_enter_irq()		\
	do {					\
		local_irq_disable();		\
		__printk_safe_enter();		\
	} while (0)

#define printk_safe_exit_irq()			\
	do {					\
		__printk_safe_exit();		\
		local_irq_enable();		\
	} while (0)

void defer_console_output(void);

#else

#endif /* CONFIG_PRINTK */
