// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOTE: This example is works on x86 and powerpc.
 * Here's a sample kernel module showing the use of kprobes to dump a
 * stack trace and selected registers when kernel_clone() is called.
 *
 * For more information on theory of operation of kprobes, see
 * Documentation/trace/kprobes.rst
 *
 * You will see the trace data in /var/log/messages and on the console
 * whenever kernel_clone() is invoked to create a new process.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>

#define MAX_SYMBOL_LEN	64
static char ___symbol[MAX_SYMBOL_LEN] = "kernel_clone";
module_param_string(___symbol, ___symbol, sizeof(___symbol), 0644);

/** 
 *  
 */
/* For each probe you need to allocate a kprobe structure */
static struct kprobe ____kp = {
	____kp.symbol_name	= ___symbol,
};

/* kprobe pre_handler: called just before the probed instruction is executed */
static int __kprobes handler_pre(struct kprobe *p, struct pt_regs *regs)
{
	pr_info("<%s> pre_handler: p->addr = 0x%p, ip = %lx, flags = 0x%lx\n",
		    p->symbol_name, p->addr, regs->ip, regs->flags);
	/* A dump_stack() here will give a stack backtrace */
	return 0;
}

/* kprobe post_handler: called after the probed instruction is executed */
static void __kprobes handler_post(struct kprobe *p, struct pt_regs *regs,
				unsigned long flags)
{
	pr_info("<%s> post_handler: p->addr = 0x%p, flags = 0x%lx\n",
		    p->symbol_name, p->addr, regs->flags);
}

/*
 * fault_handler: this is called if an exception is generated for any
 * instruction within the pre- or post-handler, or when Kprobes
 * single-steps the probed instruction.
 */
static int handler_fault(struct kprobe *p, struct pt_regs *regs, int trapnr)
{
	pr_info("fault_handler: p->addr = 0x%p, trap #%dn", p->addr, trapnr);
	/* Return 0 because we don't handle the fault. */
	return 0;
}
/* NOKPROBE_SYMBOL() is also available */
NOKPROBE_SYMBOL(handler_fault);

/** 
 *  
 */
static int __init kprobe_init(void)
{
	int ret;
	____kp.pre_handler = handler_pre;
	____kp.post_handler = handler_post;
	____kp.fault_handler = handler_fault;

    /** 
     *  注册
     */
	ret = register_kprobe(&____kp);
	if (ret < 0) {
		pr_err("register_kprobe failed, returned %d\n", ret);
		return ret;
	}
	pr_info("Planted kprobe at %p\n", ____kp.addr);
	return 0;
}

static void __exit kprobe_exit(void)
{
	unregister_kprobe(&____kp);
	pr_info("kprobe at %p unregistered\n", ____kp.addr);
}

module_init(kprobe_init)
module_exit(kprobe_exit)
MODULE_LICENSE("GPL");
