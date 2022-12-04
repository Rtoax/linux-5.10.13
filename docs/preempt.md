preempt - 抢占
==============


# 内核抢占

在 [sched.md](./sched.md) 中介绍了调度。

内核的几种抢占模型:

* `CONFIG_PREEMPT_NONE`：不支持抢占，中断退出后，需要等到低优先级任务主动让出CPU才发生抢占切换；
* `CONFIG_PREEMPT_VOLUNTARY`：自愿抢占，代码中增加抢占点，在中断退出后遇到抢占点时进行抢占切换；
* `CONFIG_PREEMPT`：抢占，当中断退出后，如果遇到了更高优先级的任务，立即进行任务抢占；
* `CONFIG_PREEMPT_RT`：


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
