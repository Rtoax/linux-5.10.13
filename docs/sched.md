sched
=====


# 调度类型

## 主动调度 - schedule()

> kernel/sched/core.c: schedule()


## 周期调度 - scheduler_tick()

> kernel/sched/core.c: scheduler_tick()


## 高精度时钟调度 - hrtick()

> kernel/sched/core.c: hrtick()


## 进程唤醒时调度 - wake_up_process()

> kernel/sched/core.c: wake_up_process()


# 进程切换

内核是通过设置TIF_NEED_RESCHED标志来对进程进行标记的，设置该位则表明需要进行调度切换，而实际的切换将在抢占执行点来完成。

* set_tsk_need_resched()
* need_resched()


## set_tsk_need_resched 设置 TIF_NEED_RESCHED 标志

1. 睡眠进程唤醒时设置 TIF_NEED_RESCHED 标志，造成睡眠的原因可能是等待资源、同步等原因；
   * try_to_wake_up()
2. 中断处理检测时间片时，其中包括高精度时钟中断和调度时钟中断
   * hrtick()
   * scheduler_tick()
3. 任务在 CPU 之间进行迁移时；
   * migrate_swap()
4. 修改任务的 nice 值、调整优先级时；
   * nice(2)
   * setpriority()

设置了 TIF_NEED_RESCHED 标志，表明需要发生抢占调度；


## 抢占点

用户抢占：抢占执行发生在进程处于用户态。抢占的执行，最明显的标志就是调用了schedule()函数，来完成任务的切换。具体来说，在用户态执行抢占在以下几种情况：

* 异常处理后返回到用户态；
* 中断处理后返回到用户态；
* 系统调用后返回到用户态；

> ARMv8有4个Exception Level，其中用户程序运行在EL0，OS运行在EL1，Hypervisor运行在EL2，Secure monitor运行在EL3；
> 用户程序在执行过程中，遇到异常或中断后，将会跳到ENTRY(vectors)向量表处开始执行；
> 返回用户空间时进行标志位判断，设置了TIF_NEED_RESCHED则需要进行调度切换，没有设置该标志，则检查是否有收到信号，有信号未处理的话，还需要进行信号的处理操作；


### arm64

* SYM_CODE_START(vectors)
    * el0_irq
    * el0_sync

最终调用:

```
ret_to_user: asm
    do_notify_resume()
        if (thread_flags & _TIF_NEED_RESCHED)
            schedule();
```


# 内核抢占

[preempt.md](./preempt.md)


# 参考链接

* [（三）Linux进程调度器-进程切换](https://mp.weixin.qq.com/s/_5FcTa_W19ZrjVs_QWRH3w)
