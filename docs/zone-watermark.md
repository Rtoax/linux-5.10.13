ZONE WaterMark
===============




```
Avaliable
Free
Page
^
|
|+                                  Back to High
| +                                 WaterMark           +
|  +                                      \         +
|   +                                      \    +
|----+--------------------------------------+------------------WMARK_HIGH
|     +                                   +
|      +                                +
|       + kswapd Wakeup               +
|        + /                        +
|---------+-----------------------+----------------------------WMARK_LOW
|           +       Direct      +
|             +     Reclaim   +
|               +    /      +
|                 + /     +
|-------------------+---+--------------------------------------WMARK_MIN (vm.min_free_kbytes)
|                    + +
|                     +                                        ALLOC_HIGH  (1/2)
|                                                              ALLOC_HARDER(5/8)
|                                                              ALLOC_OOM   (3/4)
|
+--------------------------------------------------------------> time
```

* `init_per_zone_wmark_min()`