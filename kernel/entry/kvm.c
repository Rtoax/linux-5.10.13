// SPDX-License-Identifier: GPL-2.0

#include <linux/entry-kvm.h>
#include <linux/kvm_host.h>

static int xfer_to_guest_mode_work(struct kvm_vcpu *vcpu, unsigned long ti_work)
{
	do {
		int ret;

		if (ti_work & _TIF_SIGPENDING) {
			kvm_handle_signal_exit(vcpu);
			return -EINTR;
		}

		/**
		 * 设置了 TIF_NEED_RESCHED 标志，表明需要发生抢占调度，自己可以被其他人抢占；
		 */
		if (ti_work & _TIF_NEED_RESCHED)
			schedule();

		if (ti_work & _TIF_NOTIFY_RESUME)
			tracehook_notify_resume(NULL);

		ret = arch_xfer_to_guest_mode_handle_work(vcpu, ti_work);
		if (ret)
			return ret;

		ti_work = READ_ONCE(current_thread_info()->flags);
	} while (ti_work & XFER_TO_GUEST_MODE_WORK || need_resched());
	return 0;
}

int xfer_to_guest_mode_handle_work(struct kvm_vcpu *vcpu)
{
	unsigned long ti_work;

	/*
	 * This is invoked from the outer guest loop with interrupts and
	 * preemption enabled.
	 *
	 * KVM invokes xfer_to_guest_mode_work_pending() with interrupts
	 * disabled in the inner loop before going into guest mode. No need
	 * to disable interrupts here.
	 */
	ti_work = READ_ONCE(current_thread_info()->flags);
	if (!(ti_work & XFER_TO_GUEST_MODE_WORK))
		return 0;

	return xfer_to_guest_mode_work(vcpu, ti_work);
}
EXPORT_SYMBOL_GPL(xfer_to_guest_mode_handle_work);
