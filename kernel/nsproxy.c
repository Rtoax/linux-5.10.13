// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2006 IBM Corporation
 *
 *  Author: Serge Hallyn <serue@us.ibm.com>
 *
 *  Jun 2006 - namespaces support
 *             OpenVZ, SWsoft Inc.
 *             Pavel Emelianov <xemul@openvz.org>
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/nsproxy.h>
#include <linux/init_task.h>
#include <linux/mnt_namespace.h>
#include <linux/utsname.h>
#include <linux/pid_namespace.h>
#include <net/net_namespace.h>
#include <linux/ipc_namespace.h>
#include <linux/time_namespace.h>
#include <linux/fs_struct.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/file.h>
#include <linux/syscalls.h>
#include <linux/cgroup.h>
#include <linux/perf_event.h>

static struct kmem_cache *nsproxy_cachep;


/**
 *  总要有第一个，所以静态分配
 */
struct nsproxy init_nsproxy /* init_task 的命名空间代理， namespace-资源隔离 */= {
	init_nsproxy.count			= ATOMIC_INIT(1),
	init_nsproxy.uts_ns			= &init_uts_ns,
#if defined(CONFIG_POSIX_MQUEUE) || defined(CONFIG_SYSVIPC)
	init_nsproxy.ipc_ns			= &init_ipc_ns,/* IPC 相关 */
#endif
	init_nsproxy.mnt_ns			= NULL,
	init_nsproxy.pid_ns_for_children	= &init_pid_ns,
#ifdef CONFIG_NET
    /**
     *  网络的这个比较神奇
     */
	init_nsproxy.net_ns			= &init_net,
#endif
#ifdef CONFIG_CGROUPS
	init_nsproxy.cgroup_ns		= &init_cgroup_ns,
#endif
#ifdef CONFIG_TIME_NS
	init_nsproxy.time_ns		= &init_time_ns,
	init_nsproxy.time_ns_for_children	= &init_time_ns,
#endif
};

static inline struct nsproxy *create_nsproxy(void)
{
	struct nsproxy *nsproxy;

	nsproxy = kmem_cache_alloc(nsproxy_cachep, GFP_KERNEL);
	if (nsproxy)
		atomic_set(&nsproxy->count, 1);
	return nsproxy;
}

/**
 * Create new nsproxy and all of its the associated namespaces.
 * Return the newly created nsproxy.  Do not attach this to the task,
 * leave it to the caller to do proper locking and attach it to task.
 *
 * 新建那些 ns，并拷贝
 */
static struct nsproxy *create_new_namespaces(unsigned long flags,
	struct task_struct *tsk, struct user_namespace *user_ns,
	struct fs_struct *new_fs)
{
	struct nsproxy *new_nsp;
	int err;

    /**
     *  分配内存
     *  nsproxy 一定是要有的，所以没有 flags 传入
     */
	new_nsp = create_nsproxy();
	if (!new_nsp)
		return ERR_PTR(-ENOMEM);

    /**
     *  mnt namespace
     */
	new_nsp->mnt_ns = copy_mnt_ns(flags, tsk->nsproxy->mnt_ns, user_ns, new_fs);
	if (IS_ERR(new_nsp->mnt_ns)) {
		err = PTR_ERR(new_nsp->mnt_ns);
		goto out_ns;
	}

    /**
     *  UTS namespace
     */
	new_nsp->uts_ns = copy_utsname(flags, user_ns, tsk->nsproxy->uts_ns);
	if (IS_ERR(new_nsp->uts_ns)) {
		err = PTR_ERR(new_nsp->uts_ns);
		goto out_uts;
	}

    /**
     *  IPC namespace
     */
	new_nsp->ipc_ns = copy_ipcs(flags, user_ns, tsk->nsproxy->ipc_ns);
	if (IS_ERR(new_nsp->ipc_ns)) {
		err = PTR_ERR(new_nsp->ipc_ns);
		goto out_ipc;
	}

    /**
     *  PID namespace
     */
	new_nsp->pid_ns_for_children =
		copy_pid_ns(flags, user_ns, tsk->nsproxy->pid_ns_for_children);
	if (IS_ERR(new_nsp->pid_ns_for_children)) {
		err = PTR_ERR(new_nsp->pid_ns_for_children);
		goto out_pid;
	}
    /**
     *  cgroup namespace
     */
	new_nsp->cgroup_ns = copy_cgroup_ns(flags, user_ns,
					    tsk->nsproxy->cgroup_ns);
	if (IS_ERR(new_nsp->cgroup_ns)) {
		err = PTR_ERR(new_nsp->cgroup_ns);
		goto out_cgroup;
	}
    /**
     *  网络名字空间
     */
	new_nsp->net_ns = copy_net_ns(flags, user_ns, tsk->nsproxy->net_ns);
	if (IS_ERR(new_nsp->net_ns)) {
		err = PTR_ERR(new_nsp->net_ns);
		goto out_net;
	}
    /**
     *  时间名字空间
     */
	new_nsp->time_ns_for_children = copy_time_ns(flags, user_ns,
					tsk->nsproxy->time_ns_for_children);
	if (IS_ERR(new_nsp->time_ns_for_children)) {
		err = PTR_ERR(new_nsp->time_ns_for_children);
		goto out_time;
	}
	new_nsp->time_ns = get_time_ns(tsk->nsproxy->time_ns);

	/**
	 * @brief 返回新的 ns 结构
	 *
	 */
	return new_nsp;

out_time:
	put_net(new_nsp->net_ns);
out_net:
	put_cgroup_ns(new_nsp->cgroup_ns);
out_cgroup:
	if (new_nsp->pid_ns_for_children)
		put_pid_ns(new_nsp->pid_ns_for_children);
out_pid:
	if (new_nsp->ipc_ns)
		put_ipc_ns(new_nsp->ipc_ns);
out_ipc:
	if (new_nsp->uts_ns)
		put_uts_ns(new_nsp->uts_ns);
out_uts:
	if (new_nsp->mnt_ns)
		put_mnt_ns(new_nsp->mnt_ns);
out_ns:
	kmem_cache_free(nsproxy_cachep, new_nsp);
	return ERR_PTR(err);
}

/*
 * called from clone.  This now handles copy for nsproxy and all
 * namespaces therein.
 * 复制父进程的命名空间
 */
int copy_namespaces(unsigned long flags, struct task_struct *tsk)
{
	/**
	 * @brief
	 *
	 */
	struct nsproxy *old_ns = tsk->nsproxy;
	struct user_namespace *user_ns = task_cred_xxx(tsk, user_ns);
	struct nsproxy *new_ns;
	int ret;

	/**
	 * @brief 如果下面的都 没有 设置
	 *
	 */
	if (likely(!(flags & (CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC |
			      CLONE_NEWPID | CLONE_NEWNET |
			      CLONE_NEWCGROUP | CLONE_NEWTIME)))) {
		if (likely(old_ns->time_ns_for_children == old_ns->time_ns)) {
			get_nsproxy(old_ns);
			return 0;
		}
	/**
	 * @brief 如果设置了其中之一
	 *	如果没有权限创建新的 ns，那么返回错误
	 */
	} else if (!ns_capable(user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * CLONE_NEWIPC must detach from the undolist: after switching
	 * to a new ipc namespace, the semaphore arrays from the old
	 * namespace are unreachable.  In clone parlance, CLONE_SYSVSEM
	 * means share undolist with parent, so we must forbid using
	 * it along with CLONE_NEWIPC.
	 */
	if ((flags & (CLONE_NEWIPC | CLONE_SYSVSEM)) ==
		(CLONE_NEWIPC | CLONE_SYSVSEM))
		return -EINVAL;

    /**
     *  根据 CLONE_xxx 判定创建哪些 namespace
     */
	new_ns = create_new_namespaces(flags, tsk, user_ns, tsk->fs);
	if (IS_ERR(new_ns))
		return  PTR_ERR(new_ns);

    /**
     *
     */
	ret = timens_on_fork(new_ns, tsk);
	if (ret) {
		free_nsproxy(new_ns);
		return ret;
	}

	/**
	 * @brief
	 *
	 */
	tsk->nsproxy = new_ns;
	return 0;
}

/**
 * @brief
 *
 * @param ns
 */
void free_nsproxy(struct nsproxy *ns)
{
	if (ns->mnt_ns)
		put_mnt_ns(ns->mnt_ns);
	if (ns->uts_ns)
		put_uts_ns(ns->uts_ns);
	if (ns->ipc_ns)
		put_ipc_ns(ns->ipc_ns);
	if (ns->pid_ns_for_children)
		put_pid_ns(ns->pid_ns_for_children);
	if (ns->time_ns)
		put_time_ns(ns->time_ns);
	if (ns->time_ns_for_children)
		put_time_ns(ns->time_ns_for_children);
	put_cgroup_ns(ns->cgroup_ns);
	put_net(ns->net_ns);
	kmem_cache_free(nsproxy_cachep, ns);
}

/*
 * Called from unshare. Unshare all the namespaces part of nsproxy.
 * On success, returns the new nsproxy.
 */
int unshare_nsproxy_namespaces(unsigned long unshare_flags,
	struct nsproxy **new_nsp, struct cred *new_cred, struct fs_struct *new_fs)
{
	struct user_namespace *user_ns;
	int err = 0;

    /**
     *
     */
	if (!(unshare_flags & (CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC |
			       CLONE_NEWNET | CLONE_NEWPID | CLONE_NEWCGROUP |
			       CLONE_NEWTIME)))
		return 0;

	user_ns = new_cred ? new_cred->user_ns : current_user_ns();
	if (!ns_capable(user_ns, CAP_SYS_ADMIN))
		return -EPERM;

    /**
     *
     */
	*new_nsp = create_new_namespaces(unshare_flags, current, user_ns,
					 new_fs ? new_fs : current->fs);
	if (IS_ERR(*new_nsp)) {
		err = PTR_ERR(*new_nsp);
		goto out;
	}

out:
	return err;
}

void switch_task_namespaces(struct task_struct *p, struct nsproxy *new)
{
	struct nsproxy *ns;

	might_sleep();

	task_lock(p);
	ns = p->nsproxy;
	p->nsproxy = new;
	task_unlock(p);

	if (ns && atomic_dec_and_test(&ns->count))
		free_nsproxy(ns);
}

void exit_task_namespaces(struct task_struct *p)
{
	switch_task_namespaces(p, NULL);
}

static int check_setns_flags(unsigned long flags)
{
	if (!flags || (flags & ~(CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC |
				 CLONE_NEWNET | CLONE_NEWTIME | CLONE_NEWUSER |
				 CLONE_NEWPID | CLONE_NEWCGROUP)))
		return -EINVAL;

#ifndef CONFIG_USER_NS
	if (flags & CLONE_NEWUSER)
		return -EINVAL;
#endif
#ifndef CONFIG_PID_NS
	if (flags & CLONE_NEWPID)
		return -EINVAL;
#endif
#ifndef CONFIG_UTS_NS
	if (flags & CLONE_NEWUTS)
		return -EINVAL;
#endif
#ifndef CONFIG_IPC_NS
	if (flags & CLONE_NEWIPC)
		return -EINVAL;
#endif
#ifndef CONFIG_CGROUPS
	if (flags & CLONE_NEWCGROUP)
		return -EINVAL;
#endif
#ifndef CONFIG_NET_NS
	if (flags & CLONE_NEWNET)
		return -EINVAL;
#endif
#ifndef CONFIG_TIME_NS
	if (flags & CLONE_NEWTIME)
		return -EINVAL;
#endif

	return 0;
}

static void put_nsset(struct nsset *nsset)
{
	unsigned flags = nsset->flags;

	if (flags & CLONE_NEWUSER)
		put_cred(nsset_cred(nsset));
	/*
	 * We only created a temporary copy if we attached to more than just
	 * the mount namespace.
	 */
	if (nsset->fs && (flags & CLONE_NEWNS) && (flags & ~CLONE_NEWNS))
		free_fs_struct(nsset->fs);
	if (nsset->nsproxy)
		free_nsproxy(nsset->nsproxy);
}

/**
 *
 */
static int prepare_nsset(unsigned flags, struct nsset *__nsset)
{
	struct task_struct *me = current;

    /**
     *  创建新的 namespace
     */
	__nsset->nsproxy = create_new_namespaces(0, me, current_user_ns(), me->fs);
	if (IS_ERR(__nsset->nsproxy))
		return PTR_ERR(__nsset->nsproxy);

	if (flags & CLONE_NEWUSER)
		__nsset->cred = prepare_creds();
	else
		__nsset->cred = current_cred();
	if (!__nsset->cred)
		goto out;

	/* Only create a temporary copy of fs_struct if we really need to. */
	if (flags == CLONE_NEWNS) {
		__nsset->fs = me->fs;
	} else if (flags & CLONE_NEWNS) {
		__nsset->fs = copy_fs_struct(me->fs);
		if (!__nsset->fs)
			goto out;
	}

	__nsset->flags = flags;
	return 0;

out:
	put_nsset(__nsset);
	return -ENOMEM;
}

/**
 *  使能
 */
static inline int validate_ns(struct nsset *nsset, struct ns_common *ns)
{
    /**
     *
     */
	return ns->ops->install(nsset, ns);
}

/*
 * This is the inverse operation to unshare().
 * Ordering is equivalent to the standard ordering used everywhere else
 * during unshare and process creation. The switch to the new set of
 * namespaces occurs at the point of no return after installation of
 * all requested namespaces was successful in commit_nsset().
 */
static int validate_nsset(struct nsset *nsset, struct pid *pid)
{
	int ret = 0;
	unsigned flags = nsset->flags;
	struct user_namespace *user_ns = NULL;
	struct pid_namespace *pid_ns = NULL;
	struct nsproxy *nsp;
	struct task_struct *tsk;

	/* Take a "snapshot" of the target task's namespaces. */
	rcu_read_lock();
	tsk = pid_task(pid, PIDTYPE_PID);
	if (!tsk) {
		rcu_read_unlock();
		return -ESRCH;
	}

	if (!ptrace_may_access(tsk, PTRACE_MODE_READ_REALCREDS)) {
		rcu_read_unlock();
		return -EPERM;
	}

	task_lock(tsk);
	nsp = tsk->nsproxy;
	if (nsp)
		get_nsproxy(nsp);
	task_unlock(tsk);
	if (!nsp) {
		rcu_read_unlock();
		return -ESRCH;
	}

#ifdef CONFIG_PID_NS
	if (flags & CLONE_NEWPID) {
		pid_ns = task_active_pid_ns(tsk);
		if (unlikely(!pid_ns)) {
			rcu_read_unlock();
			ret = -ESRCH;
			goto out;
		}
		get_pid_ns(pid_ns);
	}
#endif

#ifdef CONFIG_USER_NS
	if (flags & CLONE_NEWUSER)
		user_ns = get_user_ns(__task_cred(tsk)->user_ns);
#endif
	rcu_read_unlock();

	/*
	 * Install requested namespaces. The caller will have
	 * verified earlier that the requested namespaces are
	 * supported on this kernel. We don't report errors here
	 * if a namespace is requested that isn't supported.
	 */
#ifdef CONFIG_USER_NS
	if (flags & CLONE_NEWUSER) {
		ret = validate_ns(nsset, &user_ns->ns);
		if (ret)
			goto out;
	}
#endif

	if (flags & CLONE_NEWNS) {
		ret = validate_ns(nsset, from_mnt_ns(nsp->mnt_ns));
		if (ret)
			goto out;
	}

#ifdef CONFIG_UTS_NS
	if (flags & CLONE_NEWUTS) {
		ret = validate_ns(nsset, &nsp->uts_ns->ns);
		if (ret)
			goto out;
	}
#endif

#ifdef CONFIG_IPC_NS
	if (flags & CLONE_NEWIPC) {
		ret = validate_ns(nsset, &nsp->ipc_ns->ns);
		if (ret)
			goto out;
	}
#endif

#ifdef CONFIG_PID_NS
	if (flags & CLONE_NEWPID) {
		ret = validate_ns(nsset, &pid_ns->ns);
		if (ret)
			goto out;
	}
#endif

#ifdef CONFIG_CGROUPS
	if (flags & CLONE_NEWCGROUP) {
		ret = validate_ns(nsset, &nsp->cgroup_ns->ns);
		if (ret)
			goto out;
	}
#endif

#ifdef CONFIG_NET_NS
	if (flags & CLONE_NEWNET) {
		ret = validate_ns(nsset, &nsp->net_ns->ns);
		if (ret)
			goto out;
	}
#endif

#ifdef CONFIG_TIME_NS
	if (flags & CLONE_NEWTIME) {
		ret = validate_ns(nsset, &nsp->time_ns->ns);
		if (ret)
			goto out;
	}
#endif

out:
	if (pid_ns)
		put_pid_ns(pid_ns);
	if (nsp)
		put_nsproxy(nsp);
	put_user_ns(user_ns);

	return ret;
}

/*
 * This is the point of no return. There are just a few namespaces
 * that do some actual work here and it's sufficiently minimal that
 * a separate ns_common operation seems unnecessary for now.
 * Unshare is doing the same thing. If we'll end up needing to do
 * more in a given namespace or a helper here is ultimately not
 * exported anymore a simple commit handler for each namespace
 * should be added to ns_common.
 */
static void commit_nsset(struct nsset *nsset)
{
	unsigned flags = nsset->flags;
	struct task_struct *me = current;

#ifdef CONFIG_USER_NS
	if (flags & CLONE_NEWUSER) {
		/* transfer ownership */
		commit_creds(nsset_cred(nsset));
		nsset->cred = NULL;
	}
#endif

	/* We only need to commit if we have used a temporary fs_struct. */
	if ((flags & CLONE_NEWNS) && (flags & ~CLONE_NEWNS)) {
		set_fs_root(me->fs, &nsset->fs->root);
		set_fs_pwd(me->fs, &nsset->fs->pwd);
	}

#ifdef CONFIG_IPC_NS
	if (flags & CLONE_NEWIPC)
		exit_sem(me);
#endif

#ifdef CONFIG_TIME_NS
	if (flags & CLONE_NEWTIME)
		timens_commit(me, nsset->nsproxy->time_ns);
#endif

	/* transfer ownership */
	switch_task_namespaces(me, nsset->nsproxy);
	nsset->nsproxy = NULL;
}

/**
 *  setns(2) - 把进程加入到指定的Namespace中
 *
 *  fd参数：表示文件描述符，前面提到可以通过打开/proc/$pid/ns/的方式将指定的Namespace保留下来，
 *      也就是说可以通过文件描述符的方式来索引到某个Namespace。
 *  nstype参数：用来检查fd关联Namespace是否与nstype表明的Namespace一致，
 *      如果填0的话表示不进行该项检查。
 */
int setns(int fd, int nstype){}//+++++
SYSCALL_DEFINE2(setns, int, fd, int, flags)
{
	struct file *file;
	struct ns_common *ns = NULL;
	struct nsset __nsset = {};
	int err = 0;

	file = fget(fd);
	if (!file)
		return -EBADF;
    /**
     *  是否为ns
     *  file->f_op == &ns_file_operations;
     */
	if (proc_ns_file(file)) {
        /**
         *  ns = ((struct ns_common *)(inode)->i_private)
         */
		ns = get_proc_ns(file_inode(file));
		if (flags && (ns->ops->type != flags))
			err = -EINVAL;
		flags = ns->ops->type;
	} else if (!IS_ERR(pidfd_pid(file))) {
		err = check_setns_flags(flags);
	} else {
		err = -EINVAL;
	}
	if (err)
		goto out;

    /**
     *
     */
	err = prepare_nsset(flags, &__nsset);
	if (err)
		goto out;
    /**
     *  why again?
     */
	if (proc_ns_file(file))
		err = validate_ns(&__nsset, ns);
	else
		err = validate_nsset(&__nsset, file->private_data);

	if (!err) {
		commit_nsset(&__nsset);
		perf_event_namespaces(current);
	}
	put_nsset(&__nsset);
out:
	fput(file);
	return err;
}

/**
 *  namespace proxy slab 初始化
 */
int __init nsproxy_cache_init(void)
{
	nsproxy_cachep = KMEM_CACHE(nsproxy, SLAB_PANIC);
	return 0;
}
