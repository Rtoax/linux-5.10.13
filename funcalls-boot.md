<center><font size='6'>描述内核的启动过程的函数/调用关系</font></center>
<br/>
<br/>
<center><font size='5'>rtoax</font></center>
<center><font size='5'>荣涛</font></center>
<br/>
<br/>

> **linux-5.10.13**
> **x84_64**

# 1. _start

`_start`in`arch/x86/boot/header.S`。
内核设置入口点， 实际上 `_start` 开始之前的代码是 kenerl 自带的 bootloader。

```
_start:    arch/x86/boot/header.S
  start_of_setup: arch/x86/boot/header.S
    main(): arch/x86/boot/main.c
      copy_boot_params()
      console_init()
      cmdline_find_option_bool("debug");
      init_heap();
      validate_cpu();
      set_bios_mode();
      detect_memory();
      keyboard_init();
      query_ist();
      query_apm_bios();: 高级电源管理信息
      query_edd(); Enhanced Disk Drive 信息
      set_video();
      go_to_protected_mode()
        protected_mode_jump(): arch/x86/boot/pmjump.S
          jmpl    *%eax -> startup_32
          startup_32(): arch/x86/boot/compressed/head_64.S
            startup_64(): arch/x86/boot/compressed/head_64.S
              extract_kernel(): arch/x86/boot/compressed/misc.c
                choose_random_location();
                __decompress();
                parse_elf();
                handle_relocations();
                sev_es_shutdown_ghcb();
              jmp    *%rax -> arch/x86/kernel/head_64.S startup_64()
```


# 2. startup_64

`startup_64`in `arch/x86/kernel/head_64.S`


```
startup_64(): arch/x86/kernel/head_64.S
  leaq    (__end_init_task - SIZEOF_PTREGS)(%rip), %rsp
  leaq    _text(%rip), %rdi
  pushq    %rsi
  startup_64_setup_env() : arch\x86\kernel\head64.c
  verify_cpu() : arch\x86\kernel\verify_cpu.S
  __startup_64() : arch\x86\kernel\head64.c
  addq    $(early_top_pgt - __START_KERNEL_map), %rax
  jmp 1f
```


# 3. x86_64_start_kernel

```
x86_64_start_kernel()
    cr4_init_shadow()
    reset_early_page_tables()
    clear_bss()
    clear_page(init_top_pgt)
    sme_early_init() 安全内存加密(SME)的功能
    kasan_early_init()
    idt_setup_early_handler() 
    copy_bootdata(__va(real_mode_data))
    load_ucode_bsp()
    init_top_pgt[511] = early_top_pgt[511];
    x86_64_start_reservations()
      copy_bootdata(__va(real_mode_data))
      x86_early_init_platform_quirks()
      start_kernel()
```


# 4. start_kernel

```
start_kernel()
    set_task_stack_end_magic(&init_task)
    smp_setup_processor_id()
    debug_objects_early_init()
    cgroup_init_early()
    local_irq_disable() ------------------------------------------------
    early_boot_irqs_disabled = true
    boot_cpu_init()
    page_address_init()
    early_security_init()
    setup_arch(&command_line)
    setup_boot_config(command_line)
    setup_command_line(command_line)
    setup_nr_cpu_ids()
    setup_per_cpu_areas()
    smp_prepare_boot_cpu()
    boot_cpu_hotplug_init()
    build_all_zonelists(NULL)
    page_alloc_init()
    pr_notice("Kernel command line: %s\n", saved_command_line)
    jump_label_init()
    parse_early_param()
    parse_args()
    setup_log_buf(0)
    vfs_caches_init_early()
    sort_main_extable() 对异常表进行排序
    trap_init()
    mm_init()
      ...
      kmemleak_init()
      ...
    ftrace_init()
    early_trace_init()
    sched_init()
    preempt_disable() +++++++++++++++++++++++++++++++++++++++++++++++++++++++
    radix_tree_init()
    housekeeping_init()
    workqueue_init_early()
    rcu_init()
    trace_init()
    context_tracking_init()
    early_irq_init()
    init_IRQ()
    tick_init()
    rcu_init_nohz()
    init_timers()
    hrtimers_init()
    softirq_init() tasklet 和 hi(高优先级) tasklet 初始化
    timekeeping_init()
    rand_initialize()
    add_latent_entropy()
    add_device_randomness(command_line, strlen(command_line))
    boot_init_stack_canary()
    time_init()
    perf_event_init()
    profile_init()
    call_function_init()
    early_boot_irqs_disabled = false
    local_irq_enable()  ------------------------------------------------
    kmem_cache_init_late()
    console_init()
    lockdep_init()
    locking_selftest()
    mem_encrypt_init()
    setup_per_cpu_pageset()
    numa_policy_init()
    acpi_early_init()  高级电源管理
    sched_clock_init()
    calibrate_delay()
    pid_idr_init()
    anon_vma_init()
    thread_stack_cache_init()
    cred_init()
    fork_init()
    proc_caches_init()
    uts_ns_init()
    buffer_init()
    key_init()
    security_init()
    dbg_late_init()
    vfs_caches_init()
    pagecache_init()
    signals_init()
    seq_file_init()
    proc_root_init()
    nsfs_init()
    cpuset_init()
    cgroup_init()
    taskstats_init_early()
    delayacct_init()
    poking_init()
    check_bugs()
    acpi_subsystem_init()
    arch_post_acpi_subsys_init()
    sfi_init_late()
    kcsan_init()
    arch_call_rest_init()
      rest_init()
    prevent_tail_call_optimization()
      mb()
```

# 5. rest_init

```
rest_init()
    rcu_scheduler_starting()
    kernel_thread(kernel_init, NULL, CLONE_FS)  => PID=1
    numa_default_policy()
    kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES)  => PID=2
    system_state = SYSTEM_SCHEDULING
    complete(&kthreadd_done) => kernel_init()..wait_for_completion(&kthreadd_done)
    schedule_preempt_disabled()
      schedule()
    cpu_startup_entry(CPUHP_ONLINE)
      cpuhp_online_idle(CPUHP_ONLINE)
      while (1)
        do_idle()
          tick_nohz_idle_enter()
          cpuidle_idle_call()
```

# 6. kernel_init - systemd(PID = 1)

```
kernel_thread(kernel_init, NULL, CLONE_FS)
  kernel_init() => PID=1
    kernel_init_freeable()
      wait_for_completion(&kthreadd_done) => rest_init().complete(&kthreadd_done)
      workqueue_init()
      init_mm_internals()
      rcu_init_tasks_generic()
      do_pre_smp_initcalls()
      lockup_detector_init()
      smp_init()
      sched_init_smp()
      padata_init()
      page_alloc_init_late()
      page_ext_init()
      do_basic_setup()
        do_initcalls()
          do_initcall_level()
            do_one_initcall()
              xxx__initcall()
      kunit_run_all_tests()
      console_on_rootfs()
      integrity_load_keys()
    async_synchronize_full()
    kprobe_free_init_mem()
    ftrace_free_init_mem()
    free_initmem()
    mark_readonly()  /* protect `.rodata` */
    pti_finalize()  /* PTI:页表隔离 */
    system_state = SYSTEM_RUNNING
    numa_default_policy()
    rcu_end_inkernel_boot()
    do_sysctl_args()
    run_init_process(ramdisk_execute_command) ->__setup("rdinit=", rdinit_setup)
      kernel_execve()
    run_init_process(execute_command) -> init=systemd
    /* 如果上面两个执行都失败 */
    if (!try_to_run_init_process("/sbin/init") ||
        !try_to_run_init_process("/etc/init") ||
        !try_to_run_init_process("/bin/init") ||
        !try_to_run_init_process("/bin/sh"))
```

# 7. kthreadd - kthreadd(PID = 2)

```
kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES)
  kthreadd() => PID=2
    struct task_struct *tsk = current
    set_task_comm(tsk, "kthreadd")
    ignore_signals(tsk)
    cgroup_init_kthreadd()
    for (;;)
      set_current_state(TASK_INTERRUPTIBLE)
      if (list_empty(&kthread_create_list))
        schedule()
      __set_current_state(TASK_RUNNING)
      while (!list_empty(&kthread_create_list))
        create_kthread(create)
          kernel_thread()
```

