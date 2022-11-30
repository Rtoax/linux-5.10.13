loadavg
=======


# CPU负载

目前内核中，有以下几种方式来跟踪CPU负载：

* 全局CPU平均负载；
* 运行队列CPU负载；
* PELT（per entity load tracking）；

# 全局CPU平均负载

先来明确两个与CPU负载计算相关的概念：

1. `active task（活动任务）`：只有知道活动任务数量，才能计算CPU负载，而活动任务包括了TASK_RUNNING和TASK_UNINTERRUPTIBLE两类任务。包含TASK_UNINTERRUPTIBLE任务的原因是，这类任务经常是在等待I/O请求，将其包含在内也合理；
2. `NO_HZ`：我们都知道Linux内核每隔固定时间发出timer interrupt，而HZ是用来定义1秒中的timer interrupts次数，HZ的倒数是tick，是系统的节拍器，每个tick会处理包括调度器、时间管理、定时器等事务。周期性的时钟中断带来的问题是，不管CPU空闲或繁忙都会触发，会带来额外的系统损耗，因此引入了NO_HZ模式，可以在CPU空闲时将周期性时钟关掉。在NO_HZ期间，活动任务数量的改变也需要考虑，而它的计算不如周期性时钟模式下直观。


```
kernel/sched/core.c:
scheduler_tick()
  calc_global_load_tick()
    calc_load_fold_active()
    atomic_long_add(delta, &calc_load_tasks);
```

```
tick_nohz_stop_sched_tick() -> tick_nohz_stop_tick() -> calc_load_nohz_start()
tick_nohz_restart_sched_tick() -> calc_load_nohz_stop()
```

## 负载计算

```
do_timer()
  calc_global_load()
```


# 运行队列CPU负载

> Linux系统会计算每个tick的平均CPU负载，并将其存储在运行队列中 rq->cpu_load[5]，用于负载均衡；


# PELT（per entity load tracking）

