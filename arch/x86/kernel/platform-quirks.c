// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/bios_ebda.h>

//quirk
//
//n.双关话；遁辞；讥讽话；奇想
//v.嘲讽；使弯曲；挥鞭急打；【建】在(装饰线条等)上作成深槽
//网络怪癖；怪异；怪异模式

void __init x86_early_init_platform_quirks(void)
{
	x86_platform.legacy.i8042 = X86_LEGACY_I8042_EXPECTED_PRESENT;
	x86_platform.legacy.rtc = 1;
	x86_platform.legacy.warm_reset = 1;
	x86_platform.legacy.reserve_bios_regions = 0;
	x86_platform.legacy.devices.pnpbios = 1;

	switch (boot_params.hdr.hardware_subarch) {
	case X86_SUBARCH_PC:    /*  */
		x86_platform.legacy.reserve_bios_regions = 1;
		break;
	case X86_SUBARCH_XEN:   /* xen */
		x86_platform.legacy.devices.pnpbios = 0;
		x86_platform.legacy.rtc = 0;
		break;
	case X86_SUBARCH_INTEL_MID: /* 手机网络设备 */
	case X86_SUBARCH_CE4100:    /*  */
		x86_platform.legacy.devices.pnpbios = 0;
		x86_platform.legacy.rtc = 0;
		x86_platform.legacy.i8042 = X86_LEGACY_I8042_PLATFORM_ABSENT;
		break;
	}

	if (x86_platform.set_legacy_features)   /* legacy:（计算机系统或产品）已停产的 */
		x86_platform.set_legacy_features();
}

bool __init x86_pnpbios_disabled(void)
{
	return x86_platform.legacy.devices.pnpbios == 0;
}

#if defined(CONFIG_PNPBIOS)
bool __init arch_pnpbios_disabled(void)
{
	return x86_pnpbios_disabled();
}
#endif
