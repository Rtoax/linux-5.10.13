硬中断
====================

# 数据结构


# 接口API

* request_irq(): 
* free_irq():	
* can_request_irq(): 查询某个中断线是否可用
* probe_irq_on():	返回一个 未分配中断 的位掩码
* probe_irq_off():	生成一个中断号

1. 调用`request_irq()`的正确位置应该是在设备第一次打开、硬件被告知产生中断之前；
2. 调用`free_irq()`的位置应该在最后一次关闭设备、硬件被告知不在中断处理之后；


例：
```
  native_init_IRQ()
    request_irq(2, ...)
```

# /proc

## /proc/interrupts

* proc_interrupts_init()
* show_interrupts()

