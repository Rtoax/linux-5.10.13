x86-64 分页初始化

============================

# 内存初始化流程


1. 从 BIOS 到 E820(见 e820.txt 文档)

```
		e820__memory_setup_default
BIOS ------------------------------> e820
```

2. 内核 代码 注入 e820_add_kernel_range

3. 从 e820 到 memblock

```
		e820__memblock_setup
e820 ------------------------------> memblock 
```

4. 从 memblock 到 分页机制

```
				paging_init
memblock ----------------------------> node/zone
```


# 函数调用关系

```
start_kernel
  setup_arch
    paging_init "x86_init.paging.pagetable_init()"
      zone_sizes_init
        free_area_init "初始化所有node和zone"
          free_area_init_node
            calculate_node_totalpages "初始化 NODE 和 ZONE 结构"
            free_area_init_core
              zone_init_internals
                zone_pcp_init "per-CPU pages = PCP"
              memmap_init_zone
                __init_single_page
```

# 开启分页方式

* X86 是通过 设置 CR0 寄存器开启分页的。
