/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_GENERIC_IOCTL_H
#define _UAPI_ASM_GENERIC_IOCTL_H

/* ioctl command encoding: 32 bits total, command in lower 16 bits,
 * size of the parameter structure in the lower 14 bits of the
 * upper 16 bits.
 * Encoding the size of the parameter structure in the ioctl request
 * is useful for catching programs compiled with old versions
 * and to avoid overwriting user space outside the user buffer area.
 * The highest 2 bits are reserved for indicating the ``access mode''.
 * NOTE: This limits the max parameter size to 16kB -1 !
 */

/*
 * The following is for compatibility across the various Linux
 * platforms.  The generic ioctl numbering scheme doesn't really enforce
 * a type field.  De facto, however, the top 8 bits of the lower 16
 * bits are indeed used as a type field, so we might just as well make
 * this explicit here.  Please be sure to use the decoding macros
 * below from now on.
 */
/**
 *  序数，顺序编号
 */
#define _IOC_NRBITS	8

/**
 *  幻数: 选择一个号码，并在整个驱动程序中使用这个号码。
 */
#define _IOC_TYPEBITS	8

/*
 * Let any architecture override either of the following before
 * including this file.
 */

#ifndef _IOC_SIZEBITS
# define _IOC_SIZEBITS	14
#endif

#ifndef _IOC_DIRBITS
# define _IOC_DIRBITS	2
#endif

#define _IOC_NRMASK	    /* 0xff */((1 << _IOC_NRBITS/* 8 */)-1)
#define _IOC_TYPEMASK	/* 0xff */((1 << _IOC_TYPEBITS/* 8 */)-1)
#define _IOC_SIZEMASK	/* 0x3fff */((1 << _IOC_SIZEBITS/* 14 */)-1)
#define _IOC_DIRMASK	/* 0x3 */((1 << _IOC_DIRBITS/* 2 */)-1)

#define _IOC_NRSHIFT	0
#define _IOC_TYPESHIFT	/* 8 */(_IOC_NRSHIFT/* 0 */+_IOC_NRBITS/* 8 */)
#define _IOC_SIZESHIFT	/* 16 */(_IOC_TYPESHIFT/* 8 */+_IOC_TYPEBITS/* 8 */)
#define _IOC_DIRSHIFT	/* 30 */(_IOC_SIZESHIFT/* 16 */+_IOC_SIZEBITS/* 14 */)

/*
 * Direction bits, which any architecture can choose to override
 * before including this file.
 *
 * NOTE: _IOC_WRITE means userland is writing and kernel is
 * reading. _IOC_READ means userland is reading and kernel is writing.
 */

/**
 *  定义数据传输的方向，
 */
#ifndef _IOC_NONE
# define _IOC_NONE	0U  //没有数据传输
#endif

#ifndef _IOC_WRITE
# define _IOC_WRITE	1U  //数据写
#endif

#ifndef _IOC_READ
# define _IOC_READ	2U  //数据读
#endif

/**
 *
 *    31 30 29                   16 15             8 7               0
 *    +----+-----------------------+----------------+----------------+
 *    |dir |          size         |      type      |     number     |
 *    +----+-----------------------+----------------+----------------+
 *  direction
 */
#define _IOC(dir,type,nr,size) \
	(((dir)  << _IOC_DIRSHIFT/* 30 */) | \
	 ((type) << _IOC_TYPESHIFT/* 8 */) | \
	 ((nr)   << _IOC_NRSHIFT/* 0 */) | \
	 ((size) << _IOC_SIZESHIFT/* 16 */))

#ifndef __KERNEL__
#define _IOC_TYPECHECK(t) (sizeof(t))
#endif

/*
 * Used to create numbers.
 *
 * NOTE: _IOW means userland is writing and kernel is reading. _IOR
 * means userland is reading and kernel is writing.
 *
 *
 *    31 30 29                   16 15             8 7               0
 *    +----+-----------------------+----------------+----------------+
 *    |dir |          size         |      type      |     number     |
 *    +----+-----------------------+----------------+----------------+
 *  direction
 */
#define _IO(type,nr)		    _IOC(_IOC_NONE,(type),(nr),0)
#define _IOR(type,nr,size)	    _IOC(_IOC_READ,(type),(nr),(_IOC_TYPECHECK(size)))
#define _IOW(type,nr,size)	    _IOC(_IOC_WRITE,(type),(nr),(_IOC_TYPECHECK(size)))
#define _IOWR(type,nr,size)	    _IOC(_IOC_READ|_IOC_WRITE,(type),(nr),(_IOC_TYPECHECK(size)))
#define _IOR_BAD(type,nr,size)	_IOC(_IOC_READ,(type),(nr),sizeof(size))
#define _IOW_BAD(type,nr,size)	_IOC(_IOC_WRITE,(type),(nr),sizeof(size))
#define _IOWR_BAD(type,nr,size)	_IOC(_IOC_READ|_IOC_WRITE,(type),(nr),sizeof(size))

/* used to decode ioctl numbers..
 **
 *    31 30 29                   16 15             8 7               0
 *    +----+-----------------------+----------------+----------------+
 *    |dir |          size         |      type      |     number     |
 *    +----+-----------------------+----------------+----------------+
 *  direction
 */
#define _IOC_DIR(nr)        (((nr) >> _IOC_DIRSHIFT) & _IOC_DIRMASK)
#define _IOC_TYPE(nr)       (((nr) >> _IOC_TYPESHIFT) & _IOC_TYPEMASK)
#define _IOC_NR(nr)         (((nr) >> _IOC_NRSHIFT) & _IOC_NRMASK)
#define _IOC_SIZE(nr)       (((nr) >> _IOC_SIZESHIFT) & _IOC_SIZEMASK)

/* ...and for the drivers/sound files... */

#define IOC_IN		(_IOC_WRITE << _IOC_DIRSHIFT)
#define IOC_OUT		(_IOC_READ << _IOC_DIRSHIFT)
#define IOC_INOUT	((_IOC_WRITE|_IOC_READ) << _IOC_DIRSHIFT)
#define IOCSIZE_MASK	(_IOC_SIZEMASK << _IOC_SIZESHIFT)
#define IOCSIZE_SHIFT	(_IOC_SIZESHIFT)

#endif /* _UAPI_ASM_GENERIC_IOCTL_H */
