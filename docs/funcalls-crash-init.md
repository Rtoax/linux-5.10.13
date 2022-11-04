

# arm64 特殊部分

```
setup_arch()
	arm64_memblock_init()
		reserve_elfcorehdr()
			early_init_dt_scan_elfcorehdr()
			memblock_reserve(elfcorehdr_addr, elfcorehdr_size);
```


# x86_64 特殊部分

```

```


# 架构以外的通用部分

```
start_kernel()
	...
	fs_initcall(vmcore_init);
```
