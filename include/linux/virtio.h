/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VIRTIO_H
#define _LINUX_VIRTIO_H
/* Everything a virtio driver needs to work with any particular virtio
 * implementation. */
#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/gfp.h>

/**
 * virtqueue - a queue to register buffers for sending or receiving.
 * @list: the chain of virtqueues for this device
 * @callback: the function to call when buffers are consumed (can be NULL).
 * @name: the name of this virtqueue (mainly for debugging)
 * @vdev: the virtio device this queue was created for.
 * @priv: a pointer for the virtqueue implementation to use.
 * @index: the zero-based ordinal number for this queue.
 * @num_free: number of elements we expect to be able to fit.
 *
 * A note on @num_free: with indirect buffers, each buffer needs one
 * element in the queue, otherwise a buffer will need one element per
 * sg element.
 *
 * struct vring_virtqueue;
 *
 * 在virtio协议中，所有的设备都使用virtqueue来进行数据的传输。每个设备可以有 0 个或者多个
 * virtqueue，每个virtqueue占用2个或者更多个4K的物理页。virtqueue有 Split Virtqueues
 * 和 Packed Virtqueues 两种模式，在Split virtqueues模式下virtqueue被分成若干个部分，
 * 每个部分都是前端驱动或者后端单向可写的（不能两端同时写）。每个virtqueue都有一个 16bit 的
 * queue size参数，表示队列的总长度。
 *
 * 每个virtqueue由3个部分组成：
 * +-------------------+--------------------------------+-----------------------+
 * | Descriptor Table  |   Available Ring  (padding)    |       Used Ring       |
 * +-------------------+--------------------------------+-----------------------+
 * Descriptor Table：存放IO传输请求信息；
 * Available Ring：记录了Descriptor Table表中的I/O请求下发信息，前端Driver可写后端只读；
 * Used Ring：记录Descriptor Table表中已被提交到硬件的信息，前端Driver只读后端可写。
 *
 * 整个virtio协议中设备IO请求的工作机制可以简单地概括为：
 * 1. 前端驱动将IO请求放到 Descriptor Table中，然后将索引更新到 Available Ring 中，最后
 *    kick后端去取数据；
 * 2. 后端取出IO请求进行处理，然后将结果刷新到 Descriptor Table 中再更新 Using Ring，
 *    然后发送中断notify前端。
 */
struct virtqueue {
    /**
     *  这个设备的 virtqueues 的链表
     */
	struct list_head list;
    /**
     *  当buffers 被消费，这个函数将被调用
     */
	void (*callback)(struct virtqueue *vq);
    /**
     *  这个 virtqueue 的名称
     */
	const char *name;
    /**
     *  virtio 设备
     */
	struct virtio_device *vdev;
    /**
     *  从0开始的 索引
     */
	unsigned int index;

    /**
     *  期望能匹配的 元素个数
     */
	unsigned int num_free;
    /**
     *  要使用的 virtqueue 实现的指针。
     */
	void *priv;
};

int virtqueue_add_outbuf(struct virtqueue *vq,
			 struct scatterlist sg[], unsigned int num,
			 void *data,
			 gfp_t gfp);

int virtqueue_add_inbuf(struct virtqueue *vq,
			struct scatterlist sg[], unsigned int num,
			void *data,
			gfp_t gfp);

int virtqueue_add_inbuf_ctx(struct virtqueue *vq,
			    struct scatterlist sg[], unsigned int num,
			    void *data,
			    void *ctx,
			    gfp_t gfp);

int virtqueue_add_sgs(struct virtqueue *vq,
		      struct scatterlist *sgs[],
		      unsigned int out_sgs,
		      unsigned int in_sgs,
		      void *data,
		      gfp_t gfp);

bool virtqueue_kick(struct virtqueue *vq);

bool virtqueue_kick_prepare(struct virtqueue *vq);

bool virtqueue_notify(struct virtqueue *vq);

void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len);

void *virtqueue_get_buf_ctx(struct virtqueue *vq, unsigned int *len,
			    void **ctx);

void virtqueue_disable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb(struct virtqueue *vq);

unsigned virtqueue_enable_cb_prepare(struct virtqueue *vq);

bool virtqueue_poll(struct virtqueue *vq, unsigned);

bool virtqueue_enable_cb_delayed(struct virtqueue *vq);

void *virtqueue_detach_unused_buf(struct virtqueue *vq);

unsigned int virtqueue_get_vring_size(struct virtqueue *vq);

bool virtqueue_is_broken(struct virtqueue *vq);

const struct vring *virtqueue_get_vring(struct virtqueue *vq);
dma_addr_t virtqueue_get_desc_addr(struct virtqueue *vq);
dma_addr_t virtqueue_get_avail_addr(struct virtqueue *vq);
dma_addr_t virtqueue_get_used_addr(struct virtqueue *vq);

/**
 * virtio_device - representation of a device using virtio
 * @index: unique position on the virtio bus
 * @failed: saved value for VIRTIO_CONFIG_S_FAILED bit (for restore)
 * @config_enabled: configuration change reporting enabled
 * @config_change_pending: configuration change reported while disabled
 * @config_lock: protects configuration change reporting
 * @dev: underlying device.
 * @id: the device type identification (used to match it with a driver).
 * @config: the configuration ops for this device.
 * @vringh_config: configuration ops for host vrings.
 * @vqs: the list of virtqueues for this device.
 * @features: the features supported by both driver and device.
 * @priv: private pointer for the driver's use.
 *
 * 一个使用 virtio 的设备
 *
 * 组成一个virtio设备的四要素包括：
 * 1. 设备状态域，见 VIRTIO_CONFIG_S_ACKNOWLEDGE ...
 * 2. feature bits，
 * 3. 设备配置空间，
 * 4. 一个或者多个virtqueue。
 */
struct virtio_device {
    /**
     *  在 virtio bus总线上的唯一位置
     */
	int index;
    /**
     *
     */
	bool failed;
	bool config_enabled;
	bool config_change_pending;
	spinlock_t config_lock;
    /**
     *  底层设备
     */
	struct device dev;
    /**
     *  设备标识
     */
	struct virtio_device_id id; /* 设备，厂商 */
	const struct virtio_config_ops *config; /* 配置 virtio 的操作 */
	const struct vringh_config_ops *vringh_config;  /* host VRing */
	struct list_head vqs;

	/**
	 * feature bits: 用来标志设备支持哪个特性
	 *
	 * bit0-bit23 是特定设备可以使用的 feature bits
	 * bit24-bit37 预给队列和 feature 协商机制
	 * bit38 以上保留给未来其他用途
	 *
	 * 例如：对于 virtio-net 设备而言，feature bit0 表示网卡设备支持 checksum 校验。
	 */
	u64 features;

	void *priv;
};

static inline struct virtio_device *dev_to_virtio(struct device *_dev)
{
	return container_of(_dev, struct virtio_device, dev);
}

void virtio_add_status(struct virtio_device *dev, unsigned int status);
int register_virtio_device(struct virtio_device *dev);
void unregister_virtio_device(struct virtio_device *dev);
bool is_virtio_device(struct device *dev);

void virtio_break_device(struct virtio_device *dev);

void virtio_config_changed(struct virtio_device *dev);
void virtio_config_disable(struct virtio_device *dev);
void virtio_config_enable(struct virtio_device *dev);
int virtio_finalize_features(struct virtio_device *dev);
#ifdef CONFIG_PM_SLEEP
int virtio_device_freeze(struct virtio_device *dev);
int virtio_device_restore(struct virtio_device *dev);
#endif

size_t virtio_max_dma_size(struct virtio_device *vdev);

#define virtio_device_for_each_vq(vdev, vq) \
	list_for_each_entry(vq, &vdev->vqs, list)

/**
 * virtio_driver - operations for a virtio I/O driver
 * @driver: underlying device driver (populate name and owner).
 * @id_table: the ids serviced by this driver.
 * @feature_table: an array of feature numbers supported by this driver.
 * @feature_table_size: number of entries in the feature table array.
 * @feature_table_legacy: same as feature_table but when working in legacy mode.
 * @feature_table_size_legacy: number of entries in feature table legacy array.
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @scan: optional function to call after successful probe; intended
 *    for virtio-scsi to invoke a scan.
 * @remove: the function to call when a device is removed.
 * @config_changed: optional function to call when the device configuration
 *    changes; may be called in interrupt context.
 * @freeze: optional function to call during suspend/hibernation.
 * @restore: optional function to call on resume.
 */
struct virtio_driver {  /* virtio 驱动 */
	struct device_driver driver;
	const struct virtio_device_id *id_table;
	const unsigned int *feature_table;
	unsigned int feature_table_size;
	const unsigned int *feature_table_legacy;
	unsigned int feature_table_size_legacy;
	int (*validate)(struct virtio_device *dev);
	int (*probe)(struct virtio_device *dev);
	void (*scan)(struct virtio_device *dev);
	void (*remove)(struct virtio_device *dev);
	void (*config_changed)(struct virtio_device *dev);
#ifdef CONFIG_PM
	int (*freeze)(struct virtio_device *dev);
	int (*restore)(struct virtio_device *dev);
#endif
};

static inline struct virtio_driver *drv_to_virtio(struct device_driver *drv)
{
	return container_of(drv, struct virtio_driver, driver);
}

int register_virtio_driver(struct virtio_driver *drv);
void unregister_virtio_driver(struct virtio_driver *drv);

/* module_virtio_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_virtio_driver(__virtio_driver) \
	module_driver(__virtio_driver, register_virtio_driver, \
			unregister_virtio_driver)
#endif /* _LINUX_VIRTIO_H */
