// SPDX-License-Identifier: GPL-2.0-only
/*
 * This is the 1999 rewrite of IP Firewalling, aiming for kernel 2.3.x.
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 * Copyright (C) 2000-2004 Netfilter Core Team <coreteam@netfilter.org>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("ip6tables filter table");

#define FILTER_VALID_HOOKS ((1 << NF_INET_LOCAL_IN) | \
            			    (1 << NF_INET_FORWARD) | \
            			    (1 << NF_INET_LOCAL_OUT))

static int __net_init ip6table_filter_table_init(struct net *net);

/**
 *  
 */
static const struct xt_table packet6_filter = {
	packet6_filter.name		= "filter",
	packet6_filter.valid_hooks	= FILTER_VALID_HOOKS,
	packet6_filter.me		= THIS_MODULE,
	packet6_filter.af		= NFPROTO_IPV6,
	packet6_filter.priority	= NF_IP6_PRI_FILTER,
	packet6_filter.table_init	= ip6table_filter_table_init,
};

/* The work comes in here from netfilter.c. */
static unsigned int
ip6table_filter_hook(void *priv, struct sk_buff *skb,
		                const struct nf_hook_state *state)
{
	return ip6t_do_table(skb, state, state->net->ipv6.ip6table_filter);
}

static struct nf_hook_ops __read_mostly *filter_ops ;

/* Default to forward because I got too much mail already. */
static bool forward = true;
module_param(forward, bool, 0000);

static int __net_init ip6table_filter_table_init(struct net *net)
{
	struct ip6t_replace *repl;
	int err;

	if (net->ipv6.ip6table_filter)
		return 0;

	repl = ip6t_alloc_initial_table(&packet6_filter);
	if (repl == NULL)
		return -ENOMEM;
	/* Entry 1 is the FORWARD hook */
	((struct ip6t_standard *)repl->entries)[1].target.verdict =
		forward ? -NF_ACCEPT - 1 : -NF_DROP - 1;

	err = ip6t_register_table(net, &packet6_filter, repl, filter_ops,
				  &net->ipv6.ip6table_filter);
	kfree(repl);
	return err;
}

/**
 *  
 */
static int __net_init ip6table_filter_net_init(struct net *net)
{
	if (net == &init_net || !forward)
		return ip6table_filter_table_init(net);

	return 0;
}

/**
 *  
 */
static void __net_exit ip6table_filter_net_pre_exit(struct net *net)
{
	if (net->ipv6.ip6table_filter)
		ip6t_unregister_table_pre_exit(net, net->ipv6.ip6table_filter, filter_ops);
}

/**
 *  
 */
static void __net_exit ip6table_filter_net_exit(struct net *net)
{
	if (!net->ipv6.ip6table_filter)
		return;
	ip6t_unregister_table_exit(net, net->ipv6.ip6table_filter);
	net->ipv6.ip6table_filter = NULL;
}

/**
 *  
 */
static struct pernet_operations ip6table_filter_net_ops = {
	ip6table_filter_net_ops.init = ip6table_filter_net_init,
	ip6table_filter_net_ops.pre_exit = ip6table_filter_net_pre_exit,
	ip6table_filter_net_ops.exit = ip6table_filter_net_exit,
};

/**
 *  
 */
static int __init ip6table_filter_init(void)
{
	int ret;

	filter_ops = xt_hook_ops_alloc(&packet6_filter, ip6table_filter_hook);
	if (IS_ERR(filter_ops))
		return PTR_ERR(filter_ops);

	ret = register_pernet_subsys(&ip6table_filter_net_ops);
	if (ret < 0)
		kfree(filter_ops);

	return ret;
}

/**
 *  
 */
static void __exit ip6table_filter_fini(void)
{
	unregister_pernet_subsys(&ip6table_filter_net_ops);
	kfree(filter_ops);
}

module_init(ip6table_filter_init);
module_exit(ip6table_filter_fini);
