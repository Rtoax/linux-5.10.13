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
* request_mem_region() 	注册接口：

上面两个接口均调用`__request_region()`；

## 释放

* release_region()
* release_mem_region()

## 访问

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

# 相关系统调用

* ioperm()
* iopl()

# /dev/port

访问IO端口

# /proc

* /proc/ioports
* /proc/iomem