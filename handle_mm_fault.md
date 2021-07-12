
# handle_mm_fault


```
handle_mm_fault
    __handle_mm_fault(struct vm_fault)
        handle_pte_fault
            do_anonymous_page
            do_fault
                do_read_fault
                do_cow_fault
                do_shared_fault
            do_swap_page
                lookup_swap_cache
                alloc_page_vma
            do_numa_page
                vm_normal_page
            do_wp_page
                vm_normal_page
                wp_page_copy
                wp_page_reuse
                wp_page_shared
```
