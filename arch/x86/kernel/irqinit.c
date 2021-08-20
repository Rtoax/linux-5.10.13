// SPDX-License-Identifier: GPL-2.0
#include <linux/linkage.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/timex.h>
#include <linux/random.h>
#include <linux/kprobes.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/device.h>
#include <linux/bitops.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pgtable.h>

#include <linux/atomic.h>
#include <asm/timer.h>
#include <asm/hw_irq.h>
#include <asm/desc.h>
#include <asm/io_apic.h>
#include <asm/acpi.h>
#include <asm/apic.h>
#include <asm/setup.h>
#include <asm/i8259.h>
#include <asm/traps.h>
#include <asm/prom.h>

/*
 * ISA PIC or low IO-APIC triggered (INTA-cycle or APIC) interrupts:
 * (these are usually mapped to vectors 0x30-0x3f)
 */

/*
 * The IO-APIC gives us many more interrupt sources. Most of these
 * are unused but an SMP system is supposed to have enough memory ...
 * sometimes (mostly wrt. hw bugs) we get corrupted vectors all
 * across the spectrum, so we really want to be prepared to get all
 * of these. Plus, more powerful systems might have more than 64
 * IO-APIC registers.
 *
 * (these are usually mapped into the 0x30-0xff vector range)
 */
    vector_irq_t vector_irq;/* 我加的 */
//`vector_irq` will be used during the first steps of an external hardware interrupt handling 
//in the `do_IRQ` function from the [arch/x86/kernel/irq.c]
DEFINE_PER_CPU(vector_irq_t, vector_irq) = {
	[0 ... NR_VECTORS - 1] = VECTOR_UNUSED,
};

//**ISA**(Industry Standard Architecture) interrupts;
void __init init_ISA_irqs(void)
{
    //definition of the `chip` variable
	struct irq_chip *chip = legacy_pic->chip;/* default is chip=&`i8259A_chip` */
	int i;

	/*
	 * Try to set up the through-local-APIC virtual wire mode earlier.
	 *
	 * On some 32-bit UP machines, whose APIC has been disabled by BIOS
	 * and then got re-enabled by "lapic", it hangs at boot time without this.
	 *
	 * makes initialization of the [APIC] of `bootstrap processor` (or processor which starts first)
	 */
	init_bsp_APIC();

	legacy_pic->init(0);    /* default is init=&`init_8259A()` */

	for (i = 0; i < nr_legacy_irqs(); i++)
		irq_set_chip_and_handler(i, chip, handle_level_irq);
}

//initialization of the `vector_irq` [percpu] variable
/**
 *  老版本的中断函数为 do_IRQ()
 *  新版本内核的中断为 common_interrupt()
 */
void __init init_IRQ(void)  /*  */
{
	int i;

	/*
	 * On cpu 0, Assign ISA_IRQ_VECTOR(irq) to IRQ 0..15.
	 * If these IRQ's are handled by legacy interrupt-controllers like PIC,
	 * then this configuration will likely be static after the boot. If
	 * these IRQs are handled by more modern controllers like IO-APIC,
	 * then this vector space can be freed and re-used dynamically as the
	 * irq's migrate etc.
	 *
	 * * `0x00-0x1f` architecture-defined exceptions and interrupts;
     * * `0x30-0x3f` are used for **ISA**(Industry Standard Architecture) interrupts;
	 */
	for (i = 0; i < nr_legacy_irqs(); i++)
		per_cpu(vector_irq, 0)[ISA_IRQ_VECTOR(i)] = irq_to_desc(i);

	BUG_ON(irq_init_percpu_irqstack(smp_processor_id()));

    /**
     *  native_init_IRQ()
     */
	x86_init.irqs.intr_init(); /* = native_init_IRQ() */
#if __rtoax_
    native_init_IRQ(); //+++ 为了看起来方便
#endif
}

/**
 *  `native_` prefix means that architecture-specific
 *  初始化本地 [APIC]
 */
void __init native_init_IRQ(void)
{
	/* Execute any quirks before the call gates are initialised: */
	x86_init.irqs.pre_vector_init(); /* = init_ISA_irqs() */

    //初始化本地 [APIC],executes general initialization of the [Local APIC]
	idt_setup_apic_and_irq_gates();

    // initialization of the [ISA](Industry Standard Architecture)
	lapic_assign_system_vectors();

    //`acpi_ioapic` variable represents existence of [I/O APIC]
    //`!of_ioapic` checks that we do not use [Open Firmware]
    //`nr_legacy_irqs()` checks that we do not use legacy interrupt controller
	if (!acpi_ioapic && !of_ioapic && nr_legacy_irqs()) {
		/* IRQ2 is cascade interrupt to second interrupt controller */
#ifdef rtoax_add //++++
        static struct irqaction irq2 = {
            irq2.handler = no_action,
            irq2.name = "cascade",
            irq2.flags = IRQF_NO_THREAD,
        };
#endif            
        /**
         *  
         */
		if (request_irq(2, no_action, IRQF_NO_THREAD, "cascade"/* 级 联 */, NULL))
			pr_err("%s: request_irq() failed\n", "cascade");
	}
}
