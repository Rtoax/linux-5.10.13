/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __KVM_IODEV_H__
#define __KVM_IODEV_H__

#include <linux/kvm_types.h>
#include <linux/errno.h>

struct kvm_io_device;
struct kvm_vcpu;

/**
 * kvm_io_device_ops are called under kvm slots_lock.
 * read and write handlers return 0 if the transaction has been handled,
 * or non-zero to have it passed to the next device.
 **/
struct kvm_io_device_ops {
	int (*read)(struct kvm_vcpu *vcpu,
		    struct kvm_io_device *this,
		    gpa_t addr,
		    int len,
		    void *val);
	int (*write)(struct kvm_vcpu *vcpu,
		     struct kvm_io_device *this,
		     gpa_t addr,
		     int len,
		     const void *val);
	void (*destructor)(struct kvm_io_device *this);
};


struct kvm_io_device {
	const struct kvm_io_device_ops *ops;
};

static inline void kvm_iodevice_init(struct kvm_io_device *dev,
				     const struct kvm_io_device_ops *ops)
{
	dev->ops = ops;
}

static inline int kvm_iodevice_read(struct kvm_vcpu *vcpu,
				    struct kvm_io_device *dev, gpa_t addr,
				    int l, void *v)
{
	return dev->ops->read ? dev->ops->read(vcpu, dev, addr, l, v)
				: -EOPNOTSUPP;
}

/**
 * 对于一个设备而言，仅仅简单把源操作数赋值给目的操作数指向的地址还不够，因为写寄存器
 * 的操作可能伴随一些副作用，需要设备做一些额外的操作。比如：对于 APIC 而言，写 icr
 * 寄存器可能需要 LAPIC 向另外一个处理器发出 IPI 中断，因此，还需要调用设备的相应
 * 处理函数。
 */
static inline int kvm_iodevice_write(struct kvm_vcpu *vcpu,
				     struct kvm_io_device *dev, gpa_t addr,
				     int l, const void *v)
{
	/**
	 * apic_mmio_ops: apic_mmio_write()
	 * kvm_io_gic_ops: dispatch_mmio_write()
	 * ...
	 */
	return dev->ops->write ? dev->ops->write(vcpu, dev, addr, l, v)
				 : -EOPNOTSUPP;
}

static inline void kvm_iodevice_destructor(struct kvm_io_device *dev)
{
	if (dev->ops->destructor)
		dev->ops->destructor(dev);
}

#endif /* __KVM_IODEV_H__ */
