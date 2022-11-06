Try To Wake Up - TTWU
=====================

* ttwu_queue_wakelist(): insert to wake list
* sched_ttwu_pending(): remove from wake list

```
sched_ttwu_pending()    try_to_wake_up()
  STORE p->on_rq = 1     LOAD p->state
```


# Insert to Wake List

```
1. hrtimer_wakeup()/timer
2. syscall
3. interrupt return
  try_to_wake_up()
    ttwu_queue()
      ttwu_queue_wakelist()
        __ttwu_queue_wakelist()
          rq->ttwu_pending = 1
          __smp_call_single_queue(cpu, &p->wake_entry.llist)
      ttwu_do_activate() if ttwu_queue_wakelist failed(如下)
```


# Read from Wake List

```
1. do_idle()
2. __schedule()
  sched_ttwu_pending()
    llist_for_each_entry_safe(p, t, llist, wake_entry.llist)
      ttwu_do_activate() 
        activate_task()
          enqueue_task()
            enqueue_task_fair() - CFS
              enqueue_entity()
                update_curr(cfs_rq) - 更新 vruntime
                update_load_avg() - 更新 loadavg
                __enqueue_entity()
                  rb_link_node() - 插入 cfs_rq 红黑树运行队列
                se->on_rq = 1;
              hrtick_update()
        ttwu_do_wakeup()
          p->state = TASK_RUNNING
```


# When to dequeue_entity()

```
1. schedule()
  ...
    dequeue_entity()
      update_curr(cfs_rq)
      update_load_avg()
      __dequeue_entity()
      se->on_rq = 0;
```



