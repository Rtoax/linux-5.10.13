ftrace
==================

# 调用栈

## register_ftrace_direct

```
register_ftrace_direct(unsigned long ip, unsigned long addr)
	ftrace_find_rec_direct		-- 检测 ip 位置处的函数
	lookup_rec					-- 查找这个函数，返回 struct dyn_ftrace 结构
	entry = kmalloc				-- 分配struct ftrace_func_entry 结构
	ftrace_find_direct_func		-- 查找/分配struct ftrace_direct_func 结构
	ftrace_set_filter_ip		--
	register_ftrace_function	-- 注册
		ftrace_ops_init				-- 初始化 ops
		ftrace_startup				-- 启动
			__register_ftrace_function	--
				ftrace_update_trampoline	-- ?
				update_ftrace_function	-- ?
			ftrace_startup_enable		--


```

## register_ftrace_function

```
register_ftrace_function
	ftrace_startup
		[see above]
```

## __ftrace_ops_list_func

```
ftrace_ops_list_func
	__ftrace_ops_list_func
		op->func()
			direct_ops: call_direct_funcs()
```
