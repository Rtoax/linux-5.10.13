Workqueue 工作队列 (中断下半部)
===========================


# 初始化

```
start_kernel
  workqueue_init_early	初始化系统默认工作队列
  ...
  arch_call_rest_init
    rest_init
      kernel_thread(kernel_init	==`PID = 1` for `init`
        kernel_init
          kernel_init_freeable
            workqueue_init	创建工作线程
```

# 创建工作队列

* alloc_workqueue: 
* alloc_ordered_workqueue: 
* create_workqueue:
* create_freezable_workqueue: 
* create_singlethread_workqueue: 

# 添加和调度一个 work

* INIT_WORK:		初始化work
* schedule_work:	挂入 系统的默认工作队列中，通常使用本地CPU提高局部性性能
  * queue_work_on:	可以指定 CPU

# 处理一个 work

* worker_thread

# 取消一个 work

* cancel_work_sync


# 和调度器 的交互

schedule()
  sched_submit_work()
    wq_worker_sleeping()
  