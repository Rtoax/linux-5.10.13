

# 解析 "crashkernel="

## arm64

```
arm64_memblock_init()
	reserve_crashkernel()
		parse_crashkernel(boot_command_line, ...)
```

## x86_64

```
setup_arch()
	reserve_crashkernel()
		parse_crashkernel(boot_command_line, ...)
```

> 这最终会分配 crash 地址，大小等，保存在 struct resource crashk_res 中。
> 下文围绕 crashk_res 展开。


# 初始化 vmcoreinfo_data

```
subsys_initcall(crash_save_vmcoreinfo_init);
```


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
