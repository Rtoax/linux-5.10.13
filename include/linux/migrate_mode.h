/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MIGRATE_MODE_H_INCLUDED
#define MIGRATE_MODE_H_INCLUDED
/*
 * MIGRATE_ASYNC means never block
 * MIGRATE_SYNC_LIGHT in the current implementation means to allow blocking
 *	on most operations but not ->writepage as the potential stall time
 *	is too significant
 * MIGRATE_SYNC will block when migrating pages
 * MIGRATE_SYNC_NO_COPY will block when migrating pages but will not copy pages
 *	with the CPU. Instead, page copy happens outside the migratepage()
 *	callback and is likely using a DMA engine. See migrate_vma() and HMM
 *	(mm/hmm.c) for users of this mode.
 */
/**
 *  迁移模式
 */
enum migrate_mode {

    /**
     *  异步模式 
     *
     *  在判断内存规整是否完成时，若可以从其他迁移类型中挪用空闲页块，那么也算完成任务。
     *

     *  在分离页面时，若发现大量的临时页面(分离的页面大于LRU页面数量的一半)
     *  也不会暂停扫描, 详见 `compact_should_abort()`(5.10.13中有`compact_unlock_should_abort()`)
     */
	MIGRATE_ASYNC,          

    /**
     *  同步模式，允许调用者被阻塞. kcompactd 内核线程采用的模式 
     *
     *  在分离页面时，若发现大量的临时页面(分离的页面大于LRU页面数量的一半)
     *  会睡眠等待100ms, 详见 `too_many_isolated()`
     */
	MIGRATE_SYNC_LIGHT,  
	
	/**
	 *  同步模式，在页面迁移时会被阻塞 
	 *
	 *  手工设置`/proc/sys/vm/compact_memory`后会采用这种模式,见`sysctl_compact_memory`。

     *  在分离页面时，若发现大量的临时页面(分离的页面大于LRU页面数量的一半)
     *  会睡眠等待100ms, 详见 `too_many_isolated()`
	 */
	MIGRATE_SYNC,     

    /**
     *  同步模式，迁移时不拷贝，有DMA引擎来复制 
     */
	MIGRATE_SYNC_NO_COPY,   
};

#endif		/* MIGRATE_MODE_H_INCLUDED */
