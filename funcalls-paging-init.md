x86-64 分页初始化

============================

# 函数调用关系

```
start_kernel
    setup_arch
        paging_init "x86_init.paging.pagetable_init()"
            zone_sizes_init
                free_area_init "初始化所有node和zone"
                    free_area_init_node
                        free_area_init_core
```
