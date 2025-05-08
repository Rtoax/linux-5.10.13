// SPDX-License-Identifier: GPL-2.0
/* -*- linux-c -*-
 * sysctl_net_core.c: sysctl interface to net core subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/core directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/netdevice.h>
#include <linux/ratelimit.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <net/ip.h>
#include <net/sock.h>
#include <net/net_ratelimit.h>
#include <net/busy_poll.h>
#include <net/pkt_sched.h>

static int two = 2;
static int three = 3;
static int min_sndbuf = SOCK_MIN_SNDBUF;
static int min_rcvbuf = SOCK_MIN_RCVBUF;
static int max_skb_frags = MAX_SKB_FRAGS;
static long __maybe_unused long_one  = 1;
static long __maybe_unused long_max  = LONG_MAX;

static int net_msg_warn;	/* Unused, but still a sysctl */

int __read_mostly sysctl_fb_tunnels_only_for_init_net  = 0;
EXPORT_SYMBOL(sysctl_fb_tunnels_only_for_init_net);

/* 0 - Keep current behavior:
 *     IPv4: inherit all current settings from init_net
 *     IPv6: reset all settings to default
 * 1 - Both inherit all current settings from init_net
 * 2 - Both reset all settings to default
 * 3 - Both inherit all settings from current netns
 */
int __read_mostly sysctl_devconf_inherit_init_net ;
EXPORT_SYMBOL(sysctl_devconf_inherit_init_net);

#ifdef CONFIG_RPS
static int rps_sock_flow_sysctl(struct ctl_table *table, int write,
				void *buffer, size_t *lenp, loff_t *ppos)
{
	unsigned int orig_size, size;
	int ret, i;
	struct ctl_table tmp = {
		.data = &size,
		.maxlen = sizeof(size),
		.mode = table->mode
	};
	struct rps_sock_flow_table *orig_sock_table, *sock_table;
	static DEFINE_MUTEX(sock_flow_mutex);

	mutex_lock(&sock_flow_mutex);

	orig_sock_table = rcu_dereference_protected(rps_sock_flow_table,
					lockdep_is_held(&sock_flow_mutex));
	size = orig_size = orig_sock_table ? orig_sock_table->mask + 1 : 0;

	ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);

	if (write) {
		if (size) {
			if (size > 1<<29) {
				/* Enforce limit to prevent overflow */
				mutex_unlock(&sock_flow_mutex);
				return -EINVAL;
			}
			size = roundup_pow_of_two(size);
			if (size != orig_size) {
				sock_table =
				    vmalloc(RPS_SOCK_FLOW_TABLE_SIZE(size));
				if (!sock_table) {
					mutex_unlock(&sock_flow_mutex);
					return -ENOMEM;
				}
				rps_cpu_mask = roundup_pow_of_two(nr_cpu_ids) - 1;
				sock_table->mask = size - 1;
			} else
				sock_table = orig_sock_table;

			for (i = 0; i < size; i++)
				sock_table->ents[i] = RPS_NO_CPU;
		} else
			sock_table = NULL;

		if (sock_table != orig_sock_table) {
			rcu_assign_pointer(rps_sock_flow_table, sock_table);
			if (sock_table) {
				static_branch_inc(&rps_needed);
				static_branch_inc(&rfs_needed);
			}
			if (orig_sock_table) {
				static_branch_dec(&rps_needed);
				static_branch_dec(&rfs_needed);
				synchronize_rcu();
				vfree(orig_sock_table);
			}
		}
	}

	mutex_unlock(&sock_flow_mutex);

	return ret;
}
#endif /* CONFIG_RPS */

#ifdef CONFIG_NET_FLOW_LIMIT
static DEFINE_MUTEX(flow_limit_update_mutex);

static int flow_limit_cpu_sysctl(struct ctl_table *table, int write,
				 void *buffer, size_t *lenp, loff_t *ppos)
{
	struct sd_flow_limit *cur;
	struct softnet_data *sd;
	cpumask_var_t mask;
	int i, len, ret = 0;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	if (write) {
		ret = cpumask_parse(buffer, mask);
		if (ret)
			goto done;

		mutex_lock(&flow_limit_update_mutex);
		len = sizeof(*cur) + netdev_flow_limit_table_len;
		for_each_possible_cpu(i) {
			sd = &per_cpu(softnet_data, i);
			cur = rcu_dereference_protected(sd->flow_limit,
				     lockdep_is_held(&flow_limit_update_mutex));
			if (cur && !cpumask_test_cpu(i, mask)) {
				RCU_INIT_POINTER(sd->flow_limit, NULL);
				synchronize_rcu();
				kfree(cur);
			} else if (!cur && cpumask_test_cpu(i, mask)) {
				cur = kzalloc_node(len, GFP_KERNEL,
						   cpu_to_node(i));
				if (!cur) {
					/* not unwinding previous changes */
					ret = -ENOMEM;
					goto write_unlock;
				}
				cur->num_buckets = netdev_flow_limit_table_len;
				rcu_assign_pointer(sd->flow_limit, cur);
			}
		}
write_unlock:
		mutex_unlock(&flow_limit_update_mutex);
	} else {
		char kbuf[128];

		if (*ppos || !*lenp) {
			*lenp = 0;
			goto done;
		}

		cpumask_clear(mask);
		rcu_read_lock();
		for_each_possible_cpu(i) {
			sd = &per_cpu(softnet_data, i);
			if (rcu_dereference(sd->flow_limit))
				cpumask_set_cpu(i, mask);
		}
		rcu_read_unlock();

		len = min(sizeof(kbuf) - 1, *lenp);
		len = scnprintf(kbuf, len, "%*pb", cpumask_pr_args(mask));
		if (!len) {
			*lenp = 0;
			goto done;
		}
		if (len < *lenp)
			kbuf[len++] = '\n';
		memcpy(buffer, kbuf, len);
		*lenp = len;
		*ppos += len;
	}

done:
	free_cpumask_var(mask);
	return ret;
}

static int flow_limit_table_len_sysctl(struct ctl_table *table, int write,
				       void *buffer, size_t *lenp, loff_t *ppos)
{
	unsigned int old, *ptr;
	int ret;

	mutex_lock(&flow_limit_update_mutex);

	ptr = table->data;
	old = *ptr;
	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!ret && write && !is_power_of_2(*ptr)) {
		*ptr = old;
		ret = -EINVAL;
	}

	mutex_unlock(&flow_limit_update_mutex);
	return ret;
}
#endif /* CONFIG_NET_FLOW_LIMIT */

#ifdef CONFIG_NET_SCHED
static int set_default_qdisc(struct ctl_table *table, int write,
			     void *buffer, size_t *lenp, loff_t *ppos)
{
	char id[IFNAMSIZ];
	struct ctl_table tbl = {
		.data = id,
		.maxlen = IFNAMSIZ,
	};
	int ret;

	qdisc_get_default(id, IFNAMSIZ);

	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);
	if (write && ret == 0)
		ret = qdisc_set_default(id);
	return ret;
}
#endif

static int proc_do_dev_weight(struct ctl_table *table, int write,
			   void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (ret != 0)
		return ret;

	dev_rx_weight = weight_p * dev_weight_rx_bias;
	dev_tx_weight = weight_p * dev_weight_tx_bias;

	return ret;
}

static int proc_do_rss_key(struct ctl_table *table, int write,
			   void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table fake_table;
	char buf[NETDEV_RSS_KEY_LEN * 3];

	snprintf(buf, sizeof(buf), "%*phC", NETDEV_RSS_KEY_LEN, netdev_rss_key);
	fake_table.data = buf;
	fake_table.maxlen = sizeof(buf);
	return proc_dostring(&fake_table, write, buffer, lenp, ppos);
}

#ifdef CONFIG_BPF_JIT
static int proc_dointvec_minmax_bpf_enable(struct ctl_table *table, int write,
					   void *buffer, size_t *lenp,
					   loff_t *ppos)
{
	int ret, jit_enable = *(int *)table->data;
	struct ctl_table tmp = *table;

	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	tmp.data = &jit_enable;
	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);
	if (write && !ret) {
		if (jit_enable < 2 ||
		    (jit_enable == 2 && bpf_dump_raw_ok(current_cred()))) {
			*(int *)table->data = jit_enable;
			if (jit_enable == 2)
				pr_warn("bpf_jit_enable = 2 was set! NEVER use this in production, only for JIT debugging!\n");
		} else {
			ret = -EPERM;
		}
	}
	return ret;
}

# ifdef CONFIG_HAVE_EBPF_JIT
static int
proc_dointvec_minmax_bpf_restricted(struct ctl_table *table, int write,
				    void *buffer, size_t *lenp, loff_t *ppos)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return proc_dointvec_minmax(table, write, buffer, lenp, ppos);
}
# endif /* CONFIG_HAVE_EBPF_JIT */

static int
proc_dolongvec_minmax_bpf_restricted(struct ctl_table *table, int write,
				     void *buffer, size_t *lenp, loff_t *ppos)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
}
#endif

static struct ctl_table net_core_table[] = {    /* /proc/sys/net/core/ */
#ifdef CONFIG_NET
	/**
	 * net.core.wmem_max
	 * /proc/sys/net/core/wmem_max
	 */
	{
		.procname	= "wmem_max",
		.data		= &sysctl_wmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_sndbuf,
	},
	{
		.procname	= "rmem_max",   /* /proc/sys/net/core/rmem_max */
		.data		= &sysctl_rmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_rcvbuf,
	},
	{
		.procname	= "wmem_default",   /* /proc/sys/net/core/wmem_default */
		.data		= &sysctl_wmem_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_sndbuf,
	},
	{
		.procname	= "rmem_default",   /* /proc/sys/net/core/ */
		.data		= &sysctl_rmem_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_rcvbuf,
	},
	{
		.procname	= "dev_weight", /* /proc/sys/net/core/ */
		.data		= &weight_p,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_do_dev_weight,
	},
	{
		.procname	= "dev_weight_rx_bias", /* /proc/sys/net/core/ */
		.data		= &dev_weight_rx_bias,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_do_dev_weight,
	},
	{
		.procname	= "dev_weight_tx_bias", /* /proc/sys/net/core/ */
		.data		= &dev_weight_tx_bias,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_do_dev_weight,
	},
	{
		// /proc/sys/net/core/netdev_max_backlog
		.procname	= "netdev_max_backlog", /* /proc/sys/net/core/ */
		.data		= &netdev_max_backlog,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "netdev_rss_key", /* /proc/sys/net/core/ */
		.data		= &netdev_rss_key,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_do_rss_key,
	},
#ifdef CONFIG_BPF_JIT
	{
		/**
		 * /proc/sys/net/core/bpf_jit_enable
		 */
		.procname	= "bpf_jit_enable", /* /proc/sys/net/core/ */
		.data		= &bpf_jit_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax_bpf_enable,
/**
 * Linux 内核提供了选项CONFIG_BPF_JIT_ALWAYS_ON，该选项从内核中删除整个
 * BPF 解释器并永久启用 JIT 编译器。这是作为 Spectre v2 上下文中缓解措施
 * 的一部分开发的，因此在基于 VM 的设置中使用时，来宾内核在发起攻击时不
 * 会再重用主机内核的 BPF 解释器。对于基于容器的环境，
 * CONFIG_BPF_JIT_ALWAYS_ON配置选项是可选的，但如果在那里启用了 JIT，则
 * 解释器也可以编译出来以降低内核的复杂性。因此，对于主流架构（如 x86_64
 * 和 arm64），通常也建议将其用于广泛使用的 JIT。
 *
 * https://docs.cilium.io/en/stable/bpf/
 */
# ifdef CONFIG_BPF_JIT_ALWAYS_ON
		.extra1		= SYSCTL_ONE,
		.extra2		= SYSCTL_ONE,
# else
		.extra1		= SYSCTL_ZERO,
		.extra2		= &two,
# endif
	},
# ifdef CONFIG_HAVE_EBPF_JIT
	{
		/**
		 *  /proc/sys/net/core/bpf_jit_harden
		 *
		 *  设置为 1 会为非特权用户（ unprivileged users）的 JIT
		 *  编译做一些额外的加固工作。这些额外加固会稍微降低程序
		 *  的性能，但在有非受信用户在系统上进行操作的情况下，
		 *  能够有效地减小（潜在的）受攻击面。但与完全切换到解释器相比，
		 *  这些性能损失还是比较小的。
		 *
		 *  https://docs.cilium.io/en/stable/bpf/
		 *
		 * Example of JITing a program with hardening disabled:
		 *
		 *  echo 0 > /proc/sys/net/core/bpf_jit_harden
		 *  ffffffffa034f5e9 + <x>:
		 *  [...]
		 *  39:   mov    $0xa8909090,%eax
		 *  3e:   mov    $0xa8909090,%eax
		 *  43:   mov    $0xa8ff3148,%eax
		 *  48:   mov    $0xa89081b4,%eax
		 *  4d:   mov    $0xa8900bb0,%eax
		 *  52:   mov    $0xa810e0c1,%eax
		 *  57:   mov    $0xa8908eb4,%eax
		 *  5c:   mov    $0xa89020b0,%eax
		 *  [...]
		 *
		 * The same program gets constant blinded when loaded through BPF
		 * as an unprivileged user in the case hardening is enabled:
		 *
		 *  echo 1 > /proc/sys/net/core/bpf_jit_harden
		 *
		 *  ffffffffa034f1e5 + <x>:
		 *  [...]
		 *  39:   mov    $0xe1192563,%r10d
		 *  3f:   xor    $0x4989b5f3,%r10d
		 *  46:   mov    %r10d,%eax
		 *  49:   mov    $0xb8296d93,%r10d
		 *  4f:   xor    $0x10b9fd03,%r10d
		 *  56:   mov    %r10d,%eax
		 *  59:   mov    $0x8c381146,%r10d
		 *  5f:   xor    $0x24c7200e,%r10d
		 *  66:   mov    %r10d,%eax
		 *  69:   mov    $0xeb2a830e,%r10d
		 *  6f:   xor    $0x43ba02ba,%r10d
		 *  76:   mov    %r10d,%eax
		 *  79:   mov    $0xd9730af,%r10d
		 *  7f:   xor    $0xa5073b1f,%r10d
		 *  86:   mov    %r10d,%eax
		 *  89:   mov    $0x9a45662b,%r10d
		 *  8f:   xor    $0x325586ea,%r10d
		 *  96:   mov    %r10d,%eax
		 *  [...]
		 *
		 *  这两个程序在语义上是相同的，只是在第二个程序的反汇编中不再
		 *  可以看到原始的即时值。
		 *
		 *  同时，强化还会禁用特权用户的任何 JIT kallsyms 公开，从而防止
		 *  JIT 映像地址不再向 /proc/kallsyms 公开。
		 */
		.procname	= "bpf_jit_harden", /* /proc/sys/net/core/ */
		.data		= &bpf_jit_harden,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec_minmax_bpf_restricted,
		.extra1		= SYSCTL_ZERO,
		.extra2		= &two,
	},
	{
		.procname	= "bpf_jit_kallsyms",   /* /proc/sys/net/core/ */
		.data		= &bpf_jit_kallsyms,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec_minmax_bpf_restricted,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
# endif
	{
		.procname	= "bpf_jit_limit",  /* /proc/sys/net/core/ */
		.data		= &bpf_jit_limit,
		.maxlen		= sizeof(long),
		.mode		= 0600,
		.proc_handler	= proc_dolongvec_minmax_bpf_restricted,
		.extra1		= &long_one,
		.extra2		= &long_max,
	},
#endif
	{
		.procname	= "netdev_tstamp_prequeue", /* /proc/sys/net/core/ */
		.data		= &netdev_tstamp_prequeue,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "message_cost",   /* /proc/sys/net/core/ */
		.data		= &net_ratelimit_state.interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "message_burst",  /* /proc/sys/net/core/ */
		.data		= &net_ratelimit_state.burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "optmem_max", /* /proc/sys/net/core/ */
		.data		= &sysctl_optmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "tstamp_allow_data",  /* /proc/sys/net/core/ */
		.data		= &sysctl_tstamp_allow_data,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE
	},
#ifdef CONFIG_RPS
	{
		.procname	= "rps_sock_flow_entries",  /* /proc/sys/net/core/ */
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= rps_sock_flow_sysctl
	},
#endif
#ifdef CONFIG_NET_FLOW_LIMIT
	{
		.procname	= "flow_limit_cpu_bitmap",  /* /proc/sys/net/core/ */
		.mode		= 0644,
		.proc_handler	= flow_limit_cpu_sysctl
	},
	{
		.procname	= "flow_limit_table_len",   /* /proc/sys/net/core/ */
		.data		= &netdev_flow_limit_table_len,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= flow_limit_table_len_sysctl
	},
#endif /* CONFIG_NET_FLOW_LIMIT */
#ifdef CONFIG_NET_RX_BUSY_POLL
	//net.core.busy_poll = 0
	{
		.procname	= "busy_poll",  /* /proc/sys/net/core/busy_poll */
		.data		= &sysctl_net_busy_poll,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{
		.procname	= "busy_read",  /* /proc/sys/net/core/ */
		.data		= &sysctl_net_busy_read,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
#endif
#ifdef CONFIG_NET_SCHED
	{
		.procname	= "default_qdisc",  /* /proc/sys/net/core/default_qdisc */
		.mode		= 0644,
		.maxlen		= IFNAMSIZ,
		.proc_handler	= set_default_qdisc
	},
#endif
#endif /* CONFIG_NET */
	{
		.procname	= "netdev_budget",  /* /proc/sys/net/core/ */
		.data		= &netdev_budget,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "warnings",   /* /proc/sys/net/core/ */
		.data		= &net_msg_warn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "max_skb_frags",  /* /proc/sys/net/core/ */
		.data		= &sysctl_max_skb_frags,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
		.extra2		= &max_skb_frags,
	},
	{
		.procname	= "netdev_budget_usecs",    /* /proc/sys/net/core/ */
		.data		= &netdev_budget_usecs,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{
		.procname	= "fb_tunnels_only_for_init_net",   /* /proc/sys/net/core/ */
		.data		= &sysctl_fb_tunnels_only_for_init_net,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= &two,
	},
	{
		.procname	= "devconf_inherit_init_net",   /* /proc/sys/net/core/ */
		.data		= &sysctl_devconf_inherit_init_net,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= &three,
	},
	{
		.procname	= "high_order_alloc_disable",   /* /proc/sys/net/core/ */
		.data		= &net_high_order_alloc_disable_key.key,
		.maxlen         = sizeof(net_high_order_alloc_disable_key),
		.mode		= 0644,
		.proc_handler	= proc_do_static_key,
	},
	{
		.procname	= "gro_normal_batch",   /* /proc/sys/net/core/ */
		.data		= &gro_normal_batch,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
	},
	{ }
};

static struct ctl_table netns_core_table[] = {  /* /proc/sys/net/core/somaxconn */
	{
		.procname	= "somaxconn",
		.data		= &init_net.core.sysctl_somaxconn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.extra1		= SYSCTL_ZERO,
		.proc_handler	= proc_dointvec_minmax
	},
	{ }
};

static int __init fb_tunnels_only_for_init_net_sysctl_setup(char *str)
{
	/* fallback tunnels for initns only */
	if (!strncmp(str, "initns", 6))
		sysctl_fb_tunnels_only_for_init_net = 1;
	/* no fallback tunnels anywhere */
	else if (!strncmp(str, "none", 4))
		sysctl_fb_tunnels_only_for_init_net = 2;

	return 1;
}
__setup("fb_tunnels=", fb_tunnels_only_for_init_net_sysctl_setup);

static __net_init int sysctl_core_net_init(struct net *net)
{
	struct ctl_table *tbl;

	tbl = netns_core_table; /* /proc/sys/net/core/ */
	if (!net_eq(net, &init_net)) {
		tbl = kmemdup(tbl, sizeof(netns_core_table), GFP_KERNEL);
		if (tbl == NULL)
			goto err_dup;

		tbl[0].data = &net->core.sysctl_somaxconn;

		/* Don't export any sysctls to unprivileged users */
		if (net->user_ns != &init_user_ns) {
			tbl[0].procname = NULL;
		}
	}

	net->core.sysctl_hdr = register_net_sysctl(net, "net/core", tbl);
	if (net->core.sysctl_hdr == NULL)
		goto err_reg;

	return 0;

err_reg:
	if (tbl != netns_core_table)
		kfree(tbl);
err_dup:
	return -ENOMEM;
}

static __net_exit void sysctl_core_net_exit(struct net *net)
{
	struct ctl_table *tbl;

	tbl = net->core.sysctl_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->core.sysctl_hdr);
	BUG_ON(tbl == netns_core_table);
	kfree(tbl);
}

static __net_initdata struct pernet_operations sysctl_core_ops = {
	.init = sysctl_core_net_init,
	.exit = sysctl_core_net_exit,
};

static __init int sysctl_core_init(void)
{
	register_net_sysctl(&init_net, "net/core", net_core_table); /* /proc/sys/net/core/xxx */
	return register_pernet_subsys(&sysctl_core_ops);
}

fs_initcall(sysctl_core_init);  /* /proc/sys/net/core/xxx */

