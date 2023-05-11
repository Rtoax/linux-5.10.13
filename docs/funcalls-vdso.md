vDSO
====


# clock_gettime()

## 用户态

```
__GI___clock_gettime
  INTERNAL_VSYSCALL_CALL
    __cvdso_clock_gettime: 内核中实现的函数
```


## 内核

```
__cvdso_clock_gettime
  __cvdso_clock_gettime_data(__arch_get_vdso_data(), ...)
    __cvdso_clock_gettime_common
      do_hres
        __arch_get_hw_counter # x86
          rdtsc_ordered
            rdtsc
        __arch_get_hw_counter # aarch64
          asm volatile("mrs %0, cntvct_el0" : "=r" (res) :: "memory");
```
