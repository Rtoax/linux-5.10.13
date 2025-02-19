/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__ASM_GENERIC_SIGNAL_H
#define _UAPI__ASM_GENERIC_SIGNAL_H

#include <linux/types.h>

#define _NSIG		64
#define _NSIG_BPW	__BITS_PER_LONG
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGIOT		 6

/**
 *  SIGBUS
 *
 *  1) 硬件故障，不用说，程序员最常碰上的肯定不是这种情形。
 *
 *  2) Linux平台上执行malloc()，如果没有足够的RAM，Linux不是让malloc()失败返回，
 *     而是向当前进程分发SIGBUS信号。
 *
 *     注: 对该点执怀疑态度，有机会可自行测试确认当前系统反应。
 *
 *  3) 某些架构上访问数据时有对齐的要求，比如只能从4字节边界上读取一个4字节的
 *     数据类型。IA-32架构没有硬性要求对齐，尽管未对齐的访问降低执行效率。另外
 *     一些架构，比如SPARC、m68k，要求对齐访问，否则向当前进程分发SIGBUS信号。
 */
#define SIGBUS		 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGUSR1		10
#define SIGSEGV		11
#define SIGUSR2		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGSTKFLT	16
#define SIGCHLD		17  /* 在一个进程终止或者停止时，将SIGCHLD信号发送给其父进程 */
#define SIGCONT		18
#define SIGSTOP		19
#define SIGTSTP		20
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGURG		23
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM	26
#define SIGPROF		27
#define SIGWINCH	28
#define SIGIO		29
#define SIGPOLL		SIGIO
/*
#define SIGLOST		29
*/
#define SIGPWR		30
#define SIGSYS		31
#define	SIGUNUSED	31

/* These should not be considered constants from userland.  */
/**
 * SIGRTMIN 是实时信号（Real-time signals）中的最小编号信号。在 POSIX 标准中，实时信号
 * 范围从 SIGRTMIN 到 SIGRTMAX。实时信号与标准信号不同，它们可以排队，并且每个信号可以携带
 * 附加信息。
 *
 * 实时信号通常由用户进程或应用程序使用，用于特定的应用需求。以下是一些典型的使用场景：
 *
 * 进程间通信：实时信号可以用于进程间通信，传递比标准信号更多的信息。
 * 异步事件通知：某些库或系统服务可能使用实时信号来通知进程异步事件发生。例如，高性能 I/O 操
 *             作完成时的通知。
 * 定时器：通过 timer_create 和 timer_settime 创建的定时器到期时，可以选择发送实时信号。
 *
 * 实时信号的使用通常是由开发者在应用程序中显式定义和处理的。系统不会在没有明确配置的情况下自动
 * 发送实时信号。
 */
#define SIGRTMIN	32
#ifndef SIGRTMAX
#define SIGRTMAX	_NSIG
#endif

/*
 * SA_FLAGS values:
 *
 * SA_ONSTACK indicates that a registered stack_t will be used.
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_NOCLDSTOP flag to turn off SIGCHLD when children stop.
 * SA_RESETHAND clears the handler when the signal is delivered.
 * SA_NOCLDWAIT flag on SIGCHLD to inhibit zombies.
 * SA_NODEFER prevents the current signal from being masked in the handler.
 *
 * SA_ONESHOT and SA_NOMASK are the historical Linux names for the Single
 * Unix names RESETHAND and NODEFER respectively.
 */
#define SA_NOCLDSTOP	0x00000001
#define SA_NOCLDWAIT	0x00000002
#define SA_SIGINFO	0x00000004
#define SA_ONSTACK	0x08000000
#define SA_RESTART	0x10000000
#define SA_NODEFER	0x40000000
#define SA_RESETHAND	0x80000000

#define SA_NOMASK	SA_NODEFER
#define SA_ONESHOT	SA_RESETHAND

/*
 * New architectures should not define the obsolete
 *	SA_RESTORER	0x04000000
 */

#if !defined MINSIGSTKSZ || !defined SIGSTKSZ
#define MINSIGSTKSZ	2048
#define SIGSTKSZ	8192
#endif

#ifndef __ASSEMBLY__
typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

/* not actually used, but required for linux/syscalls.h */
typedef unsigned long old_sigset_t;

#include <asm-generic/signal-defs.h>

#ifdef SA_RESTORER
#define __ARCH_HAS_SA_RESTORER
#endif

#ifndef __KERNEL__
struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
#ifdef SA_RESTORER
	__sigrestore_t sa_restorer;
#endif
	sigset_t sa_mask;		/* mask last for extensibility */
};
#endif

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	size_t ss_size;
} stack_t;

#endif /* __ASSEMBLY__ */

#endif /* _UAPI__ASM_GENERIC_SIGNAL_H */
