PageCache
=========

# 以 xfs 为例

## 创建 pagecache

```
const struct vm_operations_struct xfs_file_vm_ops.fault = xfs_filemap_fault
xfs_filemap_fault
	__xfs_filemap_fault :: trace_xfs_filemap_fault
		filemap_fault
			find_get_page
				pagecache_get_page	:: 查找 并 获取 一个 page 引用(fgp_flags=0)
					find_get_entry
						xas_load	:: 从基数树中获取 page
			pagecache_get_page		:: 上面 find_get_page 执行这里 (fgp_flags=FGP_CREAT|FGP_FOR_MMAP)
				__page_cache_alloc
					alloc_pages
				add_to_page_cache_lru
					__add_to_page_cache_locked
						xas_store	:: 添加到基数树
					lru_cache_add
```

## 删除 pagecache

```
shrink_page_list
	__remove_mapping
		__delete_from_page_cache
```

```
delete_from_page_cache
	__delete_from_page_cache
	page_cache_free_page
```


```
__delete_from_page_cache
	page_cache_delete
		xas_init_marks
		page->mapping = NULL;
		mapping->nrpages -= nr;
```

```
page_cache_free_page
	mapping->a_ops->freepage(page)
	put_page
```