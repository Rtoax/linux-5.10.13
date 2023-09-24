preempt - 抢占
==============


# 内核抢占

在 [sched.md](./sched.md) 中介绍了调度。

内核的几种抢占模型:

* `CONFIG_PREEMPT_NONE`：不支持抢占，中断退出后，需要等到低优先级任务主动让出CPU才发生抢占切换；
* `CONFIG_PREEMPT_VOLUNTARY`：自愿抢占，代码中增加抢占点，在中断退出后遇到抢占点时进行抢占切换；
    * PREEMPT_VOLUNTARY 适用于有桌面的环境
* `CONFIG_PREEMPT_DYNAMIC`:
* `CONFIG_PREEMPT`：抢占，当中断退出后，如果遇到了更高优先级的任务，立即进行任务抢占；
    * kernel 2.6 引入;
    * CONFIG_PREEMPT 打开后可以在任何位置抢占，除非代码中禁止了本地中断；
    * 一个无限循环不会抢占整个系统;
    * CONFIG_PREEMPT 则可以用于桌面或者嵌入式;
* `CONFIG_PREEMPT_RT`：
* `CONFIG_PREEMPTION`:

用户空间进程总是可抢占的，例如，用户空间写了一个无限循环，也不能够阻塞系统。


* 图例

`==` TaskA 低优先级任务
`##` ISR
`$$` TaskB 高优先级任务

```
---------------------------------------------> CPU time
CONFIG_PREEMPT_NONE:

      |####|
|=====|    |====|$$$$$$$$$|
                ^
                低优先级任务主动让出 CPU 时抢占

CONFIG_PREEMPT_VOLUNTARY:

      |####|
|=====|    |====|$$$$$$$$$|
                ^
                抢占点：might_sleep(), might_resched()

CONFIG_PREEMPT

      |####|
|=====|    |$$$$$$$$$|
           ^
           中断退出，立即抢占
```


# CONFIG_PREEMPT

```
preempt_schedule()
    preempt_schedule_common()
        while (need_resched())
            __schedule(true)

el1_irq: asm
    preempt_schedule_irq()
        while (need_resched())
            preempt_disable();
            local_irq_enable();
            __schedule(true);
            local_irq_disable();
            sched_preempt_enable_no_resched();
```

# preempt_count

* x86: 使用 __preempt_count 全局变量
* arm64: 在 struct thread_info {} 中

```c
/**
 * 31              24  23   20 19    16 15             8 7               0
 *  +----------------+--------+--------+----------------+----------------+
 *  |                | f00000 | f0000  |   0x0000ff00   |   0x000000ff   |
 *  +----------------+--------+--------+----------------+----------------+
 *                   |        |        |                |                |
 *                   |        |        |                |                |
 *                   |        |        |                |                +----
 *                   |        |        |                |                   PREEMPT_MASK
 *                   |        |        |                +---------------------
 *                   |        |        |                                    SOFTIRQ_MASK  in_softirq()
 *                   |        |        +--------------------------------------
 *                   |        |                                             HARDIRQ_MASK  in_irq()
 *                   |        +-----------------------------------------------
 *                   |                                                      NMI_MASK      in_nmi()
 *                   +--------------------------------------------------------
 *
 * <-----------------|----------------------------------|---------------------->
 *   in_task()       |         in_interrupt()           |     in_task()
 */
```

# Links

- https://kernelnewbies.org/FAQ/Preemption
- https://blog.csdn.net/tiantao2012/article/details/56840183
