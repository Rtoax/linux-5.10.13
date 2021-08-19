IO 端口
======================

# 数据结构

* struct resource

# 全局变量

* ioport_resource:	"PCI IO"
* iomem_resource:	"PCI mem"


# 接口API

* request_region()		注册接口：这个函数告诉内核，我们要使用 起始于 first 的 n 个 端口，
* request_mem_region() 	注册接口：

* release_region()
* release_mem_region()

上面两个接口均调用`__request_region()`；



