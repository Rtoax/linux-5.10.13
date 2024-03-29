// SPDX-License-Identifier: GPL-2.0-only
/*
 * "security" table
 *
 * This is for use by Mandatory Access Control (MAC) security models,
 * which need to be able to manage security policy in separate context
 * to DAC.
 *
 * Based on iptable_mangle.c
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 * Copyright (C) 2000-2004 Netfilter Core Team <coreteam <at> netfilter.org>
 * Copyright (C) 2008 Red Hat, Inc., James Morris <jmorris <at> redhat.com>
 */
#include <linux/module.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/slab.h>
#include <net/ip.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Morris <jmorris <at> redhat.com>");
MODULE_DESCRIPTION("iptables security table, for MAC rules");

#define SECURITY_VALID_HOOKS	(1 << NF_INET_LOCAL_IN) | \
                				(1 << NF_INET_FORWARD) | \
                				(1 << NF_INET_LOCAL_OUT)

static int __net_init iptable_security_table_init(struct net *net);

/**
 *  
 */
static const struct xt_table security_table = {
	security_table.name		= "security",
	security_table.valid_hooks	= SECURITY_VALID_HOOKS,
	security_table.me		= THIS_MODULE,
	security_table.af		= NFPROTO_IPV4,
	security_table.priority	= NF_IP_PRI_SECURITY,
	security_table.table_init	= iptable_security_table_init,
};

static unsigned int
iptable_security_hook(void *priv, struct sk_buff *skb,
		      const struct nf_hook_state *state)
{
	return ipt_do_table(skb, state, state->net->ipv4.iptable_security);
}

static struct nf_hook_ops __read_mostly *sectbl_ops ;

/**
 *  
 */
static int __net_init iptable_security_table_init(struct net *net)
{
	struct ipt_replace *repl;
	int ret;

	if (net->ipv4.iptable_security)
		return 0;

	repl = ipt_alloc_initial_table(&security_table);
	if (repl == NULL)
		return -ENOMEM;

    /**
     *  
     */
	ret = ipt_register_table(net, &security_table, repl, sectbl_ops,
				 &net->ipv4.iptable_security);
	kfree(repl);
	return ret;
}

/**
 *  
 */
static void __net_exit iptable_security_net_pre_exit(struct net *net)
{
	if (net->ipv4.iptable_security)
		ipt_unregister_table_pre_exit(net, net->ipv4.iptable_security, sectbl_ops);
}

static void __net_exit iptable_security_net_exit(struct net *net)
{
	if (!net->ipv4.iptable_security)
		return;
	ipt_unregister_table_exit(net, net->ipv4.iptable_security);
	net->ipv4.iptable_security = NULL;
}

static struct pernet_operations iptable_security_net_ops = {
	iptable_security_net_ops.pre_exit = iptable_security_net_pre_exit,
	iptable_security_net_ops.exit = iptable_security_net_exit,
};

/**
 *  
 */
static int __init iptable_security_init(void)
{
	int ret;

	sectbl_ops = xt_hook_ops_alloc(&security_table, iptable_security_hook);
	if (IS_ERR(sectbl_ops))
		return PTR_ERR(sectbl_ops);

    /**
     *  
     */
	ret = register_pernet_subsys(&iptable_security_net_ops);
	if (ret < 0) {
		kfree(sectbl_ops);
		return ret;
	}

	ret = iptable_security_table_init(&init_net);
	if (ret) {
		unregister_pernet_subsys(&iptable_security_net_ops);
		kfree(sectbl_ops);
	}

	return ret;
}

/**
 *  
 */
static void __exit iptable_security_fini(void)
{
	unregister_pernet_subsys(&iptable_security_net_ops);
	kfree(sectbl_ops);
}

/**
 *  [rongtao@localhost src]$ lsmod | grep iptab
 *  iptable_nat            12875  1 
 *  nf_nat_ipv4            14115  1 iptable_nat
 *  iptable_mangle         12695  1 
 *  iptable_security       12705  1 
 *  iptable_raw            12678  1 
 *  iptable_filter         12810  1
 */


module_init(iptable_security_init);
module_exit(iptable_security_fini);
