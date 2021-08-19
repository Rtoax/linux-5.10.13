vmalloc
=====================

# 数据结构

* struct vm_struct
* struct vmap_area
* 

# 全局变量

* vmap_area_root:	struct vmap_area.rb_node 红黑树的根
* vmap_area_list: 	struct vmap_area.list	 链表的头

# 接口API

## vmalloc

```
vmalloc
  __vmalloc_node
    __vmalloc_node_range
      __get_vm_area_node
        area = kzalloc_node
        va = alloc_vmap_area
        setup_vmalloc_vm(area, va, ...)
      __vmalloc_area_node
        pages = kmalloc_node
        for (i = 0; i < area->nr_pages; i++) {
          page = alloc_page|alloc_pages_node
          area->pages[i] = page;
        }
        map_kernel_range((unsigned long)area->addr, get_vm_area_size(area), prot, pages)
          map_kernel_range_noflush
            vmap_p4d_range
              vmap_pud_range
                vmap_pmd_range
                  vmap_pte_range
                    set_pte_at
                      set_pte
```

## 


