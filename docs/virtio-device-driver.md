VirtIO 设备和驱动
===============


# VirtIO 驱动初始化一个设备的过程

1. 重置设备:
```
    register_virtio_device() dev->config->reset()
```

2. 设置 VIRTIO_CONFIG_S_ACKNOWLEDGE 状态位，表示 virtio 驱动已经知道了该设备:
```
    register_virtio_device()
        virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE)
```

3. 设置 VIRTIO_CONFIG_S_DRIVER 状态位，表示 virtio 驱动已经知道了怎么驱动该设备:
```
    virtio_dev_probe()
        virtio_add_status(dev, VIRTIO_CONFIG_S_DRIVER);
```

4. 读取 virtio 设备的 feature 位，求出驱动设置的 features,将两者计算交集，然后向设备写入这个交集特性，然后调用 virtio_finalize_features(), 见: virtio_dev_probe()

5. 设置 VIRTIO_CONFIG_S_FEATURES_OK 特性位，这之后， virtio 驱动就不会再接受新的特性了
```
   virtio_finalize_features()
        virtio_add_status(dev, VIRTIO_CONFIG_S_FEATURES_OK);
```

6. 确保设置了 VIRTIO_CONFIG_S_FEATURES_OK, 否则设备不支持 virtio 驱动设备的一些状态，表示设备不可用。
```
   virtio_finalize_features()
```

7. 执行设备相关的初始化工作，包括发现设备的 virtqueue，读写 virtio 设备的配置空间等。
```
   virtio_dev_probe()
        drv->probe(dev);
```

8. 设置 VIRTIO_CONFIG_S_DRIVER_OK 状态位，这通常是在具体设备驱动的 probe 函数中通过调用 virtio_device_ready() 完成的。对于 virtio balloon 来说，是 virtballoon_probe() 完成的。如果设备驱动没有设置 DRIVER_OK 状态位，则会在此由总线的 probe 函数来设置。
```
   virtio_dev_probe()
        drv->probe(dev) = virtballoon_probe()
    或者
    virtio_dev_probe()
        virtio_device_ready()
```


# VirtIO 驱动的初始化





