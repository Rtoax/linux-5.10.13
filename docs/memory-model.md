内存模型
===================

[https://zhuanlan.zhihu.com/p/220068494](https://zhuanlan.zhihu.com/p/220068494)

内存对于OS来说就像我们生活中的水和电，这么重要的资源管理起来是很花心思的。我们知道Linux中的物理内存被按页框划分，每个页框都会对应一个struct page结构体存放元数据，也就是说每块页框大小的内存都要花费sizeof(struct page)个字节进行管理。

所以系统会有大量的struct page，在linux的历史上出现过三种内存模型去管理它们。依次是：

* 平坦内存模型(flat memory model)、
* 不连续内存模型 (discontiguous memory model)；
* 稀疏内存模型(sparse memory model)。

新的内存模型的一次次被提出，无非因为是老的内存模型已不适应计算机硬件的新技术(例如：NUMA技术、内存热插拔等)。

内存模型的设计则主要是权衡以下两点(空间与时间)：

1. 尽量少的消耗内存去管理众多的struct page
2. pfn_to_page和page_to_pfn的转换效率。


# 1. 平坦内存模型(flat memory model)

FLATMEM内存模型是Linux最早使用的内存模型，那时计算机的内存通常不大。Linux会使用一个struct page mem_map[x]的数组根据PFN去依次存放所有的strcut page，且mem_map也位于内核空间的线性映射区，所以根据PFN(页帧号)即可轻松的找到目标页帧的strcut page

# 2. 不连续内存模型 (discontiguous memory model)

对于物理地址空间不存在空洞(holes)的计算机来说，FLATMEM无疑是最优解。可物理地址中若是存在空洞的话，FLATMEM就显得格外的浪费内存，因为FLATMEM会在mem_map数组中为所有的物理地址都创建一个struct page，即使大块的物理地址是空洞，即不存在物理内存。可是为这些空洞这些struct page完全是没有必要的。为了解决空洞的问题，Linux社区提出了DISCONTIGMEM模型。

DISCONTIGMEM是个稍纵即逝的内存模型，在SPARSEMEM出现后即被完全替代，且当前的Linux kernel默认都是使用SPARSEMEM，所以介绍DISCONTIGMEM的意义不大，感兴趣可以看这篇文章：[https://lwn.net/Articles/789304/](https://lwn.net/Articles/789304/)

# 3. 稀疏内存模型(sparse memory model)

稀疏内存模型是当前内核默认的选择，从2005年被提出后沿用至今，但中间经过几次优化，包括：CONFIG_SPARSEMEM_VMEMMAP和CONFIG_SPARSEMEM_EXTREME的引入，这两个配置通常是被打开的，下面的原理介绍也会基于它们开启的情况。

首次引入SPARSEMEM时的commit。[https://lwn.net/Articles/134804/](https://lwn.net/Articles/134804/) 原文中阐明了它的三个优点：

1. 可以解决内存空洞导致的内存浪费。
2. 支持内存的热插拔(memory hotplug)。
3. 支持nodes间的overlap。

# 4. SPARSEMEM 稀疏内存模型原理

更多请参见： 

1. [https://zhuanlan.zhihu.com/p/220068494](https://zhuanlan.zhihu.com/p/220068494)
2. [https://rtoax.blog.csdn.net/article/details/119038040](https://rtoax.blog.csdn.net/article/details/119038040)






