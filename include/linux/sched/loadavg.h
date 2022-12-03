/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_LOADAVG_H
#define _LINUX_SCHED_LOADAVG_H

/*
 * These are the constant used to fake the fixed-point load-average
 * counting. Some notes:
 *  - 11 bit fractions expand to 22 bits by the multiplies: this gives
 *    a load-average precision of 10 bits integer + 11 bits fractional
 *  - if you want to count load-averages more often, you need more
 *    precision, or rounding will get you. With 2-second counting freq,
 *    the EXP_n values would be 1981, 2034 and 2043 if still using only
 *    11 bit fractions.
 */
extern unsigned long avenrun[];		/* Load averages */
extern void get_avenrun(unsigned long *loads, unsigned long offset, int shift);

/**
 * 采用11位精度的定点化计算，CPU负载 1.0 由整数2048表示
 */
#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_FREQ	(5*HZ+1)	/* 5 sec intervals */
/**
 * 分别代表最近1/5/15分钟的定点化值的指数因子
 *
 * 以 e 为底的指数函数:
 *
 *  float exp_1 = 1.0/exp(5.0 / 60.0) * 2048;
 *  float exp_5 = 1.0/exp(5.0 / 300.0) * 2048;
 *  float exp_15 = 1.0/exp(5.0 / 900.0) * 2048;
 *
 *  1884.250977
 *  2014.149536
 *  2036.653809
 */
#define EXP_1		1884		/* 1/exp(5sec/1min) as fixed-point */
#define EXP_5		2014		/* 1/exp(5sec/5min) */
#define EXP_15		2037		/* 1/exp(5sec/15min) */

/**
 * @load - 值为旧的CPU负载值avenrun[]
 * @exp - EXP_1/EXP_5/EXP_15，分别代表最近1/5/15分钟的定点化值的指数因子；
 * @active - 根据读取calc_load_tasks的值来判断，大于0则乘以 FIXED_1(2048) 传入；
 *          active = active * FIXED_1
 *
 * a1 = a0 * e + a * (1 - e)
 *
 * calc_global_load() {
 *  ...
 *  avenrun[0] = calc_load(avenrun[0], EXP_1, active);
 *  avenrun[1] = calc_load(avenrun[1], EXP_5, active);
 *  avenrun[2] = calc_load(avenrun[2], EXP_15, active);
 * }
 */
static inline unsigned long
calc_load(unsigned long load, unsigned long exp, unsigned long active)
{
	unsigned long newload;

	/**
	 * 例如 1分钟的负载
	 *
	 *                      1 * 2048                             1 * 2048
	 * newload = load * ----------------- + active * ( 2048 - -------------- )
	 *                    exp( 5 / 60 )                        exp( 5 / 60 )
	 */
	newload = load * exp + active * (FIXED_1 - exp);

	/**
	 * 根据active和load值的大小关系来决定是否需要加 2047；
	 */
	if (active >= load)
		newload += FIXED_1-1;

	return newload / FIXED_1;
}

extern unsigned long calc_load_n(unsigned long load, unsigned long exp,
				 unsigned long active, unsigned int n);

/**
 * FSHIFT = 11
 */
#define LOAD_INT(x) ((x) >> FSHIFT)
/**
 * 100: 保留两位小数
 */
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

extern void calc_global_load(void);

#endif /* _LINUX_SCHED_LOADAVG_H */
