/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  include/linux/eventfd.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_EVENTFD_H
#define _LINUX_EVENTFD_H

#include <linux/fcntl.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/percpu-defs.h>
#include <linux/percpu.h>

/*
 * CAREFUL: Check include/uapi/asm-generic/fcntl.h when defining
 * new flags, since they might collide with O_* ones. We want
 * to re-use O_* flags that couldn't possibly have a meaning
 * from eventfd, in order to leave a free define-space for
 * shared O_* flags.
 *
 * EFD_SEMAPHORE： 一次只能处理一个,在 man 中有：
 *
 * If EFD_SEMAPHORE was specified and the eventfd counter has a nonzero value,
 * then a read(2) returns 8 bytes containing the value 1, and the counter's
 * value is decremented by 1.
 */
#define EFD_SEMAPHORE (1 << 0)
#define EFD_CLOEXEC O_CLOEXEC
#define EFD_NONBLOCK O_NONBLOCK

#define EFD_SHARED_FCNTL_FLAGS (O_CLOEXEC | O_NONBLOCK)
#define EFD_FLAGS_SET (EFD_SHARED_FCNTL_FLAGS | EFD_SEMAPHORE)

struct eventfd_ctx;
struct file;

#ifdef CONFIG_EVENTFD

void eventfd_ctx_put(struct eventfd_ctx *ctx);
struct file *eventfd_fget(int fd);
struct eventfd_ctx *eventfd_ctx_fdget(int fd);
struct eventfd_ctx *eventfd_ctx_fileget(struct file *file);
__u64 eventfd_signal(struct eventfd_ctx *ctx, __u64 n);
int eventfd_ctx_remove_wait_queue(struct eventfd_ctx *ctx, wait_queue_entry_t *wait,
				  __u64 *cnt);

DECLARE_PER_CPU(int, eventfd_wake_count);

static inline bool eventfd_signal_count(void)
{
	return this_cpu_read(eventfd_wake_count);
}

#else /* CONFIG_EVENTFD */

/*
 * Ugly ugly ugly error layer to support modules that uses eventfd but
 * pretend to work in !CONFIG_EVENTFD configurations. Namely, AIO.
 */
    /**
         *
         */
#endif

#endif /* _LINUX_EVENTFD_H */

