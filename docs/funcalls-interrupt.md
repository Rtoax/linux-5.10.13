中断子系统
===============================

```
//arch/x86/kernel/head_64.S
early_idt_handler_array

```

# 1. 初始化

## 1.1. 系统启动初始化调用图谱

```
x86_64_start_kernel
    idt_setup_early_handler
        set_intr_gate
        load_idt(&idt_descr);
    x86_64_start_reservations
        start_kernel
            setup_arch
                idt_setup_early_traps
                    idt_setup_from_table(idt_table, early_idts, ...); 调试(1)+断点(3)
                    load_idt(&idt_descr);
                idt_setup_early_pf
                    idt_setup_from_table(idt_table, early_pf_idts, ...); 缺页(14)
            trap_init
                idt_setup_traps
                    idt_setup_from_table(idt_table, def_idts, ...);
                idt_setup_ist_traps
                    idt_setup_from_table(idt_table, ist_idts, ...);
            sched_init
                init_sched_fair_class
                    open_softirq(SCHED_SOFTIRQ, run_rebalance_domains);
            early_irq_init()
                arch_early_irq_init
                    arch_early_ioapic_init
            init_IRQ()
                x86_init.irqs.intr_init() => native_init_IRQ()
                    idt_setup_apic_and_irq_gates
                        idt_setup_from_table(idt_table, apic_idts, ...);
            hrtimers_init
                open_softirq(HRTIMER_SOFTIRQ, hrtimer_run_softirq);
            init_timers
                open_softirq(TIMER_SOFTIRQ, run_timer_softirq);
            softirq_init()
                open_softirq(TASKLET_SOFTIRQ, tasklet_action);
                open_softirq(HI_SOFTIRQ, tasklet_hi_action);
```

* 子系统

```
net_dev_init
    open_softirq(NET_TX_SOFTIRQ, net_tx_action);    /* 发 */
    open_softirq(NET_RX_SOFTIRQ, net_rx_action);    /* 收 */

blk_mq_init
    open_softirq(BLOCK_SOFTIRQ, blk_done_softirq);

inet_init
    tcp_init
        tcp_tasklet_init
            tasklet_init(&tsq->tasklet, tcp_tasklet_func, (unsigned long)tsq);
```

## 1.2. arch/x86/entry/entry_64.S

```c
	.align 16
	.globl __irqentry_text_start
__irqentry_text_start:

#include <asm/idtentry.h>

	.align 16
	.globl __irqentry_text_end
__irqentry_text_end:
```

## 1.3. arch\x86\include\asm\idtentry.h

展开请见源文件，分为

```c
#ifdef __ASSEMBLY__
//在 arch/x86/entry/entry_64.S 中展开
#else
//在 arch/x86/include/asm/traps.h 中的头文件
#endif
```

# 2. 中断后半部

[《Linux内核深入理解中断和异常（7）：中断下半部：Softirq, Tasklets and Workqueues》](https://rtoax.blog.csdn.net/article/details/115213699)

* `0x00-0x1f` architecture-defined exceptions and interrupts;
* `0x30-0x3f` are used for ISA(Industry Standard Architecture) interrupts;
* `softirqs`是静态分配的，这对内核模块来说是不可加载的，这就引出了tasklets；
* `softirqs`实际上很少使用；
* `workqueue`运行在内核进程上下文中；
* `tasklets`运行在软件中断上下文中；
* `ksoftirqd`调度 softirqs -> spawn_ksoftirqd
* `kworker`调度workqueue


```

```

# /proc/interrupts

## LOC xxx Local timer interrupts

```c
//arch/x86/include/asm/idtentry.h:632
DECLARE_IDTENTRY_SYSVEC(LOCAL_TIMER_VECTOR,		sysvec_apic_timer_interrupt);
//arch/x86/kernel/idt.c:208:
INTG(LOCAL_TIMER_VECTOR,		asm_sysvec_apic_timer_interrupt),
```


处理侧

```bash
sysvec_apic_timer_interrupt
	__sysvec_apic_timer_interrupt
		trace_local_timer_entry(LOCAL_TIMER_VECTOR)
		local_apic_timer_interrupt
			inc_irq_stat(apic_timer_irqs)
			evt->event_handler(evt)
		trace_local_timer_exit(LOCAL_TIMER_VECTOR)
```

# 示例分析

## #UD 未定义的指令

```
asm_exc_invalid_op()
  exc_invalid_op()
    handle_invalid_op()
      do_error_trap(..., X86_TRAP_UD, SIGILL, ILL_ILLOPN, ...)
        do_trap()
```

> KVM #UD 见 funcalls-kvm-nmi.md


### KVM

```
vmx_x86_ops.handle_exit = vmx_handle_exit,

kvm_vmx_exit_handlers[] = {
  [EXIT_REASON_EXCEPTION_NMI] = handle_exception_nmi,
}

vmx_handle_exit()
  kvm_vmx_exit_handlers[EXIT_REASON_EXCEPTION_NMI] =
  handle_exception_nmi()
    # Guest中执行非法指令
    handle_ud() #UD
      if == ’ud2; .ascii "kvm"‘: skip
      else kvm_emulate_instruction()

kvm_emulate_instruction()
  x86_emulate_instruction()
```
