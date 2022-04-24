


# 添加到 HASH 表

## __d_rehash

d_rehash
	__d_rehash
		hlist_bl_add_head_rcu

d_add
	__d_add
		__d_rehash
			hlist_bl_add_head_rcu

__d_add
	__d_rehash
		hlist_bl_add_head_rcu

## __d_instantiate_anon

d_instantiate_anon
	__d_instantiate_anon
		hlist_bl_add_head

__d_obtain_alias
	__d_instantiate_anon
		hlist_bl_add_head

# 从 LRU 删除

___d_drop

# 添加到 LRU 链表

dentry_kill
	retain_dentry
		d_lru_add

dput
	retain_dentry
		d_lru_add

dput_to_list
	retain_dentry
		d_lru_add

# kmem_cache_free

## run_ksoftirqd

run_ksoftirqd
	__softirqentry_text_start
		rcu_core
			rcu_do_batch
				kmem_cache_free

## task_work_run

[syscall]
	fput_many
		init_task_work(xxx, ____fput)
		task_work_add

task_work_run
	__fput
		__dentry_kill
			kmem_cache_free

## __irq_exit_rcu

sysvec_apic_timer_interrupt
common_interrupt
	__irq_exit_rcu
		__softirqentry_text_start
			rcu_core
				rcu_do_batch