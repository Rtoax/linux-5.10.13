页面回收
==========================================


# 回收页面的三种手段

* 直接页面回收
* 周期性回收
* slab收割机

## 直接页面回收 

```
alloc_pages 
    alloc_pages_current
        __alloc_pages_nodemask
            get_page_from_freelist
                node_reclaim
                    __node_reclaim
                        shrink_node
                            shrink_node_memcgs
                                shrink_slab
                                vmpressure
                            vmpressure
shrink_zones  *** 没找到
```

## 周期性回收 

```
kswapd 
	balance_pgdat
	    pgdat_balanced

kswapd_shrink_node *** 没找到
```

## slab收割机 

```
cpucache_init
    slab_online_cpu
        start_cpu_timer
        	cache_reap 
        		drain_array
        			slabs_destroy
        			    slab_destroy
```

上面三种情况会调用

```
shrink_node --->
    shrink_active_list
    shrink_inactive_list
        shrink_slab
```

# page_referenced

```
page_referenced
    rmap_walk ****
        rmap_walk_ksm
        rmap_walk_anon
        rmap_walk_file
```
