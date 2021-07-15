// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  fs/eventpoll.c (Efficient event retrieval implementation)
 *  Copyright (C) 2001,...,2009	 Davide Libenzi
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/rbtree.h>
#include <linux/wait.h>
#include <linux/eventpoll.h>
#include <linux/mount.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/anon_inodes.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/mman.h>
#include <linux/atomic.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/compat.h>
#include <linux/rculist.h>
#include <net/busy_poll.h>

/*
 * LOCKING:
 * There are three level of locking required by epoll :
 *
 * 1) epmutex (mutex)
 * 2) ep->mtx (mutex)
 * 3) ep->lock (rwlock)
 *
 * The acquire order is the one listed above, from 1 to 3.
 * We need a rwlock (ep->lock) because we manipulate objects
 * from inside the poll callback, that might be triggered from
 * a wake_up() that in turn might be called from IRQ context.
 * So we can't sleep inside the poll callback and hence we need
 * a spinlock. During the event transfer loop (from kernel to
 * user space) we could end up sleeping due a copy_to_user(), so
 * we need a lock that will allow us to sleep. This lock is a
 * mutex (ep->mtx). It is acquired during the event transfer loop,
 * during epoll_ctl(EPOLL_CTL_DEL) and during eventpoll_release_file().
 * Then we also need a global mutex to serialize eventpoll_release_file()
 * and ep_free().
 * This mutex is acquired by ep_free() during the epoll file
 * cleanup path and it is also acquired by eventpoll_release_file()
 * if a file has been pushed inside an epoll set and it is then
 * close()d without a previous call to epoll_ctl(EPOLL_CTL_DEL).
 * It is also acquired when inserting an epoll fd onto another epoll
 * fd. We do this so that we walk the epoll tree and ensure that this
 * insertion does not create a cycle of epoll file descriptors, which
 * could lead to deadlock. We need a global mutex to prevent two
 * simultaneous inserts (A into B and B into A) from racing and
 * constructing a cycle without either insert observing that it is
 * going to.
 * It is necessary to acquire multiple "ep->mtx"es at once in the
 * case when one epoll fd is added to another. In this case, we
 * always acquire the locks in the order of nesting (i.e. after
 * epoll_ctl(e1, EPOLL_CTL_ADD, e2), e1->mtx will always be acquired
 * before e2->mtx). Since we disallow cycles of epoll file
 * descriptors, this ensures that the mutexes are well-ordered. In
 * order to communicate this nesting to lockdep, when walking a tree
 * of epoll file descriptors, we use the current recursion depth as
 * the lockdep subkey.
 * It is possible to drop the "ep->mtx" and to use the global
 * mutex "epmutex" (together with "ep->lock") to have it working,
 * but having "ep->mtx" will make the interface more scalable.
 * Events that require holding "epmutex" are very rare, while for
 * normal operations the epoll private "ep->mtx" will guarantee
 * a better scalability.
 */

/* Epoll private bits inside the event mask */
#define EP_PRIVATE_BITS (EPOLLWAKEUP | EPOLLONESHOT | EPOLLET | EPOLLEXCLUSIVE)

#define EPOLLINOUT_BITS (EPOLLIN | EPOLLOUT)

#define EPOLLEXCLUSIVE_OK_BITS (EPOLLINOUT_BITS | EPOLLERR | EPOLLHUP | \
				EPOLLWAKEUP | EPOLLET | EPOLLEXCLUSIVE)

/* Maximum number of nesting allowed inside epoll sets */
#define EP_MAX_NESTS 4

#define EP_MAX_EVENTS (INT_MAX / sizeof(struct epoll_event))

#define EP_UNACTIVE_PTR ((void *) -1L)

#define EP_ITEM_COST (sizeof(struct epitem) + sizeof(struct eppoll_entry))

/**
 *  一个打开的文件
 *  一个文件描述符
 *  这是 epoll item(epitem)中需要的
 */
struct epoll_filefd {
	struct file *file;
	int fd;
} __packed;

/*
 * Structure used to track possible nested calls, for too deep recursions
 * and loop cycles.
 */
struct nested_call_node {
	struct list_head llink;
	void *cookie;
	void *ctx;
};

/*
 * This structure is used as collector for nested calls, to check for
 * maximum recursion dept and loop cycles.
 */
struct nested_calls {   /*  */
	struct list_head tasks_call_list;
	spinlock_t lock;
};

/*
 * Each file descriptor added to the eventpoll interface will
 * have an entry of this type linked to the "rbr" RB tree.
 * Avoid increasing the size of this struct, there can be many thousands
 * of these on a server and we do not want this to take another cache line.
 *
 * 每个文件描述符对应有个结构
 */
struct epitem {
	union {
        /**
         *  红黑树节点，对应 eventpoll.rbr
         */
		/* RB tree node links this structure to the eventpoll RB tree */
		struct rb_node rbn; /* 红黑树节点 */
		/* Used to free the struct epitem */
		struct rcu_head rcu;
	};

    /**
     *  用于 链接 eventpoll 准备好的 结构
     *
     *  eventpoll.rdllist 链表头
     */
	/* List header used to link this structure to the eventpoll ready list */
	struct list_head rdllink;

	/*
	 * Works together "struct eventpoll"->ovflist in keeping the
	 * single linked chain of items.
	 */
	struct epitem *next;    /* 链表头    eventpoll"->ovflist*/

    /**
     *  这个 epoll 项 对应的文件描述符结构
     */
	/* The file descriptor information this item refers to */
	struct epoll_filefd ffd;

    /**
     *  
     */
	/* Number of active wait queue attached to poll operations */
	int nwait;

    /**
     *  轮训等待队列
     */
	/* List containing poll wait queues */
	struct list_head pwqlist;

    /**
     *  属于哪个 epoll
     */
	/* The "container" of this item */
	struct eventpoll *ep;

    /**
     *  用于连接 本 epitem 到 struct file 结构中， file 结构中有对应的链表节点
     *  链表头为 "struct file"->f_ep_links
     */
	/* List header used to link this item to the "struct file"->f_ep_links items list */
	struct list_head fllink;

    /**
     *  
     */
	/* wakeup_source used when EPOLLWAKEUP is set */
	struct wakeup_source __rcu *ws;

    /**
     *  具体对应 的 源 fd 和 event
     *  参见 epoll_ctl 
     */
	/* The structure that describe the interested events and the source fd */
	struct epoll_event event;
};

/*
 * This structure is stored inside the "private_data" member of the file
 * structure and represents the main data structure for the eventpoll
 * interface.
 */
struct eventpoll {/* 这个结构是：file->pivate_data */
	/*
	 * This mutex is used to ensure that files are not removed
	 * while epoll is using them. This is held during the event
	 * collection loop, the file cleanup path, the epoll file exit
	 * code and the ctl operations.
	 */
	struct mutex mtx;/* 当epoll在使用， files 不被删除 */

	/* Wait queue used by sys_epoll_wait() */
	wait_queue_head_t wq;/* epoll_wait 使用 */

    /**
     *  文件 wait queue
     */
	/* Wait queue used by file->poll() */
	wait_queue_head_t poll_wait;/*  */

	/**
	 *  List of ready file descriptors 
	 *
	 *  epitem.rdllink 节点
	 */
	struct list_head rdllist;/* 就绪的  链表*/

	/* Lock which protects rdllist and ovflist */
	rwlock_t lock;/* 保护就绪的 files 和  */

    /**
     *  用于管理 fd 的红黑树,对应 epitem.rbn
     */
	/* RB tree root used to store monitored fd structs */
	struct rb_root_cached rbr;/* 用于存储 fd 结构体 */

	/*
	 * This is a single linked list that chains all the "struct epitem" that
	 * happened while transferring ready events to userspace w/out
	 * holding ->lock.
	 *
	 * 单链表：链接所有的 epitem ，这些 epitem 准备好传输.
	 * 这是一个链表头，链表节点是 epitem.next
	 */
	struct epitem *ovflist;/* 链接所有的  epitem ，用于传输 可用的 event 到 用户空间 */

	/* wakeup_source used when ep_scan_ready_list is running */
	struct wakeup_source *ws;/*  */

	/* The user that created the eventpoll descriptor */
	struct user_struct *user;/* 创建 epoll 的 用户 */

	struct file *file;  /* 文件 file */

	/* used to optimize loop detection check */
	u64 gen;/* 用于优化 loop 探测 */

#ifdef CONFIG_NET_RX_BUSY_POLL
    /**
     *  NAPI ID, struct socket.sk.sk_napi_id
     */
	/* used to track busy poll napi_id */
	unsigned int napi_id;   /* 追踪忙碌 poll napi_id */
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/* tracks wakeup nests for lockdep validation */
	u8 nests;   /*  */
#endif
};

/* Wait structure used by the poll hooks */
struct eppoll_entry {   /*  */
	/* List header used to link this structure to the "struct epitem" */
	struct list_head llink;

	/* The "base" pointer is set to the container "struct epitem" */
	struct epitem *base;

	/*
	 * Wait queue item that will be linked to the target file wait
	 * queue head.
	 */
	wait_queue_entry_t wait;

	/* The wait queue head that linked the "wait" wait queue item */
	wait_queue_head_t *whead;
};

/**
 *  
 */
/* Wrapper struct used by poll queueing */
struct ep_pqueue {
	poll_table pt;
	struct epitem *epi;
};

/* Used by the ep_send_events() function as callback private data */
struct ep_send_events_data {
	int maxevents;
	struct epoll_event __user *events;
	int res;
};

/*
 * Configuration options available inside /proc/sys/fs/epoll/
 */
/* Maximum number of epoll watched descriptors, per user */
static long __read_mostly max_user_watches ;

/*
 * This mutex is used to serialize ep_free() and eventpoll_release_file().
 */
//static DEFINE_MUTEX(epmutex);/* 我使用下面这句 */
struct mutex epmutex = __MUTEX_INITIALIZER(epmutex);
static u64 loop_check_gen = 0;

/* Used to check for epoll file descriptor inclusion loops */
static struct nested_calls poll_loop_ncalls;    /*  */

/* Slab cache used to allocate "struct epitem" */
static struct kmem_cache __read_mostly *epi_cache ;

/* Slab cache used to allocate "struct eppoll_entry" */
static struct kmem_cache __read_mostly *pwq_cache ;

/*
 * List of files with newly added links, where we may need to limit the number
 * of emanating paths. Protected by the epmutex.
 *
 * 带有新添加链接的文件列表，我们可能需要在其中限制发出路径的数量。 受 epmutex 保护。
 */
static LIST_HEAD(tfile_check_list);
static struct list_head tfile_check_list; /* +++ */

#ifdef CONFIG_SYSCTL

#include <linux/sysctl.h>

static long long_zero;
static long long_max = LONG_MAX;

struct ctl_table epoll_table[] = {
	{
		.procname	= "max_user_watches",
		.data		= &max_user_watches,
		.maxlen		= sizeof(max_user_watches),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra1		= &long_zero,
		.extra2		= &long_max,
	},
	{ }
};
#endif /* CONFIG_SYSCTL */

//static const struct file_operations eventpoll_fops;

/**
 * 根据操作符确定是否为 epoll
 */
static inline int is_file_epoll(struct file *f)
{
	return f->f_op == &eventpoll_fops;
}

/* Setup the structure that is used as key for the RB tree */
static inline void ep_set_ffd(struct epoll_filefd *ffd,
			      struct file *file, int fd)
{
	ffd->file = file;
	ffd->fd = fd;
}

/* Compare RB tree keys */
static inline int ep_cmp_ffd(struct epoll_filefd *p1,
			     struct epoll_filefd *p2)
{
	return (p1->file > p2->file ? +1:
	        (p1->file < p2->file ? -1 : p1->fd - p2->fd));
}

/* Tells us if the item is currently linked */
static inline int ep_is_linked(struct epitem *epi)
{
	return !list_empty(&epi->rdllink);
}

static inline struct eppoll_entry *ep_pwq_from_wait(wait_queue_entry_t *p)
{
	return container_of(p, struct eppoll_entry, wait);
}

/* Get the "struct epitem" from a wait queue pointer */
static inline struct epitem *ep_item_from_wait(wait_queue_entry_t *p)
{
	return container_of(p, struct eppoll_entry, wait)->base;
}

/* Get the "struct epitem" from an epoll queue wrapper */
static inline struct epitem *ep_item_from_epqueue(poll_table *p)
{
	return container_of(p, struct ep_pqueue, pt)->epi;
}

/* Initialize the poll safe wake up structure */
static void ep_nested_calls_init(struct nested_calls *ncalls)
{
	INIT_LIST_HEAD(&ncalls->tasks_call_list);
	spin_lock_init(&ncalls->lock);
}

/**
 * ep_events_available - Checks if ready events might be available.
 *
 * @ep: Pointer to the eventpoll context.
 *
 * Returns: Returns a value different than zero if ready events are available,
 *          or zero otherwise.
 *//* 是否有可用的 events */
static inline int ep_events_available(struct eventpoll *ep)
{
	return !list_empty_careful(&ep->rdllist)/* ready list 不为空 */ ||
		READ_ONCE(ep->ovflist) != EP_UNACTIVE_PTR;/* 就绪的 eitem 不为空 */
}

#ifdef CONFIG_NET_RX_BUSY_POLL
static bool ep_busy_loop_end(void *p, unsigned long start_time)
{
	struct eventpoll *ep = p;

	return ep_events_available(ep) /* 有可用的 event */|| busy_loop_timeout(start_time)/* 已超时 */;
}

/*
 * Busy poll if globally on and supporting sockets found && no events,
 * busy loop will return if need_resched or ep_events_available.
 *
 * we must do our busy polling with irqs enabled
 *//*  */
static void ep_busy_loop(struct eventpoll *ep, int nonblock)
{
	unsigned int napi_id = READ_ONCE(ep->napi_id);/* 获取 NAPI ID */

	if ((napi_id >= MIN_NAPI_ID) && net_busy_loop_on())
		napi_busy_loop(napi_id, nonblock ? NULL : ep_busy_loop_end, ep);
}

static inline void ep_reset_busy_poll_napi_id(struct eventpoll *ep)
{
	if (ep->napi_id)
		ep->napi_id = 0;
}

/*
 * Set epoll busy poll NAPI ID from sk.
 */
static inline void ep_set_busy_poll_napi_id(struct epitem *epi)
{
	struct eventpoll *ep;
	unsigned int napi_id;
	struct socket *sock;
	struct sock *sk;
	int err;

	if (!net_busy_loop_on())
		return;

    /**
     *  如果是 socket 返回 socket 结构
     */
	sock = sock_from_file(epi->ffd.file, &err);
	if (!sock)
		return;

    /**
     *  
     */
	sk = sock->sk;
	if (!sk)
		return;

    /**
     *  读取 NAPI ID
     */
	napi_id = READ_ONCE(sk->sk_napi_id);
	ep = epi->ep;

	/* Non-NAPI IDs can be rejected
	 *	or
	 * Nothing to do if we already have this ID
	 */
	if (napi_id < MIN_NAPI_ID || napi_id == ep->napi_id)
		return;

	/* record NAPI ID for use in next busy poll */
	ep->napi_id = napi_id;
}

#else
/*  */
#endif /* CONFIG_NET_RX_BUSY_POLL */

/**
 * ep_call_nested - Perform a bound (possibly) nested call, by checking
 *                  that the recursion limit is not exceeded, and that
 *                  the same nested call (by the meaning of same cookie) is
 *                  no re-entered. 检测嵌套
 *
 * @ncalls: Pointer to the nested_calls structure to be used for this call.
 * @nproc: Nested call core function pointer.
 * @priv: Opaque data to be passed to the @nproc callback.
 * @cookie: Cookie to be used to identify this nested call.
 * @ctx: This instance context.
 *
 * Returns: Returns the code returned by the @nproc callback, or -1 if
 *          the maximum recursion limit has been exceeded.
 */
static int ep_call_nested(struct nested_calls *ncalls,
			  int (*nproc)(void *, void *, int), void *priv,
			  void *cookie, void *ctx)
{
	int error, call_nests = 0;
	unsigned long flags;
	struct list_head *lsthead = &ncalls->tasks_call_list;
	struct nested_call_node *tncur;
	struct nested_call_node tnode;

	spin_lock_irqsave(&ncalls->lock, flags);

	/*
	 * Try to see if the current task is already inside this wakeup call.
	 * We use a list here, since the population inside this set is always
	 * very much limited.
	 */
	list_for_each_entry(tncur, lsthead, llink) {
		if (tncur->ctx == ctx &&
		    (tncur->cookie == cookie || ++call_nests > EP_MAX_NESTS)) {
			/* 发生嵌套
			 * Ops ... loop detected or maximum nest level reached.
			 * We abort this wake by breaking the cycle itself.
			 */
			error = -1;
			goto out_unlock;
		}
	}

	/* Add the current task and cookie to the list */
	tnode.ctx = ctx;
	tnode.cookie = cookie;
	list_add(&tnode.llink, lsthead);

	spin_unlock_irqrestore(&ncalls->lock, flags);

	/* Call the nested function */
	error = (*nproc)(priv, cookie, call_nests);

	/* Remove the current task from the list */
	spin_lock_irqsave(&ncalls->lock, flags);
	list_del(&tnode.llink);
out_unlock:
	spin_unlock_irqrestore(&ncalls->lock, flags);

	return error;
}

/*
 * As described in commit 0ccf831cb lockdep: annotate epoll
 * the use of wait queues used by epoll is done in a very controlled
 * manner. Wake ups can nest inside each other, but are never done
 * with the same locking. For example:
 *
 *   dfd = socket(...);
 *   efd1 = epoll_create();
 *   efd2 = epoll_create();
 *   epoll_ctl(efd1, EPOLL_CTL_ADD, dfd, ...);
 *   epoll_ctl(efd2, EPOLL_CTL_ADD, efd1, ...);
 *
 * When a packet arrives to the device underneath "dfd", the net code will
 * issue a wake_up() on its poll wake list. Epoll (efd1) has installed a
 * callback wakeup entry on that queue, and the wake_up() performed by the
 * "dfd" net code will end up in ep_poll_callback(). At this point epoll
 * (efd1) notices that it may have some event ready, so it needs to wake up
 * the waiters on its poll wait list (efd2). So it calls ep_poll_safewake()
 * that ends up in another wake_up(), after having checked about the
 * recursion constraints. That are, no more than EP_MAX_POLLWAKE_NESTS, to
 * avoid stack blasting.
 *
 * When CONFIG_DEBUG_LOCK_ALLOC is enabled, make sure lockdep can handle
 * this special case of epoll.
 */
#ifdef CONFIG_DEBUG_LOCK_ALLOC

static void ep_poll_safewake(struct eventpoll *ep, struct epitem *epi)
{
	struct eventpoll *ep_src;
	unsigned long flags;
	u8 nests = 0;

	/*
	 * To set the subclass or nesting level for spin_lock_irqsave_nested()
	 * it might be natural to create a per-cpu nest count. However, since
	 * we can recurse on ep->poll_wait.lock, and a non-raw spinlock can
	 * schedule() in the -rt kernel, the per-cpu variable are no longer
	 * protected. Thus, we are introducing a per eventpoll nest field.
	 * If we are not being call from ep_poll_callback(), epi is NULL and
	 * we are at the first level of nesting, 0. Otherwise, we are being
	 * called from ep_poll_callback() and if a previous wakeup source is
	 * not an epoll file itself, we are at depth 1 since the wakeup source
	 * is depth 0. If the wakeup source is a previous epoll file in the
	 * wakeup chain then we use its nests value and record ours as
	 * nests + 1. The previous epoll file nests value is stable since its
	 * already holding its own poll_wait.lock.
	 */
	if (epi) {
		if ((is_file_epoll(epi->ffd.file))) {
			ep_src = epi->ffd.file->private_data;
			nests = ep_src->nests;
		} else {
			nests = 1;
		}
	}
	spin_lock_irqsave_nested(&ep->poll_wait.lock, flags, nests);
	ep->nests = nests + 1;
	wake_up_locked_poll(&ep->poll_wait, EPOLLIN);
	ep->nests = 0;
	spin_unlock_irqrestore(&ep->poll_wait.lock, flags);
}

#else

static void ep_poll_safewake(struct eventpoll *ep, struct epitem *epi)
{
	wake_up_poll(&ep->poll_wait, EPOLLIN);
}

#endif

static void ep_remove_wait_queue(struct eppoll_entry *pwq)
{
	wait_queue_head_t *whead;

	rcu_read_lock();
	/*
	 * If it is cleared by POLLFREE, it should be rcu-safe.
	 * If we read NULL we need a barrier paired with
	 * smp_store_release() in ep_poll_callback(), otherwise
	 * we rely on whead->lock.
	 */
	whead = smp_load_acquire(&pwq->whead);
	if (whead)
		remove_wait_queue(whead, &pwq->wait);
	rcu_read_unlock();
}

/*
 * This function unregisters poll callbacks from the associated file
 * descriptor.  Must be called with "mtx" held (or "epmutex" if called from
 * ep_free).
 */
static void ep_unregister_pollwait(struct eventpoll *ep, struct epitem *epi)
{
	struct list_head *lsthead = &epi->pwqlist;
	struct eppoll_entry *pwq;

    /**
     *  
     */
	while (!list_empty(lsthead)) {
		pwq = list_first_entry(lsthead, struct eppoll_entry, llink);

		list_del(&pwq->llink);
		ep_remove_wait_queue(pwq);
		kmem_cache_free(pwq_cache, pwq);
	}
}

/* call only when ep->mtx is held */
static inline struct wakeup_source *ep_wakeup_source(struct epitem *epi)
{
	return rcu_dereference_check(epi->ws, lockdep_is_held(&epi->ep->mtx));
}

/* call only when ep->mtx is held */
static inline void ep_pm_stay_awake(struct epitem *epi)
{
    /**
     *  唤醒
     */
	struct wakeup_source *ws = ep_wakeup_source(epi);

	if (ws)
		__pm_stay_awake(ws);
}

static inline bool ep_has_wakeup_source(struct epitem *epi)
{
	return rcu_access_pointer(epi->ws) ? true : false;
}

/* call when ep->mtx cannot be held (ep_poll_callback) */
static inline void ep_pm_stay_awake_rcu(struct epitem *epi)
{
	struct wakeup_source *ws;

	rcu_read_lock();
	ws = rcu_dereference(epi->ws);
	if (ws)
		__pm_stay_awake(ws);
	rcu_read_unlock();
}

/**
 * ep_scan_ready_list - Scans the ready list in a way that makes possible for
 *                      the scan code, to call f_op->poll(). Also allows for
 *                      O(NumReady) performance.
 *
 * @ep: Pointer to the epoll private data structure.
 * @sproc: Pointer to the scan callback.
 * @priv: Private opaque data passed to the @sproc callback.
 * @depth: The current depth of recursive f_op->poll calls.
 * @ep_locked: caller already holds ep->mtx
 *
 * Returns: The same integer error code returned by the @sproc callback.
 *//* 扫描可用的 event list */
static __poll_t ep_scan_ready_list(struct eventpoll *ep,
			      __poll_t (*sproc)(struct eventpoll *,
					   struct list_head *, void *),
			      void *priv, int depth, bool ep_locked)
{
	__poll_t res;
	struct epitem *epi, *nepi;
	LIST_HEAD(txlist);

	lockdep_assert_irqs_enabled();

	/*
	 * We need to lock this because we could be hit by
	 * eventpoll_release_file() and epoll_ctl().
	 */

	if (!ep_locked)/* 未被锁定 */
		mutex_lock_nested(&ep->mtx, depth);/* 嵌套锁 */

	/*
	 * Steal the ready list, and re-init the original one to the
	 * empty list. Also, set ep->ovflist to NULL so that events
	 * happening while looping w/out locks, are not lost. We cannot
	 * have the poll callback to queue directly on ep->rdllist,
	 * because we want the "sproc" callback to be able to do it
	 * in a lockless way.
	 */
	write_lock_irq(&ep->lock);/* 写保护 */
	list_splice_init(&ep->rdllist, &txlist);/* 拼接就绪的 链表 到 txlist */
	WRITE_ONCE(ep->ovflist, NULL);/* 给 epitem 置空 */
	write_unlock_irq(&ep->lock);

	/*
	 * Now call the callback function.
	 */
	res = (*sproc)(ep, &txlist, priv);/* 执行 scan 的回调 */

	write_lock_irq(&ep->lock);/* 解锁 */
	/*
	 * During the time we spent inside the "sproc" callback, some
	 * other events might have been queued by the poll callback.
	 * We re-insert them inside the main ready-list here.
	 *//* 执行回调过程中，可能还会有其他的 event 到来 */
	for (nepi = READ_ONCE(ep->ovflist); (epi = nepi) != NULL;
	     nepi = epi->next, epi->next = EP_UNACTIVE_PTR) {
		/*
		 * We need to check if the item is already in the list.
		 * During the "sproc" callback execution time, items are
		 * queued into ->ovflist but the "txlist" might already
		 * contain them, and the list_splice() below takes care of them.
		 */
		if (!ep_is_linked(epi)) {
			/*
			 * ->ovflist is LIFO, so we have to reverse it in order
			 * to keep in FIFO.
			 */
			list_add(&epi->rdllink, &ep->rdllist);
			ep_pm_stay_awake(epi);
		}
	}
	/*
	 * We need to set back ep->ovflist to EP_UNACTIVE_PTR, so that after
	 * releasing the lock, events will be queued in the normal way inside
	 * ep->rdllist.
	 */
	WRITE_ONCE(ep->ovflist, EP_UNACTIVE_PTR);

	/*
	 * Quickly re-inject items left on "txlist".
	 */
	list_splice(&txlist, &ep->rdllist);/* 拼接 */
	__pm_relax(ep->ws);/* TODO */
	write_unlock_irq(&ep->lock);

	if (!ep_locked)
		mutex_unlock(&ep->mtx);

	return res;
}

static void epi_rcu_free(struct rcu_head *head)
{
	struct epitem *epi = container_of(head, struct epitem, rcu);
	kmem_cache_free(epi_cache, epi);
}

/*
 * Removes a "struct epitem" from the eventpoll RB tree and deallocates
 * all the associated resources. Must be called with "mtx" held.
 *
 * 删除
 */
static int ep_remove(struct eventpoll *ep, struct epitem *epi)
{
	struct file *file = epi->ffd.file;

	lockdep_assert_irqs_enabled();

	/*
	 * Removes poll wait queue hooks.
	 */
	ep_unregister_pollwait(ep, epi);

	/* Remove the current item from the list of epoll hooks */
	spin_lock(&file->f_lock);
	list_del_rcu(&epi->fllink);
	spin_unlock(&file->f_lock);

	rb_erase_cached(&epi->rbn, &ep->rbr);

	write_lock_irq(&ep->lock);
	if (ep_is_linked(epi))
		list_del_init(&epi->rdllink);
	write_unlock_irq(&ep->lock);

	wakeup_source_unregister(ep_wakeup_source(epi));
	/*
	 * At this point it is safe to free the eventpoll item. Use the union
	 * field epi->rcu, since we are trying to minimize the size of
	 * 'struct epitem'. The 'rbn' field is no longer in use. Protected by
	 * ep->mtx. The rcu read side, reverse_path_check_proc(), does not make
	 * use of the rbn field.
	 */
	call_rcu(&epi->rcu, epi_rcu_free);

	atomic_long_dec(&ep->user->epoll_watches);

	return 0;
}

static void ep_free(struct eventpoll *ep)
{
	struct rb_node *rbp;
	struct epitem *epi;

	/* We need to release all tasks waiting for these file */
	if (waitqueue_active(&ep->poll_wait))
		ep_poll_safewake(ep, NULL);

	/*
	 * We need to lock this because we could be hit by
	 * eventpoll_release_file() while we're freeing the "struct eventpoll".
	 * We do not need to hold "ep->mtx" here because the epoll file
	 * is on the way to be removed and no one has references to it
	 * anymore. The only hit might come from eventpoll_release_file() but
	 * holding "epmutex" is sufficient here.
	 */
	mutex_lock(&epmutex);

	/*
	 * Walks through the whole tree by unregistering poll callbacks.
	 */
	for (rbp = rb_first_cached(&ep->rbr); rbp; rbp = rb_next(rbp)) {
		epi = rb_entry(rbp, struct epitem, rbn);

		ep_unregister_pollwait(ep, epi);
		cond_resched();
	}

	/*
	 * Walks through the whole tree by freeing each "struct epitem". At this
	 * point we are sure no poll callbacks will be lingering around, and also by
	 * holding "epmutex" we can be sure that no file cleanup code will hit
	 * us during this operation. So we can avoid the lock on "ep->lock".
	 * We do not need to lock ep->mtx, either, we only do it to prevent
	 * a lockdep warning.
	 */
	mutex_lock(&ep->mtx);
	while ((rbp = rb_first_cached(&ep->rbr)) != NULL) {
		epi = rb_entry(rbp, struct epitem, rbn);
		ep_remove(ep, epi);
		cond_resched();
	}
	mutex_unlock(&ep->mtx);

	mutex_unlock(&epmutex);
	mutex_destroy(&ep->mtx);
	free_uid(ep->user);
	wakeup_source_unregister(ep->ws);
	kfree(ep);
}

static int ep_eventpoll_release(struct inode *inode, struct file *file)/* 释放 */
{
	struct eventpoll *ep = file->private_data;

	if (ep)
		ep_free(ep);

	return 0;
}

static __poll_t ep_read_events_proc(struct eventpoll *ep, struct list_head *head,
			       void *priv);
static void ep_ptable_queue_proc(struct file *file, wait_queue_head_t *whead,
				 poll_table *pt);

/*
 * Differs from ep_eventpoll_poll() in that internal callers already have
 * the ep->mtx so we need to start from depth=1, such that mutex_lock_nested()
 * is correctly annotated.
 */
static __poll_t ep_item_poll(const struct epitem *epi, poll_table *pt, int depth)
{
	struct eventpoll *ep;
	bool locked;

    /**
     *  
     */
	pt->_key = epi->event.events;

    /**
     *  如果不是 epoll
     */
	if (!is_file_epoll(epi->ffd.file))/* 如果不是 epoll file */
        /**
         *  执行 file 的 poll
         *  将 socket epitem 添加 至 epoll, 将执行此代码并返回
         */
		return vfs_poll(epi->ffd.file, pt) & epi->event.events;/* 使用虚拟文件系统的 file_operations->poll */

    /**
     *  如果是 epoll
     */
	ep = epi->ffd.file->private_data;/* 如果是 epoll file，获取 struct eventpoll  */

    /**
     *  阻塞于此
     *  在 epoll_ctl 时，将 epitem 添加至 红黑树中 如果此时 epitem 也为 epoll, 则执行 ep_ptable_queue_proc()
     */
	poll_wait(epi->ffd.file, &ep->poll_wait, pt);
    
	locked = pt && (pt->_qproc == ep_ptable_queue_proc);

	return ep_scan_ready_list(epi->ffd.file->private_data,/* 这里解决嵌套 epoll 问题 */
				  ep_read_events_proc/* 嵌套的回调函数 */, &depth, depth,
				  locked) & epi->event.events;
}

static __poll_t ep_read_events_proc(struct eventpoll *ep, struct list_head *head,
			       void *priv)
{
	struct epitem *epi, *tmp;
	poll_table pt;
	int depth = *(int *)priv;

	init_poll_funcptr(&pt, NULL);
	depth++;

	list_for_each_entry_safe(epi, tmp, head, rdllink) {
		if (ep_item_poll(epi, &pt, depth)) {
			return EPOLLIN | EPOLLRDNORM;
		} else {
			/*
			 * Item has been dropped into the ready list by the poll
			 * callback, but it's not actually ready, as far as
			 * caller requested events goes. We can remove it here.
			 */
			__pm_relax(ep_wakeup_source(epi));
			list_del_init(&epi->rdllink);
		}
	}

	return 0;
}

/**
 *  epoll 的 file_operations 
 */
static __poll_t ep_eventpoll_poll(struct file *file, poll_table *wait)
{
	struct eventpoll *ep = file->private_data;
	int depth = 0;

	/* Insert inside our poll wait queue */
	poll_wait(file, &ep->poll_wait, wait);  /*  */

	/*
	 * Proceed to find out if wanted events are really available inside
	 * the ready list.
	 */
	return ep_scan_ready_list(ep, ep_read_events_proc,
				  &depth, depth, false);
}

#ifdef CONFIG_PROC_FS
static void ep_show_fdinfo(struct seq_file *m, struct file *f)/* 向 proc 文件系统写数据 */
{
	struct eventpoll *ep = f->private_data;
	struct rb_node *rbp;

	mutex_lock(&ep->mtx);
	for (rbp = rb_first_cached(&ep->rbr); rbp; rbp = rb_next(rbp)) {
		struct epitem *epi = rb_entry(rbp, struct epitem, rbn);
		struct inode *inode = file_inode(epi->ffd.file);

		seq_printf(m, "tfd: %8d events: %8x data: %16llx "
			   " pos:%lli ino:%lx sdev:%x\n",
			   epi->ffd.fd, epi->event.events,
			   (long long)epi->event.data,
			   (long long)epi->ffd.file->f_pos,
			   inode->i_ino, inode->i_sb->s_dev);
		if (seq_has_overflowed(m))
			break;
	}
	mutex_unlock(&ep->mtx);
}
#endif

/**
 *  epoll 的 匿名文件操作符
 */
/* File callbacks that implement the eventpoll file behaviour */
static const struct file_operations eventpoll_fops/* epoll 文件操作 */ = {

#ifdef CONFIG_PROC_FS
	eventpoll_fops.show_fdinfo	= ep_show_fdinfo,   /* /proc/sys/fs/epoll */
#endif
	eventpoll_fops.release	    = ep_eventpoll_release, /* 释放 */
	/**
     *  
     */
	eventpoll_fops.poll		    = ep_eventpoll_poll,    /* vfs_poll */
	eventpoll_fops.llseek		= noop_llseek,  /* file->f_pos */
};

/*
 * This is called from eventpoll_release() to unlink files from the eventpoll
 * interface. We need to have this facility to cleanup correctly files that are
 * closed without being removed from the eventpoll interface.
 */
void eventpoll_release_file(struct file *file)
{
	struct eventpoll *ep;
	struct epitem *epi, *next;

	/*
	 * We don't want to get "file->f_lock" because it is not
	 * necessary. It is not necessary because we're in the "struct file"
	 * cleanup path, and this means that no one is using this file anymore.
	 * So, for example, epoll_ctl() cannot hit here since if we reach this
	 * point, the file counter already went to zero and fget() would fail.
	 * The only hit might come from ep_free() but by holding the mutex
	 * will correctly serialize the operation. We do need to acquire
	 * "ep->mtx" after "epmutex" because ep_remove() requires it when called
	 * from anywhere but ep_free().
	 *
	 * Besides, ep_remove() acquires the lock, so we can't hold it here.
	 */
	mutex_lock(&epmutex);
	list_for_each_entry_safe(epi, next, &file->f_ep_links, fllink) {
		ep = epi->ep;
		mutex_lock_nested(&ep->mtx, 0);
		ep_remove(ep, epi);
		mutex_unlock(&ep->mtx);
	}
	mutex_unlock(&epmutex);
}

/**
 *  分配 eventpoll 结构
 */
static int ep_alloc(struct eventpoll **pep)/* 申请分配一个eventpoll */
{
	int error;
	struct user_struct *user;
	struct eventpoll *ep;

    /**
     *  当前用户
     */
	user = get_current_user();/* 获取当前用户 */
	error = -ENOMEM;

    /**
     * 分配内存 
     */
	ep = kzalloc(sizeof(*ep), GFP_KERNEL);/* 分配内存 kmalloc */
	if (unlikely(!ep))
		goto free_uid;

    /**
     *  初始化 锁，链表，等待队列等
     */
	mutex_init(&ep->mtx);/* 互斥锁 */
	rwlock_init(&ep->lock);/* 读写锁 */
	init_waitqueue_head(&ep->wq);/* 等待队列，用户态可能要用pthread_cond来实现 */
	init_waitqueue_head(&ep->poll_wait);
	INIT_LIST_HEAD(&ep->rdllist);/* ready 的 文件描述符 */
	ep->rbr = RB_ROOT_CACHED; /* 一个根和一个最左节点 */
	ep->ovflist = EP_UNACTIVE_PTR;
	ep->user = user;/* 创建epoll的用户 */

	*pep = ep;/* 返回 */

	return 0;

free_uid:
	free_uid(user);
	return error;
}

/*
 * Search the file inside the eventpoll tree. The RB tree operations
 * are protected by the "mtx" mutex, and ep_find() must be called with
 * "mtx" held.  搜索 epitem
 */
static struct epitem *ep_find(struct eventpoll *ep, struct file *file, int fd)
{
	int kcmp;
	struct rb_node *rbp;
	struct epitem *epi, *epir = NULL;
	struct epoll_filefd ffd;

    /**
     *  设置 ffd 结构
     */
	ep_set_ffd(&ffd, file, fd);

    /**
     *  遍历红黑树
     */
	for (rbp = ep->rbr.rb_root.rb_node/* fd 的红黑树 根 */; rbp; ) {
        /**
         *  
         */
		epi = rb_entry(rbp, struct epitem, rbn);

        /**
         *  比较 file 和 fd ，进行搜索
         */
		kcmp = ep_cmp_ffd(&ffd, &epi->ffd);/* 先比较 struct file 在比较 int fd */
		if (kcmp > 0)
			rbp = rbp->rb_right;
		else if (kcmp < 0)
			rbp = rbp->rb_left;
		else {
			epir = epi;
			break;
		}
	}
    /**
     *  返回，当然这里返回值可能是 NULL
     */
	return epir;
}

#ifdef CONFIG_CHECKPOINT_RESTORE
/**
 *  
 */
static struct epitem *ep_find_tfd(struct eventpoll *ep, int tfd, unsigned long toff)
{
	struct rb_node *rbp;
	struct epitem *epi;

	for (rbp = rb_first_cached(&ep->rbr); rbp; rbp = rb_next(rbp)) {
		epi = rb_entry(rbp, struct epitem, rbn);
		if (epi->ffd.fd == tfd) {
			if (toff == 0)
				return epi;
			else
				toff--;
		}
		cond_resched();
	}

	return NULL;
}

struct file *get_epoll_tfile_raw_ptr(struct file *file, int tfd,
				     unsigned long toff)
{
	struct file *file_raw;
	struct eventpoll *ep;
	struct epitem *epi;

	if (!is_file_epoll(file))
		return ERR_PTR(-EINVAL);

	ep = file->private_data;

	mutex_lock(&ep->mtx);
	epi = ep_find_tfd(ep, tfd, toff);
	if (epi)
		file_raw = epi->ffd.file;
	else
		file_raw = ERR_PTR(-ENOENT);
	mutex_unlock(&ep->mtx);

	return file_raw;
}
#endif /* CONFIG_CHECKPOINT_RESTORE */

/**
 * Adds a new entry to the tail of the list in a lockless way, i.e.
 * multiple CPUs are allowed to call this function concurrently.
 *
 * Beware: it is necessary to prevent any other modifications of the
 *         existing list until all changes are completed, in other words
 *         concurrent list_add_tail_lockless() calls should be protected
 *         with a read lock, where write lock acts as a barrier which
 *         makes sure all list_add_tail_lockless() calls are fully
 *         completed.
 *
 *        Also an element can be locklessly added to the list only in one
 *        direction i.e. either to the tail either to the head, otherwise
 *        concurrent access will corrupt the list.
 *
 * Returns %false if element has been already added to the list, %true
 * otherwise.
 */
static inline bool list_add_tail_lockless(struct list_head *new,
					  struct list_head *head)
{
	struct list_head *prev;

	/*
	 * This is simple 'new->next = head' operation, but cmpxchg()
	 * is used in order to detect that same element has been just
	 * added to the list from another CPU: the winner observes
	 * new->next == new.
	 */
	if (cmpxchg(&new->next, new, head) != new)
		return false;

	/*
	 * Initially ->next of a new element must be updated with the head
	 * (we are inserting to the tail) and only then pointers are atomically
	 * exchanged.  XCHG guarantees memory ordering, thus ->next should be
	 * updated before pointers are actually swapped and pointers are
	 * swapped before prev->next is updated.
	 */

	prev = xchg(&head->prev, new);

	/*
	 * It is safe to modify prev->next and new->prev, because a new element
	 * is added only to the tail and new->next is updated before XCHG.
	 */

	prev->next = new;
	new->prev = prev;

	return true;
}

/**
 * Chains a new epi entry to the tail of the ep->ovflist in a lockless way,
 * i.e. multiple CPUs are allowed to call this function concurrently.
 *
 * Returns %false if epi element has been already chained, %true otherwise.
 */
static inline bool chain_epi_lockless(struct epitem *epi)
{
	struct eventpoll *ep = epi->ep;

	/* Fast preliminary check */
	if (epi->next != EP_UNACTIVE_PTR)
		return false;

	/* Check that the same epi has not been just chained from another CPU */
	if (cmpxchg(&epi->next, EP_UNACTIVE_PTR, NULL) != EP_UNACTIVE_PTR)
		return false;

	/* Atomically exchange tail */
	epi->next = xchg(&ep->ovflist, epi);

	return true;
}

/*
 * This is the callback that is passed to the wait queue wakeup
 * mechanism. It is called by the stored file descriptors when they
 * have events to report.
 *
 * This callback takes a read lock in order not to content with concurrent
 * events from another file descriptors, thus all modifications to ->rdllist
 * or ->ovflist are lockless.  Read lock is paired with the write lock from
 * ep_scan_ready_list(), which stops all list modifications and guarantees
 * that lists state is seen correctly.
 *
 * Another thing worth to mention is that ep_poll_callback() can be called
 * concurrently for the same @epi from different CPUs if poll table was inited
 * with several wait queues entries.  Plural wakeup from different CPUs of a
 * single wait queue is serialized by wq.lock, but the case when multiple wait
 * queues are used should be detected accordingly.  This is detected using
 * cmpxchg() operation.
 */
static int ep_poll_callback(wait_queue_entry_t *wait, unsigned mode, int sync, void *key)
{
	int pwake = 0;
	struct epitem *epi = ep_item_from_wait(wait);
	struct eventpoll *ep = epi->ep;
	__poll_t pollflags = key_to_poll(key);
	unsigned long flags;
	int ewake = 0;

	read_lock_irqsave(&ep->lock, flags);

	ep_set_busy_poll_napi_id(epi);

	/*
	 * If the event mask does not contain any poll(2) event, we consider the
	 * descriptor to be disabled. This condition is likely the effect of the
	 * EPOLLONESHOT bit that disables the descriptor when an event is received,
	 * until the next EPOLL_CTL_MOD will be issued.
	 */
	if (!(epi->event.events & ~EP_PRIVATE_BITS))
		goto out_unlock;

	/*
	 * Check the events coming with the callback. At this stage, not
	 * every device reports the events in the "key" parameter of the
	 * callback. We need to be able to handle both cases here, hence the
	 * test for "key" != NULL before the event match test.
	 */
	if (pollflags && !(pollflags & epi->event.events))
		goto out_unlock;

	/*
	 * If we are transferring events to userspace, we can hold no locks
	 * (because we're accessing user memory, and because of linux f_op->poll()
	 * semantics). All the events that happen during that period of time are
	 * chained in ep->ovflist and requeued later on.
	 */
	if (READ_ONCE(ep->ovflist) != EP_UNACTIVE_PTR) {
		if (chain_epi_lockless(epi))
			ep_pm_stay_awake_rcu(epi);
	} else if (!ep_is_linked(epi)) {
		/* In the usual case, add event to ready list. */
		if (list_add_tail_lockless(&epi->rdllink, &ep->rdllist))
			ep_pm_stay_awake_rcu(epi);
	}

	/*
	 * Wake up ( if active ) both the eventpoll wait list and the ->poll()
	 * wait list.
	 */
	if (waitqueue_active(&ep->wq)) {
		if ((epi->event.events & EPOLLEXCLUSIVE) &&
					!(pollflags & POLLFREE)) {
			switch (pollflags & EPOLLINOUT_BITS) {
			case EPOLLIN:
				if (epi->event.events & EPOLLIN)
					ewake = 1;
				break;
			case EPOLLOUT:
				if (epi->event.events & EPOLLOUT)
					ewake = 1;
				break;
			case 0:
				ewake = 1;
				break;
			}
		}
		wake_up(&ep->wq);
	}
	if (waitqueue_active(&ep->poll_wait))
		pwake++;

out_unlock:
	read_unlock_irqrestore(&ep->lock, flags);

	/* We have to call this outside the lock */
	if (pwake)
		ep_poll_safewake(ep, epi);

	if (!(epi->event.events & EPOLLEXCLUSIVE))
		ewake = 1;

	if (pollflags & POLLFREE) {
		/*
		 * If we race with ep_remove_wait_queue() it can miss
		 * ->whead = NULL and do another remove_wait_queue() after
		 * us, so we can't use __remove_wait_queue().
		 */
		list_del_init(&wait->entry);
		/*
		 * ->whead != NULL protects us from the race with ep_free()
		 * or ep_remove(), ep_remove_wait_queue() takes whead->lock
		 * held by the caller. Once we nullify it, nothing protects
		 * ep/epi or even wait.
		 */
		smp_store_release(&ep_pwq_from_wait(wait)->whead, NULL);
	}

	return ewake;
}

/*
 * This is the callback that is used to add our wait queue to the
 * target file wakeup lists.
 *
 * 用于添加我们的 等待队列到 目标 file 唤醒链表 里的回调函数
 */
static void ep_ptable_queue_proc(struct file *file, wait_queue_head_t *whead,
				 poll_table *pt)
{
	struct epitem *epi = ep_item_from_epqueue(pt);
	struct eppoll_entry *pwq;

	if (epi->nwait >= 0 && (pwq = kmem_cache_alloc(pwq_cache, GFP_KERNEL))) {
		init_waitqueue_func_entry(&pwq->wait, ep_poll_callback);
		pwq->whead = whead;
		pwq->base = epi;
		if (epi->event.events & EPOLLEXCLUSIVE)
			add_wait_queue_exclusive(whead, &pwq->wait);
		else
			add_wait_queue(whead, &pwq->wait);
		list_add_tail(&pwq->llink, &epi->pwqlist);
		epi->nwait++;
	} else {
		/* We have to signal that an error occurred */
		epi->nwait = -1;
	}
}

/**
 *  将 file 对应的 epitem 添加至 红黑树中
 */
static void ep_rbtree_insert(struct eventpoll *ep, struct epitem *epi)
{
	int kcmp;

    /**
     *  红黑树 根 p
     */
	struct rb_node **p = &ep->rbr.rb_root.rb_node, *parent = NULL;
	struct epitem *epic;
	bool leftmost = true;

    /**
     *  遍历添加
     */
	while (*p) {
		parent = *p;
		epic = rb_entry(parent, struct epitem, rbn);

        /**
         *  比较两个 epitem
         */
		kcmp = ep_cmp_ffd(&epi->ffd, &epic->ffd);
		if (kcmp > 0) {
			p = &parent->rb_right;
			leftmost = false;
		} else
			p = &parent->rb_left;
        /**
         *  因为不可能存在，所以没有单独判断 相等 (==0)的情况
         */
	}
    /**
     *  添加到红黑树中， leftmost 用于 cache
     */
	rb_link_node(&epi->rbn, parent, p);
	rb_insert_color_cached(&epi->rbn, &ep->rbr, leftmost);
}



#define PATH_ARR_SIZE 5
/*
 * These are the number paths of length 1 to 5, that we are allowing to emanate
 * from a single file of interest. For example, we allow 1000 paths of length
 * 1, to emanate from each file of interest. This essentially represents the
 * potential wakeup paths, which need to be limited in order to avoid massive
 * uncontrolled wakeup storms. The common use case should be a single ep which
 * is connected to n file sources. In this case each file source has 1 path
 * of length 1. Thus, the numbers below should be more than sufficient. These
 * path limits are enforced during an EPOLL_CTL_ADD operation, since a modify
 * and delete can't add additional paths. Protected by the epmutex.
 */
static const int path_limits[PATH_ARR_SIZE] = { 1000, 500, 100, 50, 10 };
static int path_count[PATH_ARR_SIZE];

static int path_count_inc(int nests)
{
	/* Allow an arbitrary number of depth 1 paths */
	if (nests == 0)
		return 0;

	if (++path_count[nests] > path_limits[nests])
		return -1;
	return 0;
}

static void path_count_init(void)
{
	int i;

	for (i = 0; i < PATH_ARR_SIZE; i++)
		path_count[i] = 0;
}

static int reverse_path_check_proc(void *priv, void *cookie, int call_nests)
{
	int error = 0;
	struct file *file = priv;
	struct file *child_file;
	struct epitem *epi;

	/* CTL_DEL can remove links here, but that can't increase our count */
	rcu_read_lock();
	list_for_each_entry_rcu(epi, &file->f_ep_links, fllink) {
		child_file = epi->ep->file;
		if (is_file_epoll(child_file)) {
			if (list_empty(&child_file->f_ep_links)) {
				if (path_count_inc(call_nests)) {
					error = -1;
					break;
				}
			} else {
				error = ep_call_nested(&poll_loop_ncalls,
							reverse_path_check_proc,
							child_file, child_file,
							current);
			}
			if (error != 0)
				break;
		} else {
			printk(KERN_ERR "reverse_path_check_proc: "
				"file is not an ep!\n");
		}
	}
	rcu_read_unlock();
	return error;
}

/**
 * reverse_path_check - The tfile_check_list is list of file *, which have
 *                      links that are proposed to be newly added. We need to
 *                      make sure that those added links don't add too many
 *                      paths such that we will spend all our time waking up
 *                      eventpoll objects.
 *
 * Returns: Returns zero if the proposed links don't create too many paths,
 *	    -1 otherwise.
 */
static int reverse_path_check(void)
{
	int error = 0;
	struct file *current_file;

	/* let's call this for all tfiles */
	list_for_each_entry(current_file, &tfile_check_list, f_tfile_llink) {
		path_count_init();
		error = ep_call_nested(&poll_loop_ncalls,
					reverse_path_check_proc, current_file,
					current_file, current);
		if (error)
			break;
	}
	return error;
}

/**
 *  
 */
static int ep_create_wakeup_source(struct epitem *epi)
{
	struct name_snapshot n;
	struct wakeup_source *ws;

	if (!epi->ep->ws) {
		epi->ep->ws = wakeup_source_register(NULL, "eventpoll");
		if (!epi->ep->ws)
			return -ENOMEM;
	}

	take_dentry_name_snapshot(&n, epi->ffd.file->f_path.dentry);
	ws = wakeup_source_register(NULL, n.name.name);
	release_dentry_name_snapshot(&n);

	if (!ws)
		return -ENOMEM;
	rcu_assign_pointer(epi->ws, ws);

	return 0;
}

/* rare code path, only used when EPOLL_CTL_MOD removes a wakeup source */
static noinline void ep_destroy_wakeup_source(struct epitem *epi)
{
	struct wakeup_source *ws = ep_wakeup_source(epi);

	RCU_INIT_POINTER(epi->ws, NULL);

	/*
	 * wait for ep_pm_stay_awake_rcu to finish, synchronize_rcu is
	 * used internally by wakeup_source_remove, too (called by
	 * wakeup_source_unregister), so we cannot use call_rcu
	 */
	synchronize_rcu();
	wakeup_source_unregister(ws);
}

/*
 * Must be called with "mtx" held.
 * 插入红黑树
 */
static int ep_insert(struct eventpoll *ep, const struct epoll_event *event,
		     struct file *tfile, int fd, int full_check)
{
	int error, pwake = 0;
	__poll_t revents;
	long user_watches;
	struct epitem *epi;
	struct ep_pqueue epq;

	lockdep_assert_irqs_enabled();

    /**
     *  当前 watch 的文件描述符个数，并且监测个数不能超限
     */
	user_watches = atomic_long_read(&ep->user->epoll_watches);
	if (unlikely(user_watches >= max_user_watches))
		return -ENOSPC; // 超限 返回 no space 错误

    /**
     *  分配 epitem 结构
     */
	if (!(epi = kmem_cache_alloc(epi_cache, GFP_KERNEL)))
		return -ENOMEM;

    /**
     *  初始化链表
     */
	/* Item initialization follow here ... */
	INIT_LIST_HEAD(&epi->rdllink);
	INIT_LIST_HEAD(&epi->fllink);
	INIT_LIST_HEAD(&epi->pwqlist);
	epi->ep = ep;
	ep_set_ffd(&epi->ffd, tfile, fd);

    /**
     *  将用户传入 的 event 赋值给 epitem, event 已经是 copy_from_user 的
     *  nwait 置零
     *  单链表 next 为空
     */
	epi->event = *event;
	epi->nwait = 0;
	epi->next = EP_UNACTIVE_PTR;

    /**
     *  epoll_ctl 没有填入此项
     *
     *  唤醒源
     */
	if (epi->event.events & EPOLLWAKEUP) {
		error = ep_create_wakeup_source(epi);
		if (error)
			goto error_create_wakeup_source;
	} else {
	    /**
         *  
         */
		RCU_INIT_POINTER(epi->ws, NULL);
	}

	/* Add the current item to the list of active epoll hook for this file */
	spin_lock(&tfile->f_lock);
    /**
     *  将 epitem 添加 至 file 中(这个file 可能为 socket fd file)
     */
	list_add_tail_rcu(&epi->fllink, &tfile->f_ep_links);
	spin_unlock(&tfile->f_lock);

	/*
	 * Add the current item to the RB tree. All RB tree operations are
	 * protected by "mtx", and ep_insert() is called with "mtx" held.
	 *
	 * 添加到 eventpoll 红黑树中
	 */
	ep_rbtree_insert(ep, epi);

	/* now check if we've created too many backpaths */
	error = -EINVAL;

    /**
     *  TODO 2021年7月15日22:36:16
     */
	if (full_check && reverse_path_check())
		goto error_remove_epi;

	/* Initialize the poll table using the queue callback */
	epq.epi = epi;
	init_poll_funcptr(&epq.pt, ep_ptable_queue_proc);

	/*
	 * Attach the item to the poll hooks and get current event bits.
	 * We can safely use the file* here because its usage count has
	 * been increased by the caller of this function. Note that after
	 * this operation completes, the poll callback can start hitting
	 * the new item.
	 */
	revents = ep_item_poll(epi, &epq.pt, 1);

	/*
	 * We have to check if something went wrong during the poll wait queue
	 * install process. Namely an allocation for a wait queue failed due
	 * high memory pressure.
	 */
	error = -ENOMEM;
	if (epi->nwait < 0)
		goto error_unregister;

	/* We have to drop the new item inside our item list to keep track of it */
	write_lock_irq(&ep->lock);

    /**
     *  获取 NAPI ID
     */
	/* record NAPI ID of new item if present */
	ep_set_busy_poll_napi_id(epi);

    /**
     *  如果已经有 时间发生，并且还没有添加的 ready list
     */
	/* If the file is already "ready" we drop it inside the ready list */
	if (revents && !ep_is_linked(epi)) {

        /**
         *  添加到 eventpoll.rdllist 链表中
         */
		list_add_tail(&epi->rdllink, &ep->rdllist);

        /**
         *  TODO 2021年7月15日22:56:41
         */
		ep_pm_stay_awake(epi);

        /**
         *  
         */
		/* Notify waiting tasks that events are available */
		if (waitqueue_active(&ep->wq))
            /**
             *  如果 epoll 的 waitqueue 不为空，唤醒 thread
             */
			wake_up(&ep->wq);

        /**
         *  
         */
		if (waitqueue_active(&ep->poll_wait))
			pwake++;
	}

	write_unlock_irq(&ep->lock);

    /**
     *  添加了 新的 epitem
     */
	atomic_long_inc(&ep->user->epoll_watches);

	/* We have to call this outside the lock */
	if (pwake)
		ep_poll_safewake(ep, NULL);

	return 0;

error_unregister:
	ep_unregister_pollwait(ep, epi);
error_remove_epi:
	spin_lock(&tfile->f_lock);
	list_del_rcu(&epi->fllink);
	spin_unlock(&tfile->f_lock);

	rb_erase_cached(&epi->rbn, &ep->rbr);

	/*
	 * We need to do this because an event could have been arrived on some
	 * allocated wait queue. Note that we don't care about the ep->ovflist
	 * list, since that is used/cleaned only inside a section bound by "mtx".
	 * And ep_insert() is called with "mtx" held.
	 */
	write_lock_irq(&ep->lock);
	if (ep_is_linked(epi))
		list_del_init(&epi->rdllink);
	write_unlock_irq(&ep->lock);

	wakeup_source_unregister(ep_wakeup_source(epi));

error_create_wakeup_source:
	kmem_cache_free(epi_cache, epi);

	return error;
}

/*
 * Modify the interest event mask by dropping an event if the new mask
 * has a match in the current file status. Must be called with "mtx" held.
 */
static int ep_modify(struct eventpoll *ep, struct epitem *epi,
		     const struct epoll_event *event)
{
	int pwake = 0;
	poll_table pt;

	lockdep_assert_irqs_enabled();

	init_poll_funcptr(&pt, NULL);

	/*
	 * Set the new event interest mask before calling f_op->poll();
	 * otherwise we might miss an event that happens between the
	 * f_op->poll() call and the new event set registering.
	 */
	epi->event.events = event->events; /* need barrier below */
	epi->event.data = event->data; /* protected by mtx */
	if (epi->event.events & EPOLLWAKEUP) {
		if (!ep_has_wakeup_source(epi))
			ep_create_wakeup_source(epi);
	} else if (ep_has_wakeup_source(epi)) {
		ep_destroy_wakeup_source(epi);
	}

	/*
	 * The following barrier has two effects:
	 *
	 * 1) Flush epi changes above to other CPUs.  This ensures
	 *    we do not miss events from ep_poll_callback if an
	 *    event occurs immediately after we call f_op->poll().
	 *    We need this because we did not take ep->lock while
	 *    changing epi above (but ep_poll_callback does take
	 *    ep->lock).
	 *
	 * 2) We also need to ensure we do not miss _past_ events
	 *    when calling f_op->poll().  This barrier also
	 *    pairs with the barrier in wq_has_sleeper (see
	 *    comments for wq_has_sleeper).
	 *
	 * This barrier will now guarantee ep_poll_callback or f_op->poll
	 * (or both) will notice the readiness of an item.
	 */
	smp_mb();

	/*
	 * Get current event bits. We can safely use the file* here because
	 * its usage count has been increased by the caller of this function.
	 * If the item is "hot" and it is not registered inside the ready
	 * list, push it inside.
	 */
	if (ep_item_poll(epi, &pt, 1)) {
		write_lock_irq(&ep->lock);
		if (!ep_is_linked(epi)) {
			list_add_tail(&epi->rdllink, &ep->rdllist);
			ep_pm_stay_awake(epi);

			/* Notify waiting tasks that events are available */
			if (waitqueue_active(&ep->wq))
				wake_up(&ep->wq);
			if (waitqueue_active(&ep->poll_wait))
				pwake++;
		}
		write_unlock_irq(&ep->lock);
	}

	/* We have to call this outside the lock */
	if (pwake)
		ep_poll_safewake(ep, NULL);

	return 0;
}

static __poll_t ep_send_events_proc(struct eventpoll *ep, struct list_head *head,
			       void *priv)/* ep_scan_ready_list 的回调函数 *//*  epoll_wait 关键函数*/
{
	struct ep_send_events_data *esed = priv;
	__poll_t revents;
	struct epitem *epi, *tmp;
	struct epoll_event __user *uevent = esed->events;
	struct wakeup_source *ws;
	poll_table pt;

	init_poll_funcptr(&pt, NULL);/* 初始化 poll 函数 */
	esed->res = 0;

	/*
	 * We can loop without lock because we are passed a task private list.
	 * Items cannot vanish during the loop because ep_scan_ready_list() is
	 * holding "mtx" during this call.
	 */
	lockdep_assert_held(&ep->mtx);/* 锁检测 */

	list_for_each_entry_safe(epi, tmp, head, rdllink) {/* 遍历所有 epitem */
		if (esed->res >= esed->maxevents)
			break;

		/*
		 * Activate ep->ws before deactivating epi->ws to prevent
		 * triggering auto-suspend here (in case we reactive epi->ws
		 * below).
		 *
		 * This could be rearranged to delay the deactivation of epi->ws
		 * instead, but then epi->ws would temporarily be out of sync
		 * with ep_is_linked().
		 */
		ws = ep_wakeup_source(epi);
		if (ws) {
			if (ws->active)
				__pm_stay_awake(ep->ws);
			__pm_relax(ws);
		}

		list_del_init(&epi->rdllink);/* 删除这个 epitem */

		/*
		 * If the event mask intersect the caller-requested one,
		 * deliver the event to userspace. Again, ep_scan_ready_list()
		 * is holding ep->mtx, so no operations coming from userspace
		 * can change the item.
		 */
		revents = ep_item_poll(epi, &pt, 1);/* 轮询这个 epitem, VFS(常规fd) 和 epoll(嵌套epoll) */
		if (!revents)
			continue;

		if (__put_user(revents, &uevent->events) ||
		    __put_user(epi->event.data, &uevent->data)) {
			list_add(&epi->rdllink, head);
			ep_pm_stay_awake(epi);
			if (!esed->res)
				esed->res = -EFAULT;
			return 0;
		}
		esed->res++;
		uevent++;
		if (epi->event.events & EPOLLONESHOT)
			epi->event.events &= EP_PRIVATE_BITS;
		else if (!(epi->event.events & EPOLLET)) {
			/*
			 * If this file has been added with Level
			 * Trigger mode, we need to insert back inside
			 * the ready list, so that the next call to
			 * epoll_wait() will check again the events
			 * availability. At this point, no one can insert
			 * into ep->rdllist besides us. The epoll_ctl()
			 * callers are locked out by
			 * ep_scan_ready_list() holding "mtx" and the
			 * poll callback will queue them in ep->ovflist.
			 */
			list_add_tail(&epi->rdllink, &ep->rdllist);
			ep_pm_stay_awake(epi);
		}
	}

	return 0;
}

static int ep_send_events(struct eventpoll *ep,
			  struct epoll_event __user *events, int maxevents)/* epoll_wait 关键函数 */
{/* 向用户态发送 events 数据 */
	struct ep_send_events_data esed;

	esed.maxevents = maxevents;
	esed.events = events;
    /* 扫描 就绪 event 链表 */
	ep_scan_ready_list(ep, ep_send_events_proc, &esed, 0, false);/* TODO */
	return esed.res;
}

static inline struct timespec64 ep_set_mstimeout(long ms)
{
	struct timespec64 now, ts = {
		.tv_sec = ms / MSEC_PER_SEC,
		.tv_nsec = NSEC_PER_MSEC * (ms % MSEC_PER_SEC),
	};

	ktime_get_ts64(&now);/* 获取当前时间 */
	return timespec64_add_safe(now, ts);/* 计算到期时间 */
}

/**
 * ep_poll - Retrieves ready events, and delivers them to the caller supplied
 *           event buffer.
 *
 * @ep: Pointer to the eventpoll context.
 * @events: Pointer to the userspace buffer where the ready events should be
 *          stored.
 * @maxevents: Size (in terms of number of events) of the caller event buffer.
 * @timeout: Maximum timeout for the ready events fetch operation, in
 *           milliseconds. If the @timeout is zero, the function will not block,
 *           while if the @timeout is less than zero, the function will block
 *           until at least one event has been retrieved (or an error
 *           occurred).
 *
 * Returns: Returns the number of ready events which have been fetched, or an
 *          error code, in case of error.
 *//* epoll_wait 最终会调用并阻塞于此 */
static int ep_poll(struct eventpoll *ep, struct epoll_event __user *events,
		   int maxevents, long timeout/* 微妙超时 */)
{
	int res = 0, eavail/* 是否有可用的 event */, timed_out = 0;
	u64 slack = 0;/* 懈怠的 */
	wait_queue_entry_t wait;/* 等待队列 */
	ktime_t expires/* 到期时间 纳秒级 */, *to = NULL;

	lockdep_assert_irqs_enabled();/* 死锁检测 TODO */

	if (timeout > 0) {/* 如果需要设置超时时间 */
		struct timespec64 end_time = ep_set_mstimeout(timeout);/* 微妙到 timespec64 */

		slack = select_estimate_accuracy(&end_time/* 到期时间 */);
		to = &expires;/* 纳秒级到期时间 */
		*to = timespec64_to_ktime(end_time);
	} else if (timeout == 0) {/* 如果等于0，非阻塞 */
		/*
		 * Avoid the unnecessary trip to the wait queue loop, if the
		 * caller specified a non blocking operation. We still need
		 * lock because we could race and not see an epi being added
		 * to the ready list while in irq callback. Thus incorrectly
		 * returning 0 back to userspace.
		 */
		timed_out = 1;/* 非阻塞 */

		write_lock_irq(&ep->lock);
		eavail = ep_events_available(ep);/* 非阻塞直接获取可用的 event */
		write_unlock_irq(&ep->lock);

		goto send_events;/* 直接发送 */
	}

fetch_events:/*底部 ep_send_events 失败会 goto fetch_events  */
    /* 阻塞情况会执行下列代码 */
	if (!ep_events_available(ep))/* 如果没有可用 event */
		ep_busy_loop(ep, timed_out/* 非阻塞时 timed_out == 1 */);/* 如果定义了 CONFIG_NET_RX_BUSY_POLL */

	eavail = ep_events_available(ep);/* 如果 NAPI 接收到数据 */
	if (eavail)
		goto send_events;

	/*
	 * Busy poll timed out.  Drop NAPI ID for now, we can add
	 * it back in when we have moved a socket with a valid NAPI
	 * ID onto the ready list.
	 */
	ep_reset_busy_poll_napi_id(ep);/* eventpoll->napi_id = 0 */

	do {
		/*
		 * Internally init_wait() uses autoremove_wake_function(),
		 * thus wait entry is removed from the wait queue on each
		 * wakeup. Why it is important? In case of several waiters
		 * each new wakeup will hit the next waiter, giving it the
		 * chance to harvest new event. Otherwise wakeup can be
		 * lost. This is also good performance-wise, because on
		 * normal wakeup path no need to call __remove_wait_queue()
		 * explicitly, thus ep->lock is not taken, which halts the
		 * event delivery.
		 */
		init_wait(&wait);

		write_lock_irq(&ep->lock);/* 写保护 */
		/*
		 * Barrierless variant, waitqueue_active() is called under
		 * the same lock on wakeup ep_poll_callback() side, so it
		 * is safe to avoid an explicit barrier.
		 */
		__set_current_state(TASK_INTERRUPTIBLE);/* 可中断 */

		/*
		 * Do the final check under the lock. ep_scan_ready_list()
		 * plays with two lists (->rdllist and ->ovflist) and there
		 * is always a race when both lists are empty for short
		 * period of time although events are pending, so lock is
		 * important.
		 */
		eavail = ep_events_available(ep);/*  */
		if (!eavail) {/* 阻塞情况下无可用的 event */
			if (signal_pending(current))/* 有信号被挂起 */
				res = -EINTR;
			else/* 无信号挂起 */
				__add_wait_queue_exclusive/* 独占的 */(&ep->wq, &wait);/* 添加到 wait_queue_head , ep->wq */
		}
		write_unlock_irq(&ep->lock/* 读写锁 */);/* 写保护解锁 */

		if (!eavail && !res)/* 如果 没有可用 event 并且信号被 挂起，睡眠直到超时 */
			timed_out = !schedule_hrtimeout_range(to/* 到期时间 */, slack/* 如果设置了超时时间，该值不为 0 */,
							      HRTIMER_MODE_ABS/* 绝对值 */);

		/*
		 * We were woken up, thus go and try to harvest some events.
		 * If timed out and still on the wait queue, recheck eavail
		 * carefully under lock, below.
		 */
		eavail = 1;/* 被唤醒 */
	} while (0);

	__set_current_state(TASK_RUNNING);

	if (!list_empty_careful(&wait.entry)) {
		write_lock_irq(&ep->lock);
		/*
		 * If the thread timed out and is not on the wait queue, it
		 * means that the thread was woken up after its timeout expired
		 * before it could reacquire the lock. Thus, when wait.entry is
		 * empty, it needs to harvest events.
		 */
		if (timed_out)
			eavail = list_empty(&wait.entry);
		__remove_wait_queue(&ep->wq, &wait);
		write_unlock_irq(&ep->lock);
	}

send_events:/*若非阻塞，获取可用event跳转至此  */
	if (fatal_signal_pending(current)) {
		/*
		 * Always short-circuit for fatal signals to allow
		 * threads to make a timely exit without the chance of
		 * finding more events available and fetching
		 * repeatedly.
		 */
		res = -EINTR;
	}
	/*
	 * Try to transfer events to user space. In case we get 0 events and
	 * there's still timeout left over, we go trying again in search of
	 * more luck. 支持 嵌套的 epoll
	 */
	if (!res && eavail &&
	    !(res = ep_send_events(ep, events, maxevents)) && !timed_out)/* 在这里发送数据给用户空间 */
		goto fetch_events;/* 有可用的 并且 向用户发送失败 并且 没超时 */

	return res;
}

/**
 * ep_loop_check_proc - Callback function to be passed to the @ep_call_nested()
 *                      API, to verify that adding an epoll file inside another
 *                      epoll structure, does not violate the constraints, in
 *                      terms of closed loops, or too deep chains (which can
 *                      result in excessive stack usage).
 *
 * @priv: Pointer to the epoll file to be currently checked.
 * @cookie: Original cookie for this call. This is the top-of-the-chain epoll
 *          data structure pointer.
 * @call_nests: Current dept of the @ep_call_nested() call stack.
 *
 * Returns: Returns zero if adding the epoll @file inside current epoll
 *          structure @ep does not violate the constraints, or -1 otherwise.
 *
 * 用于 嵌套 的 epoll TODO 2021年7月15日22:07:00
 */
static int ep_loop_check_proc(void *priv, void *cookie, int call_nests)
{
	int error = 0;
	struct file *file = priv;
	struct eventpoll *ep = file->private_data;
	struct eventpoll *ep_tovisit;
	struct rb_node *rbp;
	struct epitem *epi;

	mutex_lock_nested(&ep->mtx, call_nests + 1);
	ep->gen = loop_check_gen;
	for (rbp = rb_first_cached(&ep->rbr); rbp; rbp = rb_next(rbp)) {
		epi = rb_entry(rbp, struct epitem, rbn);
		if (unlikely(is_file_epoll(epi->ffd.file))) {
			ep_tovisit = epi->ffd.file->private_data;
			if (ep_tovisit->gen == loop_check_gen)
				continue;
			error = ep_call_nested(&poll_loop_ncalls,
					ep_loop_check_proc, epi->ffd.file,
					ep_tovisit, current);
			if (error != 0)
				break;
		} else {
			/*
			 * If we've reached a file that is not associated with
			 * an ep, then we need to check if the newly added
			 * links are going to add too many wakeup paths. We do
			 * this by adding it to the tfile_check_list, if it's
			 * not already there, and calling reverse_path_check()
			 * during ep_insert().
			 */
			if (list_empty(&epi->ffd.file->f_tfile_llink)) {
				if (get_file_rcu(epi->ffd.file))
					list_add(&epi->ffd.file->f_tfile_llink,
						 &tfile_check_list);
			}
		}
	}
	mutex_unlock(&ep->mtx);

	return error;
}

/**
 * ep_loop_check - Performs a check to verify that adding an epoll file (@file)
 *                 another epoll file (represented by @ep) does not create
 *                 closed loops or too deep chains.
 *
 * @ep: Pointer to the epoll private data structure.
 * @file: Pointer to the epoll file to be checked.
 *
 * Returns: Returns zero if adding the epoll @file inside current epoll
 *          structure @ep does not violate the constraints, or -1 otherwise.
 */
static int ep_loop_check(struct eventpoll *ep, struct file *file)
{
    /**
     *  epoll 中 添加 epoll，嵌套的 epoll
     */
	return ep_call_nested(&poll_loop_ncalls,
			      ep_loop_check_proc, file, ep, current);
}

static void clear_tfile_check_list(void)
{
	struct file *file;

	/* first clear the tfile_check_list */
	while (!list_empty(&tfile_check_list)) {
		file = list_first_entry(&tfile_check_list, struct file,
					f_tfile_llink);
		list_del_init(&file->f_tfile_llink);
		fput(file);
	}
	INIT_LIST_HEAD(&tfile_check_list);
}

/*
 * Open an eventpoll file descriptor.
 *
 * 打开一个 epoll
 */
static int do_epoll_create(int flags)
{
	int error, fd;
	struct eventpoll *ep = NULL;
	struct file *file;

    /**
     *  默认 flags
     */
	/* Check the EPOLL_* constant for consistency.  */
	BUILD_BUG_ON(EPOLL_CLOEXEC != O_CLOEXEC); /*  */

    /**
     *  flags 规定为 EPOLL_CLOEXEC
     */
	if (flags & ~EPOLL_CLOEXEC)/* flags 需要等于 EPOLL_CLOEXEC */
		return -EINVAL;
	/*
	 * Create the internal data structure ("struct eventpoll").
	 *
	 *  分配 eventpoll 结构
	 */
	error = ep_alloc(&ep);/* 为当前的用户分配一个 epoll */
	if (error < 0)
		return error;
	/*
	 * Creates all the items needed to setup an eventpoll file. That is,
	 * a file structure and a free file descriptor.
	 */
	fd = get_unused_fd_flags(O_RDWR/* 读写权限 */ | (flags & O_CLOEXEC));/* 为 epoll 获取一个未使用的 fd */
	if (fd < 0) {
		error = fd;
		goto out_free_ep;
	}/* 匿名 inode 中获取 file */

    /**
     *  分配一个匿名的 inode，并打开
     */
	file = anon_inode_getfile("[eventpoll]", &eventpoll_fops, ep,
				            O_RDWR | (flags & O_CLOEXEC));/* 读写 */
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto out_free_fd;
	}

    /**
     *  赋值给 epoll
     */
	ep->file = file;/* eventpoll->file = file */

    /**
     *  将 fd 和 file 关联
     */
	fd_install(fd, file);/* epollfd 装入当前进程的 files中 */

    /**
     * 创建 epoll 成功 
     */
	return fd;

out_free_fd:
	put_unused_fd(fd);
out_free_ep:
	ep_free(ep);
	return error;
}

int epoll_create1(int flags);
SYSCALL_DEFINE1(epoll_create1, int, flags)/* 创建 epoll fd */
{
    //EPOLL_CLOEXEC
	return do_epoll_create(flags);
}

/**
 *  创建 epoll fd
 */
int epoll_create(int size);
SYSCALL_DEFINE1(epoll_create, int, size)/* 创建 epoll fd， size 废弃 */
{
	if (size <= 0)
		return -EINVAL;

    /**
     *  创建 epollevent 结构，并关联 fd/file
     */
	return do_epoll_create(0);
}

/**
 *  
 */
static inline int epoll_mutex_lock(struct mutex *mutex, int depth,
				   bool nonblock)
{
	if (!nonblock) {
		mutex_lock_nested(mutex, depth);
		return 0;
	}
	if (mutex_trylock(mutex))
		return 0;
	return -EAGAIN;
}

/**
 *  epoll_ctl 直接调用
 */
struct fd
int do_epoll_ctl(int epfd, int op, int fd, struct epoll_event *epds, bool nonblock)
{/* 执行系统调用 epoll_ctl */
	int error;
	int full_check = 0;
	struct fd f, tf;/* struct fd {struct file file; unsigned int flags;}; */
	struct eventpoll *ep;
	struct epitem *epi;
	struct eventpoll *tep = NULL;

	error = -EBADF;
	f = fdget(epfd);/* 用 fd 从当前进程的 files 中 获取 struct fd 结构 */
	if (!f.file)/* 如果这个 struct fd 为空 */
		goto error_return;

    /**
     *  目标 fd， 比如一个 socket() 返回的 fd
     */
	/* Get the "struct file *" for the target file */
	tf = fdget(fd);/* 要添加的目标 fd，比如说 socketfd,eventfd 等 */
	if (!tf.file)
		goto error_fput;

	/* The target file descriptor must support poll */
	error = -EPERM;

    /**
     *  文件 有 对应的 file.f_op.poll() 回调函数，没有的话直接退出
     *
     * 对于 socket, poll -> sock_poll()
     */
	if (!file_can_poll(tf.file))/* 检查 file_operations 是否为空 */
		goto error_tgt_fput;

    /**
     *  
     */
	/* Check if EPOLLWAKEUP is allowed */
	if (ep_op_has_event(op))/* 不是 EPOLL_CTL_DEL 命令 */
		ep_take_care_of_epollwakeup(epds);/* TODO */

	/*
	 * We have to check that the file structure underneath the file descriptor
	 * the user passed to us _is_ an eventpoll file. And also we do not permit
	 * adding an epoll file descriptor inside itself.
	 */
	error = -EINVAL;

    /**
     *  epoll file 和 ADD/MOD 的 file 相同
     *  或者 epoll 传入的 fd 不是 epoll fd
     */
	if (f.file == tf.file || !is_file_epoll(f.file)/* 如果 不是 epoll 的 fd */)
		goto error_tgt_fput;

	/*
	 * epoll adds to the wakeup queue at EPOLL_CTL_ADD time only,
	 * so EPOLLEXCLUSIVE is not allowed for a EPOLL_CTL_MOD operation.
	 * Also, we do not currently supported nested exclusive wakeups.
	 */
	if (ep_op_has_event(op)/* 不是 DEL */ && (epds->events & EPOLLEXCLUSIVE)) {
		if (op == EPOLL_CTL_MOD)
			goto error_tgt_fput;
		if (op == EPOLL_CTL_ADD && (is_file_epoll(tf.file) ||
				(epds->events & ~EPOLLEXCLUSIVE_OK_BITS)))
			goto error_tgt_fput;
	}

	/*
	 * At this point it is safe to assume that the "private_data" contains
	 * our own data structure.
	 *//* f 是 epfd 的 struct fd */
	ep = f.file->private_data;/* 获取 eventpoll 结构 */

	/*
	 * When we insert an epoll file descriptor, inside another epoll file
	 * descriptor, there is the change of creating closed loops, which are
	 * better be handled here, than in more critical paths. While we are
	 * checking for loops we also determine the list of files reachable
	 * and hang them on the tfile_check_list, so we can check that we
	 * haven't created too many possible wakeup paths.
	 *
	 * We do not need to take the global 'epumutex' on EPOLL_CTL_ADD when
	 * the epoll file descriptor is attaching directly to a wakeup source,
	 * unless the epoll file descriptor is nested. The purpose of taking the
	 * 'epmutex' on add is to prevent complex toplogies such as loops and
	 * deep wakeup paths from forming in parallel through multiple
	 * EPOLL_CTL_ADD operations.
	 */
	error = epoll_mutex_lock(&ep->mtx, 0, nonblock);/*  */
	if (error)
		goto error_tgt_fput;

    /**
     *  添加 fd 到 epoll
     */
	if (op == EPOLL_CTL_ADD) {
        /**
         *  
         */
		if (!list_empty(&f.file->f_ep_links)/* 如果 就绪的 节点 不为空 */ ||
				        ep->gen == loop_check_gen /* TODO */||
						is_file_epoll(tf.file)/* 要添加的 fd 是 epoll */) {

            /**
             *  
             */
			mutex_unlock(&ep->mtx);/* 解锁 */
			error = epoll_mutex_lock(&epmutex, 0, nonblock);/* 用于 ep_free 等 */
			if (error)
				goto error_tgt_fput;
			loop_check_gen++;
            
			full_check = 1;

            /**
             *  嵌套的 epoll 
             *  TODO 2021年7月15日22:07:20
             */
			if (is_file_epoll(tf.file)) {/* 如果 要 EPOLL_CTL_ADD 也是 epoll 返回 ELOOP 错误*/
				error = -ELOOP;
				if (ep_loop_check(ep, tf.file) != 0)/* LOOP 检测 */
					goto error_tgt_fput;
                
			} else { /* 如果要添加的 fd 不是 epoll FD */

                /**
                 *  将其添加至链表
                 */
				get_file(tf.file); //引用计数
				list_add(&tf.file->f_tfile_llink, /* 将其添加到 tfile_check_list   ，受 epmutex 保护*/
							&tfile_check_list);
			}
			error = epoll_mutex_lock(&ep->mtx, 0, nonblock); /* 准备添加 */
			if (error)
				goto error_tgt_fput;

            /**
             *  如果要添加的 fd 也是 epoll (嵌套的 epoll)
             */
			if (is_file_epoll(tf.file)) {/* 如果要添加的 fd 也是 epollfd */
                /**
                 *  获取 将要添加的 eventpoll 结构，并将其 锁定
                 */
				tep = tf.file->private_data;/* 获取这个 eventpoll fd */
				error = epoll_mutex_lock(&tep->mtx, 1, nonblock);
				if (error) {
					mutex_unlock(&ep->mtx);
					goto error_tgt_fput;
				}
			}
		}
	}

	/*
	 * Try to lookup the file inside our RB tree, Since we grabbed "mtx"
	 * above, we can be sure to be able to use the item looked up by
	 * ep_find() till we release the mutex.
	 *
	 * 此时 
	 *
	 *  ep = epfd
	 *  tf = 要 添加 删除 修改 的 file 结构，这个 fd 也可能是 epoll
	 *  fd = 要 添加 删除 修改 的 fd
	 *
	 * 查找 要添加的 fd 对应的 epitem 结构
	 */
	epi = ep_find(ep/* epfd */, tf.file/* fd 对应的 struct fd */, fd/* 要添加的 int fd */);

	error = -EINVAL;

    /**
     *  ADD DEL MOD
     */
	switch (op) {
    /**
     *  添加
     */
	case EPOLL_CTL_ADD:/* 添加 */
        /**
         *  不存在，插入红黑树中
         */
		if (!epi) {/* 不存在红黑树里 */
			epds->events |= EPOLLERR | EPOLLHUP;

            /**
             *  分配，添加，poll，添加，唤醒
             */
			error = ep_insert(ep, epds, tf.file, fd, full_check);
		} else  /* 已存在 */
            /**
             *  已经存在，直接退出
             */
			error = -EEXIST;
		break;

    /**
     *  删除
     */
	case EPOLL_CTL_DEL:/* 删除 */
		if (epi)/* 不为空，即 红黑树 中 存在，将其 删除 */
			error = ep_remove(ep, epi);
		else
			error = -ENOENT;
		break;

    /**
     *  修改
     */
	case EPOLL_CTL_MOD:/* 修改 */
		if (epi) {/* 存在 在 红黑树 中才可以修改 */
			if (!(epi->event.events & EPOLLEXCLUSIVE)) {/*  */
				epds->events |= EPOLLERR | EPOLLHUP;
				error = ep_modify(ep, epi, epds);
			}
		} else
			error = -ENOENT;
		break;
	}
    
	if (tep != NULL)/* 如果是嵌套的 epoll */
		mutex_unlock(&tep->mtx);
	mutex_unlock(&ep->mtx);

error_tgt_fput:
	if (full_check) {
		clear_tfile_check_list();
		loop_check_gen++;
		mutex_unlock(&epmutex);
	}

	fdput(tf);
error_fput:
	fdput(f);
error_return:

	return error;
}

/*
 * The following function implements the controller interface for
 * the eventpoll file that enables the insertion/removal/change of
 * file descriptors inside the interest set. 系统调用
 */
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
SYSCALL_DEFINE4(epoll_ctl, int, epfd, int, op, int, fd,
		            struct epoll_event __user *, event)
{
	struct epoll_event epds;

    /**
     *  如果是删除操作， event数据是没有用的
     */
	if (ep_op_has_event(op)/* 如果 不是 EPOLL_CTL_DEL 操作，才拷贝 */ &&
	    copy_from_user(&epds, event, sizeof(struct epoll_event)))
		return -EFAULT;

    /**
     *  进行控制操作 ADD DEL MOD
     */
	return do_epoll_ctl(epfd, op, fd, &epds, false);/* 调用 */
}

/*
 * Implement the event wait interface for the eventpoll file. It is the kernel
 * part of the user space epoll_wait(2).
 *//* epoll_wait syscall */
static int do_epoll_wait(int epfd, struct epoll_event __user *events,
			 int maxevents, int timeout)
{
	int error;
	struct fd f;
	struct eventpoll *ep;

	/* The maximum number of event must be greater than zero */
	if (maxevents <= 0 || maxevents > EP_MAX_EVENTS)
		return -EINVAL;

	/* Verify that the area passed by the user is writeable */
	if (!access_ok(events, maxevents * sizeof(struct epoll_event)))
		return -EFAULT;

	/* Get the "struct file *" for the eventpoll file */
	f = fdget(epfd);/* int fd -> struct fd */
	if (!f.file)
		return -EBADF;

	/*
	 * We have to check that the file structure underneath the fd
	 * the user passed to us _is_ an eventpoll file.
	 */
	error = -EINVAL;
	if (!is_file_epoll(f.file))/* 不是 epoll fd */
		goto error_fput;

	/*
	 * At this point it is safe to assume that the "private_data" contains
	 * our own data structure.
	 */
	ep = f.file->private_data;/* 获取 eventpoll 结构，它是 file->private_data 的私有变量 */

	/* Time to fish for events ... */
	error = ep_poll(ep, events, maxevents, timeout);

error_fput:
	fdput(f);
	return error;
}

/**
 *  
 */
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
SYSCALL_DEFINE4(epoll_wait, int, epfd, struct epoll_event __user *, events,
		int, maxevents, int, timeout)
{/* 系统调用 */
	return do_epoll_wait(epfd, events, maxevents, timeout);
}

/*
 * Implement the event wait interface for the eventpoll file. It is the kernel
 * part of the user space epoll_pwait(2).
 */
int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask);
SYSCALL_DEFINE6(epoll_pwait, int, epfd, struct epoll_event __user *, events,
		int, maxevents, int, timeout, const sigset_t __user *, sigmask,
		size_t, sigsetsize)
{
	int error;

	/*
	 * If the caller wants a certain signal mask to be set during the wait,
	 * we apply it here.
	 */
	error = set_user_sigmask(sigmask, sigsetsize);
	if (error)
		return error;

	error = do_epoll_wait(epfd, events, maxevents, timeout);
	restore_saved_sigmask_unless(error == -EINTR);

	return error;
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE6(epoll_pwait, int, epfd,
			struct epoll_event __user *, events,
			int, maxevents, int, timeout,
			const compat_sigset_t __user *, sigmask,
			compat_size_t, sigsetsize)
{
	long err;

	/*
	 * If the caller wants a certain signal mask to be set during the wait,
	 * we apply it here.
	 */
	err = set_compat_user_sigmask(sigmask, sigsetsize);
	if (err)
		return err;

	err = do_epoll_wait(epfd, events, maxevents, timeout);
	restore_saved_sigmask_unless(err == -EINTR);

	return err;
}
#endif

static int __init eventpoll_init(void)  /*  */
{
	struct sysinfo si;

	si_meminfo(&si);
	/*
	 * Allows top 4% of lomem to be allocated for epoll watches (per user).
	 */
	max_user_watches = (((si.totalram - si.totalhigh) / 25) << PAGE_SHIFT) /
		EP_ITEM_COST;
	BUG_ON(max_user_watches < 0);

	/*
	 * Initialize the structure used to perform epoll file descriptor
	 * inclusion loops checks.
	 */
	ep_nested_calls_init(&poll_loop_ncalls);

	/*
	 * We can have many thousands of epitems, so prevent this from
	 * using an extra cache line on 64-bit (and smaller) CPUs
	 */
	BUILD_BUG_ON(sizeof(void *) <= 8 && sizeof(struct epitem) > 128);

	/* Allocates slab cache used to allocate "struct epitem" items */
	epi_cache = kmem_cache_create("eventpoll_epi", sizeof(struct epitem),
			0, SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT, NULL);

	/* Allocates slab cache used to allocate "struct eppoll_entry" */
	pwq_cache = kmem_cache_create("eventpoll_pwq",
		sizeof(struct eppoll_entry), 0, SLAB_PANIC|SLAB_ACCOUNT, NULL);

	return 0;
}
fs_initcall(eventpoll_init);    /* epoll kmem_cache 创建 */