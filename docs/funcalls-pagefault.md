缺页异常处理流程
===============


# 1. handle_mm_fault

```
handle_mm_fault
	hugetlb_fault	/* hugepage缺页 */
    __handle_mm_fault(struct vm_fault)	/* 普通 page 缺页 */
```

# 2. hugetlb_fault

```
hugetlb_fault	/* hugepage缺页 */
```

# 3. __handle_mm_fault

```
__handle_mm_fault(struct vm_fault)	/* 普通 page 缺页 */
    handle_pte_fault
```

# 4. handle_pte_fault

```
handle_pte_fault
    do_anonymous_page   匿名页
    do_fault            文件映射
        do_read_fault
        do_cow_fault
        do_shared_fault
    do_swap_page        交换空间
        lookup_swap_cache
        alloc_page_vma
    do_numa_page
        vm_normal_page
    do_wp_page          写时复制
        vm_normal_page
        wp_pfn_shared
        wp_page_copy
        wp_page_reuse
        wp_page_shared
```

## 4.1. do_anonymous_page

匿名页发生缺页中断，这个页是由malloc或mmap分配的。

## 4.2. do_fault

文件映射发生缺页中断

```
do_fault            文件映射
    do_read_fault       文件映射读
    do_cow_fault        文件映射写
    do_shared_fault     共享文件映射写
```

## 4.3. do_swap_page

## 4.4. do_numa_page

## 4.5. do_wp_page

处理写时复制 缺页异常。

```
do_wp_page          写时复制
    vm_normal_page    查找缺页异常地址addr对应页面的 page 数据结构
    wp_pfn_shared     处理 可写并且共享的特殊映射 页面
    wp_page_copy      处理写时复制(不可复用)
    wp_page_reuse     处理写时复制(可复用)
    wp_page_shared    处理 可写的并且共享的普通映射 页面
```





