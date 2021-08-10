/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BH_H
#define _LINUX_BH_H

#include <linux/preempt.h>

#ifdef CONFIG_TRACE_IRQFLAGS
extern void __local_bh_disable_ip(unsigned long ip, unsigned int cnt);
#else
static __always_inline void __local_bh_disable_ip(unsigned long ip, unsigned int cnt)
{
    /**
     *  增加 irq 计数
     */
	preempt_count_add(cnt);

    /**
     *  防止编译器优化
     */
	barrier();
}
#endif

/**
 *  关闭软中断的锁机制
 */
static inline void local_bh_disable(void)
{
    /**
     *  增加计数
     */
	__local_bh_disable_ip(_THIS_IP_, SOFTIRQ_DISABLE_OFFSET);
}

extern void _local_bh_enable(void);
extern void __local_bh_enable_ip(unsigned long ip, unsigned int cnt);

/**
 *  
 */
static inline void local_bh_enable_ip(unsigned long ip)
{
	__local_bh_enable_ip(ip, SOFTIRQ_DISABLE_OFFSET);
}

/**
 *  开启软中断
 */
static inline void local_bh_enable(void)
{
	__local_bh_enable_ip(_THIS_IP_, SOFTIRQ_DISABLE_OFFSET);
}

#endif /* _LINUX_BH_H */
