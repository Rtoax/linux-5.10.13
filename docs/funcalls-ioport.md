IO 端口
======================

# 数据结构

* struct resource

# 全局变量

* ioport_resource:	"PCI IO"
* iomem_resource:	"PCI mem"


# 接口API

## 注册 

* request_region()		注册接口：这个函数告诉内核，我们要使用 起始于 first 的 n 个 端口，
* request_mem_region() 	注册接口：IO内存的分配和映射

上面两个接口均调用`__request_region()`；

## 释放

* release_region()
* release_mem_region()	IO内存的释放

## 建立与移除映射

* ioremap()	不应该直接引用该函数返回的地址，该函数是体系结构相关的。
* ioremap_nocache() # 老函数，已经没有了
* iounmap()


## 访问端口

### 单个数据

* inb()
* outb()
* inw()
* outw()
* inl()
* outl()

暂停式的 IO 函数

> 如果有数据丢失的情况，可以使用 暂停式 IO 函数

* inb_p()
* outb_p()
* inw_p()
* outw_p()
* inl_p()
* outl_p()

### 串操作

* insb()
* outsb()
* insw()
* outsw()
* insl()
* outsl()

暂停式的 IO 函数

* insb_p()
* outsb_p()
* insw_p()
* outsw_p()
* insl_p()
* outsl_p()


## 访问内存

### 读

* ioread8()
* ioread16()
* ioread32()
* [...]

### 写

* iowrite8()
* iowrite16()
* iowrite32()
* [...]

### 读写多个值

* ioread8_rep()
* ioread16_rep()
* ioread32_rep()

* iowrite8_rep()
* iowrite16_rep()
* iowrite32_rep()
* [...]


### 拷贝内存

* memset_io()
* memcpy_fromio()
* memcpy_toio()

## 陈旧的接口

不推荐使用下面的接口。

* readb()
* readw()
* readl()
* writeb()
* writew()
* writel()
* [...]

## 像 IO 内存一样使用端口

* ioport_map()
* ioport_unmap()
* [...]


# 相关系统调用

* ioperm()
* iopl()

# /dev/port

访问IO端口

# /proc

* /proc/ioports
* /proc/iomem
