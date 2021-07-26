中断子系统
===============================

```
//arch/x86/kernel/head_64.S
early_idt_handler_array

```

# 1. 系统启动初始化调用图谱

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
            early_irq_init()
                arch_early_irq_init
                    arch_early_ioapic_init
            init_IRQ()
                x86_init.irqs.intr_init() => native_init_IRQ()
                    idt_setup_apic_and_irq_gates
                        idt_setup_from_table(idt_table, apic_idts, ...);
```

# 2. arch/x86/entry/entry_64.S

```c
	.align 16
	.globl __irqentry_text_start
__irqentry_text_start:

#include <asm/idtentry.h>

	.align 16
	.globl __irqentry_text_end
__irqentry_text_end:
```

# 3. arch\x86\include\asm\idtentry.h

展开请见源文件，分为 
```c
#ifdef __ASSEMBLY__
//在 arch/x86/entry/entry_64.S 中展开
#else
//在 arch/x86/include/asm/traps.h 中的头文件
#endif
```



