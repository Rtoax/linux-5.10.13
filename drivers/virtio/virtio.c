// SPDX-License-Identifier: GPL-2.0-only
#include <linux/virtio.h>
#include <linux/spinlock.h>
#include <linux/virtio_config.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <uapi/linux/virtio_ids.h>

/* Unique numbering for virtio devices. */
static DEFINE_IDA(virtio_index_ida);

static ssize_t device_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sprintf(buf, "0x%04x\n", dev->id.device);
}
static DEVICE_ATTR_RO(device);

static ssize_t vendor_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sprintf(buf, "0x%04x\n", dev->id.vendor);
}
static DEVICE_ATTR_RO(vendor);

static ssize_t status_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sprintf(buf, "0x%08x\n", dev->config->get_status(dev));
}
static DEVICE_ATTR_RO(status);

static ssize_t modalias_show(struct device *_d,
			     struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sprintf(buf, "virtio:d%08Xv%08X\n",
		       dev->id.device, dev->id.vendor);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t features_show(struct device *_d,
			     struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	unsigned int i;
	ssize_t len = 0;

	/* We actually represent this as a bitstring, as it could be
	 * arbitrary length in future. */
	for (i = 0; i < sizeof(dev->features)*8; i++)
		len += sprintf(buf+len, "%c",
			       __virtio_test_bit(dev, i) ? '1' : '0');
	len += sprintf(buf+len, "\n");
	return len;
}
static DEVICE_ATTR_RO(features);

static struct attribute *virtio_dev_attrs[] = {
	&dev_attr_device.attr,
	&dev_attr_vendor.attr,
	&dev_attr_status.attr,
	&dev_attr_modalias.attr,
	&dev_attr_features.attr,
	NULL,
};
ATTRIBUTE_GROUPS(virtio_dev);

static inline int virtio_id_match(const struct virtio_device *dev,
				  const struct virtio_device_id *id)
{
	if (id->device != dev->id.device && id->device != VIRTIO_DEV_ANY_ID)
		return 0;

	return id->vendor == VIRTIO_DEV_ANY_ID || id->vendor == dev->id.vendor;
}

/* This looks through all the IDs a driver claims to support.  If any of them
 * match, we return 1 and the kernel will call virtio_dev_probe(). */
static int virtio_dev_match(struct device *_dv, struct device_driver *_dr)
{
	unsigned int i;
	struct virtio_device *dev = dev_to_virtio(_dv);
	const struct virtio_device_id *ids;

	ids = drv_to_virtio(_dr)->id_table;
	for (i = 0; ids[i].device; i++)
		if (virtio_id_match(dev, &ids[i]))
			return 1;
	return 0;
}

static int virtio_uevent(struct device *_dv, struct kobj_uevent_env *env)
{
	struct virtio_device *dev = dev_to_virtio(_dv);

	return add_uevent_var(env, "MODALIAS=virtio:d%08Xv%08X",
			      dev->id.device, dev->id.vendor);
}

void virtio_check_driver_offered_feature(const struct virtio_device *vdev,
					 unsigned int fbit)
{
	unsigned int i;
	struct virtio_driver *drv = drv_to_virtio(vdev->dev.driver);

	for (i = 0; i < drv->feature_table_size; i++)
		if (drv->feature_table[i] == fbit)
			return;

	if (drv->feature_table_legacy) {
		for (i = 0; i < drv->feature_table_size_legacy; i++)
			if (drv->feature_table_legacy[i] == fbit)
				return;
	}

	BUG();
}
EXPORT_SYMBOL_GPL(virtio_check_driver_offered_feature);

static void __virtio_config_changed(struct virtio_device *dev)
{
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);

	if (!dev->config_enabled)
		dev->config_change_pending = true;
	else if (drv && drv->config_changed)
		drv->config_changed(dev);
}

void virtio_config_changed(struct virtio_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->config_lock, flags);
	__virtio_config_changed(dev);
	spin_unlock_irqrestore(&dev->config_lock, flags);
}
EXPORT_SYMBOL_GPL(virtio_config_changed);

void virtio_config_disable(struct virtio_device *dev)
{
	spin_lock_irq(&dev->config_lock);
	dev->config_enabled = false;
	spin_unlock_irq(&dev->config_lock);
}
EXPORT_SYMBOL_GPL(virtio_config_disable);

void virtio_config_enable(struct virtio_device *dev)
{
	spin_lock_irq(&dev->config_lock);
	dev->config_enabled = true;
	if (dev->config_change_pending)
		__virtio_config_changed(dev);
	dev->config_change_pending = false;
	spin_unlock_irq(&dev->config_lock);
}
EXPORT_SYMBOL_GPL(virtio_config_enable);

void virtio_add_status(struct virtio_device *dev, unsigned int status)
{
	might_sleep();
	dev->config->set_status(dev, dev->config->get_status(dev) | status);
}
EXPORT_SYMBOL_GPL(virtio_add_status);

/**
 * 设置 VIRTIO_CONFIG_S_FEATURES_OK 特性位，这之后， virtio 驱动就不会再接受新的
 * 特性了，
 */
int virtio_finalize_features(struct virtio_device *dev)
{
	int ret = dev->config->finalize_features(dev);
	unsigned status;

	might_sleep();
	if (ret)
		return ret;

	ret = arch_has_restricted_virtio_memory_access();
	if (ret) {
		if (!virtio_has_feature(dev, VIRTIO_F_VERSION_1)) {
			dev_warn(&dev->dev,
				 "device must provide VIRTIO_F_VERSION_1\n");
			return -ENODEV;
		}

		if (!virtio_has_feature(dev, VIRTIO_F_ACCESS_PLATFORM)) {
			dev_warn(&dev->dev,
				 "device must provide VIRTIO_F_ACCESS_PLATFORM\n");
			return -ENODEV;
		}
	}

	if (!virtio_has_feature(dev, VIRTIO_F_VERSION_1))
		return 0;

	virtio_add_status(dev, VIRTIO_CONFIG_S_FEATURES_OK);
	status = dev->config->get_status(dev);

	/**
	 * 确保设置了 VIRTIO_CONFIG_S_FEATURES_OK, 否则设备不支持 virtio 驱动设备的
	 * 一些状态，表示设备不可用。
	 */
	if (!(status & VIRTIO_CONFIG_S_FEATURES_OK)) {
		dev_err(&dev->dev, "virtio: device refuses features: %x\n",
			status);
		return -ENODEV;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_finalize_features);

/**
 * 13:40:48 38224    kworker/u16:1    vde
 *
 *        device_add+1
 *        device_add_disk+222
 *        virtblk_probe+1128
 *        virtio_dev_probe+431
 *        really_probe+505
 *
 * 如果给虚拟机添加一个 blk qcow2 盘，这个函数会被调用一次。
 */
static int virtio_dev_probe(struct device *_d)
{
	int err, i;
	struct virtio_device *dev = dev_to_virtio(_d);
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);
	u64 device_features;
	u64 driver_features;
	u64 driver_features_legacy;

	/**
	 * We have a driver!
	 * 设置 VIRTIO_CONFIG_S_DRIVER 状态位，表示 virtio 驱动已经知道了怎么驱动该设备.
	 */
	virtio_add_status(dev, VIRTIO_CONFIG_S_DRIVER);

	/**
	 * Figure out what features the device supports.
	 *
	 * 读取 virtio 后端设备的 feature 位，求出驱动设置的 features,将两者计算交集，然后
	 * 向设备写入这个交集特性，
	 */
	device_features = dev->config->get_features(dev);

	/* Figure out what features the driver supports. */
	driver_features = 0;
	for (i = 0; i < drv->feature_table_size; i++) {
		unsigned int f = drv->feature_table[i];
		BUG_ON(f >= 64);
		driver_features |= (1ULL << f);
	}

	/* Some drivers have a separate feature table for virtio v1.0 */
	if (drv->feature_table_legacy) {
		driver_features_legacy = 0;
		for (i = 0; i < drv->feature_table_size_legacy; i++) {
			unsigned int f = drv->feature_table_legacy[i];
			BUG_ON(f >= 64);
			driver_features_legacy |= (1ULL << f);
		}
	} else {
		driver_features_legacy = driver_features;
	}

	if (device_features & (1ULL << VIRTIO_F_VERSION_1))
		dev->features = driver_features & device_features;
	else
		dev->features = driver_features_legacy & device_features;

	/* Transport features always preserved to pass to finalize_features. */
	for (i = VIRTIO_TRANSPORT_F_START; i < VIRTIO_TRANSPORT_F_END; i++)
		if (device_features & (1ULL << i))
			__virtio_set_bit(dev, i);

	if (drv->validate) {
		err = drv->validate(dev);
		if (err)
			goto err;
	}

	/**
	 * feature set协商，取交集
	 *
	 * 设置 VIRTIO_CONFIG_S_FEATURES_OK 特性位，这之后，virtio 驱动就不会再接受新的
	 * 特性了，
	 */
	err = virtio_finalize_features(dev);
	if (err)
		goto err;

	/**
	 * 调用特定virtio设备的驱动程序probe，执行设备相关的初始化工作，包括发现设备的
	 * virtqueue，读写 virtio 设备的配置空间等。
	 *
	 * 可能如下：
	 * virtnet_probe()
	 * virtblk_probe()
	 * virtballoon_probe()
	 */
	err = drv->probe(dev);
	if (err)
		goto err;

	/**
	 * If probe didn't do it, mark device DRIVER_OK ourselves.
	 *
	 * 设置 VIRTIO_CONFIG_S_DRIVER_OK 状态位，这通常是在具体设备驱动的 probe 函数
	 * 中通过调用 virtio_device_ready() 完成的。对于 virtio balloon 来说，是
	 * virtballoon_probe() 完成的。如果设备驱动没有设置 DRIVER_OK 状态位，则会
	 * 在此由总线的 probe 函数来设置。
	 */
	if (!(dev->config->get_status(dev) & VIRTIO_CONFIG_S_DRIVER_OK))
		virtio_device_ready(dev);

	if (drv->scan)
		drv->scan(dev);

	virtio_config_enable(dev);

	return 0;
err:
	virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);
	return err;

}

static int virtio_dev_remove(struct device *_d)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);

	virtio_config_disable(dev);

	drv->remove(dev);

	/* Driver should have reset device. */
	WARN_ON_ONCE(dev->config->get_status(dev));

	/* Acknowledge the device's existence again. */
	virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);
	return 0;
}

/**
 *
 */
static struct bus_type virtio_bus = {
	.name  = "virtio",
	.match = virtio_dev_match,
	.dev_groups = virtio_dev_groups,
	.uevent = virtio_uevent,
	.probe = virtio_dev_probe,
	.remove = virtio_dev_remove,
};

int register_virtio_driver(struct virtio_driver *driver)
{
	/* Catch this early. */
	BUG_ON(driver->feature_table_size && !driver->feature_table);
	driver->driver.bus = &virtio_bus;
	return driver_register(&driver->driver);
}
EXPORT_SYMBOL_GPL(register_virtio_driver);

void unregister_virtio_driver(struct virtio_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(unregister_virtio_driver);

/**
 * register_virtio_device - register virtio device
 * @dev        : virtio device to be registered
 *
 * On error, the caller must call put_device on &@dev->dev (and not kfree),
 * as another code path may have obtained a reference to @dev.
 *
 * Returns: 0 on suceess, -error on failure
 *
 * 注册的 vritio 设备(句柄)在 QEMU 中，向上用来和 Guest 中的 驱动交互，向下和 KVM 交互。
 * 在打开 QEMU VM 的时候，此函数不会被调用。
 * virtio 设备是后端，驱动是前端
 *
 * @kstacks[
    device_add+1
    register_virtio_device+199
    virtio_pci_probe+179
    local_pci_probe+69
    pci_device_probe+257
    really_probe+505
    __driver_probe_device+214
    driver_probe_device+30
    __device_attach_driver+133
    bus_for_each_drv+126
    __device_attach+188
    pci_bus_add_device+74
    pci_bus_add_devices+44
    enable_slot+805
    acpiphp_check_bridge.part.0+279
    hotplug_event+518
    acpiphp_hotplug_notify+34
    acpi_device_hotplug+138
    acpi_hotplug_work_fn+61
    process_one_work+491
    worker_thread+83
    kthread+295
    ret_from_fork+34
 */
int register_virtio_device(struct virtio_device *dev)
{
	int err;

	dev->dev.bus = &virtio_bus;

	/**
	 * 初始化 struct device {} <赋值>
	 */
	device_initialize(&dev->dev);

	/* Assign a unique device index and hence name. */
	err = ida_simple_get(&virtio_index_ida, 0, 0, GFP_KERNEL);
	if (err < 0)
		goto out;

	dev->index = err;

	/**
	 * 比如
	 * /sys/devices/pci0000:00/0000:00:03.0/0000:09:00.0/virtio8/block/vdb
	 *                                                   ^^^^^^^
	 */
	dev_set_name(&dev->dev, "virtio%u", dev->index);

	spin_lock_init(&dev->config_lock);
	dev->config_enabled = false;
	dev->config_change_pending = false;

	/**
	 * We always start by resetting the device, in case a previous
	 * driver messed it up.  This also tests that code path a little.
	 *
	 * 重置设备
	 */
	dev->config->reset(dev);

	/**
	 * Acknowledge that we've seen the device.
	 *
	 * 设置 VIRTIO_CONFIG_S_ACKNOWLEDGE 状态位，表示 virtio 驱动已经知道了该设备
	 */
	virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);

	INIT_LIST_HEAD(&dev->vqs);

	/*
	 * device_add() causes the bus infrastructure to look for a matching
	 * driver.
	 *
	 * 添加设备，并找到合适的驱动程序
	 */
	err = device_add(&dev->dev);
	if (err)
		ida_simple_remove(&virtio_index_ida, dev->index);
out:
	if (err)
		virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);
	return err;
}
EXPORT_SYMBOL_GPL(register_virtio_device);

bool is_virtio_device(struct device *dev)
{
	return dev->bus == &virtio_bus;
}
EXPORT_SYMBOL_GPL(is_virtio_device);

void unregister_virtio_device(struct virtio_device *dev)
{
	int index = dev->index; /* save for after device release */

	device_unregister(&dev->dev);
	ida_simple_remove(&virtio_index_ida, index);
}
EXPORT_SYMBOL_GPL(unregister_virtio_device);

#ifdef CONFIG_PM_SLEEP
int virtio_device_freeze(struct virtio_device *dev)
{
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);

	virtio_config_disable(dev);

	dev->failed = dev->config->get_status(dev) & VIRTIO_CONFIG_S_FAILED;

	if (drv && drv->freeze)
		return drv->freeze(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(virtio_device_freeze);

int virtio_device_restore(struct virtio_device *dev)
{
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);
	int ret;

	/* We always start by resetting the device, in case a previous
	 * driver messed it up. */
	dev->config->reset(dev);

	/* Acknowledge that we've seen the device. */
	virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);

	/* Maybe driver failed before freeze.
	 * Restore the failed status, for debugging. */
	if (dev->failed)
		virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);

	if (!drv)
		return 0;

	/* We have a driver! */
	virtio_add_status(dev, VIRTIO_CONFIG_S_DRIVER);

	ret = virtio_finalize_features(dev);
	if (ret)
		goto err;

	if (drv->restore) {
		ret = drv->restore(dev);
		if (ret)
			goto err;
	}

	/* Finally, tell the device we're all set */
	virtio_add_status(dev, VIRTIO_CONFIG_S_DRIVER_OK);

	virtio_config_enable(dev);

	return 0;

err:
	virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);
	return ret;
}
EXPORT_SYMBOL_GPL(virtio_device_restore);
#endif

static int virtio_init(void)
{
	if (bus_register(&virtio_bus) != 0)
		panic("virtio bus registration failed");
	return 0;
}

static void __exit virtio_exit(void)
{
	bus_unregister(&virtio_bus);
	ida_destroy(&virtio_index_ida);
}
core_initcall(virtio_init);
module_exit(virtio_exit);

MODULE_LICENSE("GPL");
