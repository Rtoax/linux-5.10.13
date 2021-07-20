/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_PTRACE_H
#define _UAPI_LINUX_PTRACE_H
/* ptrace.h */
/* structs and defines to help the user use the ptrace system call. */

/* has the defines to get at the registers. */

#include <linux/types.h>

enum __ptrace_request {
#if 0/* +++ 对应下面的宏定义 */
/* Type of the REQUEST argument to `ptrace.'  */
  /* Indicate that the process making this request should be traced.
     All signals received by this process can be intercepted by its
     parent, and its parent can use the other `ptrace' requests.  */
  PTRACE_TRACEME = 0,
#define PT_TRACE_ME PTRACE_TRACEME

  /* Return the word in the process's text space at address ADDR.  */
  PTRACE_PEEKTEXT = 1,
#define PT_READ_I PTRACE_PEEKTEXT

  /* Return the word in the process's data space at address ADDR.  */
  PTRACE_PEEKDATA = 2,
#define PT_READ_D PTRACE_PEEKDATA

  /* Return the word in the process's user area at offset ADDR.  */
  PTRACE_PEEKUSER = 3,
#define PT_READ_U PTRACE_PEEKUSER

  /* Write the word DATA into the process's text space at address ADDR.  */
  PTRACE_POKETEXT = 4,
#define PT_WRITE_I PTRACE_POKETEXT

  /* Write the word DATA into the process's data space at address ADDR.  */
  PTRACE_POKEDATA = 5,
#define PT_WRITE_D PTRACE_POKEDATA

  /* Write the word DATA into the process's user area at offset ADDR.  */
  PTRACE_POKEUSER = 6,
#define PT_WRITE_U PTRACE_POKEUSER

  /* Continue the process.  */
  PTRACE_CONT = 7,
#define PT_CONTINUE PTRACE_CONT

  /* Kill the process.  */
  PTRACE_KILL = 8,
#define PT_KILL PTRACE_KILL

  /* Single step the process.
     This is not supported on all machines.  */
  PTRACE_SINGLESTEP = 9,
#define PT_STEP PTRACE_SINGLESTEP

  /* Get all general purpose registers used by a processes.
     This is not supported on all machines.  */
   PTRACE_GETREGS = 12,
#define PT_GETREGS PTRACE_GETREGS

  /* Set all general purpose registers used by a processes.
     This is not supported on all machines.  */
   PTRACE_SETREGS = 13,
#define PT_SETREGS PTRACE_SETREGS

  /* Get all floating point registers used by a processes.
     This is not supported on all machines.  */
   PTRACE_GETFPREGS = 14,
#define PT_GETFPREGS PTRACE_GETFPREGS

  /* Set all floating point registers used by a processes.
     This is not supported on all machines.  */
   PTRACE_SETFPREGS = 15,
#define PT_SETFPREGS PTRACE_SETFPREGS

  /* Attach to a process that is already running. */
  PTRACE_ATTACH = 16,
#define PT_ATTACH PTRACE_ATTACH

  /* Detach from a process attached to with PTRACE_ATTACH.  */
  PTRACE_DETACH = 17,
#define PT_DETACH PTRACE_DETACH

  /* Get all extended floating point registers used by a processes.
     This is not supported on all machines.  */
   PTRACE_GETFPXREGS = 18,
#define PT_GETFPXREGS PTRACE_GETFPXREGS

  /* Set all extended floating point registers used by a processes.
     This is not supported on all machines.  */
   PTRACE_SETFPXREGS = 19,
#define PT_SETFPXREGS PTRACE_SETFPXREGS

  /* Continue and stop at the next (return from) syscall.  */
  PTRACE_SYSCALL = 24,
#define PT_SYSCALL PTRACE_SYSCALL

  /* Set ptrace filter options.  */
  PTRACE_SETOPTIONS = 0x4200,
#define PT_SETOPTIONS PTRACE_SETOPTIONS

  /* Get last ptrace message.  */
  PTRACE_GETEVENTMSG = 0x4201,
#define PT_GETEVENTMSG PTRACE_GETEVENTMSG

  /* Get siginfo for process.  */
  PTRACE_GETSIGINFO = 0x4202,
#define PT_GETSIGINFO PTRACE_GETSIGINFO

  /* Set new siginfo for process.  */
  PTRACE_SETSIGINFO = 0x4203,
#define PT_SETSIGINFO PTRACE_SETSIGINFO

  /* Get register content.  */
  PTRACE_GETREGSET = 0x4204,
#define PTRACE_GETREGSET PTRACE_GETREGSET

  /* Set register content.  */
  PTRACE_SETREGSET = 0x4205,
#define PTRACE_SETREGSET PTRACE_SETREGSET

  /* Like PTRACE_ATTACH, but do not force tracee to trap and do not affect
     signal or group stop state.  */
  PTRACE_SEIZE = 0x4206,
#define PTRACE_SEIZE PTRACE_SEIZE

  /* Trap seized tracee.  */
  PTRACE_INTERRUPT = 0x4207,
#define PTRACE_INTERRUPT PTRACE_INTERRUPT

  /* Wait for next group event.  */
  PTRACE_LISTEN = 0x4208,
#define PTRACE_LISTEN PTRACE_LISTEN

  PTRACE_PEEKSIGINFO = 0x4209,
#define PTRACE_PEEKSIGINFO PTRACE_PEEKSIGINFO

  PTRACE_GETSIGMASK = 0x420a,
#define PTRACE_GETSIGMASK PTRACE_GETSIGMASK

  PTRACE_SETSIGMASK = 0x420b,
#define PTRACE_SETSIGMASK PTRACE_SETSIGMASK

  PTRACE_SECCOMP_GET_FILTER = 0x420c
#define PTRACE_SECCOMP_GET_FILTER PTRACE_SECCOMP_GET_FILTER
#endif
};

#define PTRACE_TRACEME		   0
#define PTRACE_PEEKTEXT		   1
#define PTRACE_PEEKDATA		   2
#define PTRACE_PEEKUSR		   3
#define PTRACE_POKETEXT		   4
#define PTRACE_POKEDATA		   5
#define PTRACE_POKEUSR		   6
#define PTRACE_CONT		   7
#define PTRACE_KILL		   8
#define PTRACE_SINGLESTEP	   9

#define PTRACE_ATTACH		  16
#define PTRACE_DETACH		  17

#define PTRACE_SYSCALL		  24

/* 0x4200-0x4300 are reserved for architecture-independent additions.  */
#define PTRACE_SETOPTIONS	0x4200
#define PTRACE_GETEVENTMSG	0x4201
#define PTRACE_GETSIGINFO	0x4202
#define PTRACE_SETSIGINFO	0x4203

/*
 * Generic ptrace interface that exports the architecture specific regsets
 * using the corresponding NT_* types (which are also used in the core dump).
 * Please note that the NT_PRSTATUS note type in a core dump contains a full
 * 'struct elf_prstatus'. But the user_regset for NT_PRSTATUS contains just the
 * elf_gregset_t that is the pr_reg field of 'struct elf_prstatus'. For all the
 * other user_regset flavors, the user_regset layout and the ELF core dump note
 * payload are exactly the same layout.
 *
 * This interface usage is as follows:
 *	struct iovec iov = { buf, len};
 *
 *	ret = ptrace(PTRACE_GETREGSET/PTRACE_SETREGSET, pid, NT_XXX_TYPE, &iov);
 *
 * On the successful completion, iov.len will be updated by the kernel,
 * specifying how much the kernel has written/read to/from the user's iov.buf.
 */
#define PTRACE_GETREGSET	0x4204
#define PTRACE_SETREGSET	0x4205

#define PTRACE_SEIZE		0x4206
#define PTRACE_INTERRUPT	0x4207
#define PTRACE_LISTEN		0x4208

#define PTRACE_PEEKSIGINFO	0x4209

struct ptrace_peeksiginfo_args {
	__u64 off;	/* from which siginfo to start */
	__u32 flags;
	__s32 nr;	/* how may siginfos to take */
};

#define PTRACE_GETSIGMASK	0x420a
#define PTRACE_SETSIGMASK	0x420b

#define PTRACE_SECCOMP_GET_FILTER	0x420c
#define PTRACE_SECCOMP_GET_METADATA	0x420d

struct seccomp_metadata {
	__u64 filter_off;	/* Input: which filter */
	__u64 flags;		/* Output: filter's flags */
};

#define PTRACE_GET_SYSCALL_INFO		0x420e
#define PTRACE_SYSCALL_INFO_NONE	0
#define PTRACE_SYSCALL_INFO_ENTRY	1
#define PTRACE_SYSCALL_INFO_EXIT	2
#define PTRACE_SYSCALL_INFO_SECCOMP	3

struct ptrace_syscall_info {
	__u8 op;	/* PTRACE_SYSCALL_INFO_* */
	__u8 pad[3];
	__u32 arch;
	__u64 instruction_pointer;
	__u64 stack_pointer;
	union {
		struct {
			__u64 nr;
			__u64 args[6];
		} entry;
		struct {
			__s64 rval;
			__u8 is_error;
		} exit;
		struct {
			__u64 nr;
			__u64 args[6];
			__u32 ret_data;
		} seccomp;
	};
};

/*
 * These values are stored in task->ptrace_message
 * by tracehook_report_syscall_* to describe the current syscall-stop.
 */
#define PTRACE_EVENTMSG_SYSCALL_ENTRY	1
#define PTRACE_EVENTMSG_SYSCALL_EXIT	2

/* Read signals from a shared (process wide) queue */
#define PTRACE_PEEKSIGINFO_SHARED	(1 << 0)

/* Wait extended result codes for the above trace options.  */
#define PTRACE_EVENT_FORK	1   /* 通过fork() */
#define PTRACE_EVENT_VFORK	2
#define PTRACE_EVENT_CLONE	3
#define PTRACE_EVENT_EXEC	4
#define PTRACE_EVENT_VFORK_DONE	5
#define PTRACE_EVENT_EXIT	6
#define PTRACE_EVENT_SECCOMP	7   /* Security Computing: 限制系统调用 */
/* Extended result codes which enabled by means other than options.  */
#define PTRACE_EVENT_STOP	128

/* Options set using PTRACE_SETOPTIONS or using PTRACE_SEIZE @data param */
#define PTRACE_O_TRACESYSGOOD	1
#define PTRACE_O_TRACEFORK	(1 << PTRACE_EVENT_FORK)
#define PTRACE_O_TRACEVFORK	(1 << PTRACE_EVENT_VFORK)
#define PTRACE_O_TRACECLONE	(1 << PTRACE_EVENT_CLONE)
#define PTRACE_O_TRACEEXEC	(1 << PTRACE_EVENT_EXEC)
#define PTRACE_O_TRACEVFORKDONE	(1 << PTRACE_EVENT_VFORK_DONE)
#define PTRACE_O_TRACEEXIT	(1 << PTRACE_EVENT_EXIT)
#define PTRACE_O_TRACESECCOMP	(1 << PTRACE_EVENT_SECCOMP)

/* eventless options */
#define PTRACE_O_EXITKILL		(1 << 20)
#define PTRACE_O_SUSPEND_SECCOMP	(1 << 21)

#define PTRACE_O_MASK		(\
	0x000000ff | PTRACE_O_EXITKILL | PTRACE_O_SUSPEND_SECCOMP)

#include <asm/ptrace.h>


#endif /* _UAPI_LINUX_PTRACE_H */
