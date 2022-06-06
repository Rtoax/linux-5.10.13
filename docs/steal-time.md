Steal-Time
===========

# 分析

当前Host的墙上时间是HWT1，此时Guest中的墙上时间GWT1，如果是同一个时区的话，此时
HWT1和GWT1是相等的。

如果此时Host中发生了调度，Guest所在的qemu进程不执行了，那么HWT1将继续增长，GWT1
是否应该增长呢？

如果GWT1不增长，那么等到Guest继续执行的时候，就会继续在原来的GWT1基础上增长，那
么HWT2到HWT1之间的时间就发生了丢失；现象就是Guest中的时间变慢了。

如果GWT1同时增长，那么就会在Guest进程切回来继续执行的时候，Guest中的时间会瞬间增
大了HWT2减掉HWT1的差值。现象就是Guest的墙上时间是对的了。

可是新的问题又来了：在Guest的quemu进程被Host切换之前，Guest中刚刚切换走redis，开
始执行Nginx；等到Guest继续执行的时候，因为Guest中的时钟跳变增大了很多，Guest会认
为Nginx执行了大量的CPU时间。如果Guest中是Linux采用cfs调度算法的话，那么Nginx下次
被调度会隔比较长的时间。可是实际呢，Nginx根本没有得到执行！！！

# Steal Time

为了解决上述的Guest中的调度问题，就引入了steal time。

Steal time的原理就是：告诉Guest，哪些时间被Host给steal了，调度的时候，忽略这部分
时间，就可以正确调度了。

所以，基本就是两个部分：

1. 在Host中通知Guest具体的steal time是多少；
2. 在Guest中处理这些时间，修正因时间跳变引起的调度错误。
