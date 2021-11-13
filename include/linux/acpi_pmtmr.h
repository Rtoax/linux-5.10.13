/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ACPI_PMTMR_H_
#define _ACPI_PMTMR_H_

#include <linux/clocksource.h>

/* Number of PMTMR ticks expected during calibration run */
#define PMTMR_TICKS_PER_SEC 3579545

//+-------------------------------+----------------------------------+
//|                               |                                  |
//|  upper eight bits of a        |      running count of the        |
//| 32-bit power management timer |     power management timer       |
//|                               |                                  |
//+-------------------------------+----------------------------------+
//31          E_TMR_VAL           24               TMR_VAL           0

/* limit it to 24 bits */
#define ACPI_PM_MASK CLOCKSOURCE_MASK(24)

/* Overrun value */
#define ACPI_PM_OVRRUN	(1<<24)

#ifdef CONFIG_X86_PM_TIMER

extern u32 acpi_pm_read_verified(void);
extern u32 pmtmr_ioport;    //extended address of the `Power Management Timer Control Register Block`

static inline u32 acpi_pm_read_early(void)
{
	if (!pmtmr_ioport)
		return 0;
	/* mask the output to 24 bits */
	return acpi_pm_read_verified() & ACPI_PM_MASK;
}

#else
/*  */
#endif

#endif

