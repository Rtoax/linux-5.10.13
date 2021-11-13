/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_GENERIC_POLL_H
#define __ASM_GENERIC_POLL_H

/* These are specified by iBCS2 */
/**
 *  如果设备可以无阻塞的读取，就设置该位
 */
#define POLLIN		0x0001
/**
 *  可以无阻塞的读取高优先级的数据
 */
#define POLLPRI		0x0002

#define POLLOUT		0x0004
/**
 *  设备发生了错误。
 *  如果 调用 poll，就会报告设备即可读也可写，因为读写都会无阻塞的返回一个错误码
 */
#define POLLERR		0x0008

/**
 *  当读取设备的进程到达文件末尾时，驱动程序必须设置 POLLHUP(挂起)位。
 *  依照 select() 的功能介绍，调用 select 的进程会被告知设备是可读的。
 */
#define POLLHUP		0x0010
#define POLLNVAL	0x0020

/* The rest seem to be more-or-less nonstandard. Check them! */
/**
 *  如果 “通常” 的数据已经就绪，可以读取，就设置该位
 */
#define POLLRDNORM	0x0040
/**
 *  指示从设备读取 out-of-band 带外数据
 *  通常不用于设备驱动。
 *  只在套接字中有效。
 */
#define POLLRDBAND	0x0080
/**
 *  
 */
#ifndef POLLWRNORM
#define POLLWRNORM	0x0100
#endif
/**
 *  只在套接字中有效。
 */
#ifndef POLLWRBAND
#define POLLWRBAND	0x0200
#endif
#ifndef POLLMSG
#define POLLMSG		0x0400
#endif
#ifndef POLLREMOVE
#define POLLREMOVE	0x1000
#endif
#ifndef POLLRDHUP
#define POLLRDHUP       0x2000
#endif

#define POLLFREE	(__force __poll_t)0x4000	/* currently only for epoll */

#define POLL_BUSY_LOOP	(__force __poll_t)0x8000

struct pollfd {
	int fd;
	short events;
	short revents;
};

#endif	/* __ASM_GENERIC_POLL_H */
