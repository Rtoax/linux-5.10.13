/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KDEV_T_H
#define _LINUX_KDEV_T_H

#include <uapi/linux/kdev_t.h>

/**
 * 31           20 19                      0
 *  +-------------+------------------------+ 
 *  |   MAJOR     |         MINOR          |
 *  +-------------+------------------------+ 
 */
#define MINORBITS	20
#define MINORMASK	((1U << MINORBITS) - 1)

/**
 *  major: 主设备号
 *  minor: 次设备号
 */
#define MAJOR(dev)	((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)	((unsigned int) ((dev) & MINORMASK))

/**
 *  将 主次设备号 转换为 dev_t 类型
 */
#define MKDEV(ma,mi)	(((ma) << MINORBITS) | (mi))

#define print_dev_t(buffer, dev)					\
	sprintf((buffer), "%u:%u\n", MAJOR(dev), MINOR(dev))

#define format_dev_t(buffer, dev)					\
	({								\
		sprintf(buffer, "%u:%u", MAJOR(dev), MINOR(dev));	\
		buffer;							\
	})

/**
 *   `old` 前缀代表了什么呢？出于历史原因，有两种管理主次设备号的方法。
 *
 *  第一种方法主次设备号占用 2 字节。你可以在以前的代码中发现：主设备号占用 8 bit，次设备号占用 8 bit。
 *      但是这会引入一个问题：最多只能支持 256 个主设备号和 256 个次设备号。
 *  因此后来引入了 32 bit 来表示主次设备号，其中 12 位用来表示主设备号，20 位用来表示次设备号。
 */

/* acceptable for old filesystems */
static __always_inline bool old_valid_dev(dev_t dev)
{
	return MAJOR(dev) < 256 && MINOR(dev) < 256;
}

static __always_inline u16 old_encode_dev(dev_t dev)
{
	return (MAJOR(dev) << 8) | MINOR(dev);
}

//获取设备的主次设备号
static __always_inline dev_t old_decode_dev(u16 val)
{
	return MKDEV((val >> 8) & 255, val & 255);
}

static __always_inline u32 new_encode_dev(dev_t dev)
{
	unsigned major = MAJOR(dev);
	unsigned minor = MINOR(dev);
	return (minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12);
}

static __always_inline dev_t new_decode_dev(u32 dev)
{
    //32 bit 来表示主次设备号，其中 12 位用来表示主设备号，20 位用来表示次设备号
	unsigned major = (dev & 0xfff00) >> 8;
	unsigned minor = (dev & 0xff) | ((dev >> 12) & 0xfff00);
	return MKDEV(major, minor);
}

static __always_inline u64 huge_encode_dev(dev_t dev)
{
	return new_encode_dev(dev);
}

static __always_inline dev_t huge_decode_dev(u64 dev)
{
	return new_decode_dev(dev);
}

static __always_inline int sysv_valid_dev(dev_t dev)
{
	return MAJOR(dev) < (1<<14) && MINOR(dev) < (1<<18);
}

static __always_inline u32 sysv_encode_dev(dev_t dev)
{
	return MINOR(dev) | (MAJOR(dev) << 18);
}

static __always_inline unsigned sysv_major(u32 dev)
{
	return (dev >> 18) & 0x3fff;
}

static __always_inline unsigned sysv_minor(u32 dev)
{
	return dev & 0x3ffff;
}

#endif
