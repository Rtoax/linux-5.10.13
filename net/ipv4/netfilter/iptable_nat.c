// SPDX-License-Identifier: GPL-2.0-only
/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2011 Patrick McHardy <kaber@trash.net>
 */

#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/ip.h>
#include <net/ip.h>

#include <net/netfilter/nf_nat.h>

static int __net_init iptable_nat_table_init(struct net *net);

/**
 *  
 */
static const struct xt_table nf_nat_ipv4_table = {
	nf_nat_ipv4_table.name		= "nat",
	nf_nat_ipv4_table.valid_hooks	= (1 << NF_INET_PRE_ROUTING) |
                        			  (1 << NF_INET_POST_ROUTING) |
                        			  (1 << NF_INET_LOCAL_OUT) |
                        			  (1 << NF_INET_LOCAL_IN),
	nf_nat_ipv4_table.me		= THIS_MODULE,
	nf_nat_ipv4_table.af		= NFPROTO_IPV4,
	nf_nat_ipv4_table.table_init	= iptable_nat_table_init,
};

/**
 *  
 */
static unsigned int iptable_nat_do_chain(void *priv,
                        					 struct sk_buff *skb,
                        					 const struct nf_hook_state *state)
{
	return ipt_do_table(skb, state, state->net->ipv4.nat_table);
}


/**
 *  
 */
static const struct nf_hook_ops nf_nat_ipv4_ops[] = {
	{
		nf_nat_ipv4_ops[0].hook		= iptable_nat_do_chain,
		nf_nat_ipv4_ops[0].pf		= NFPROTO_IPV4,
		nf_nat_ipv4_ops[0].hooknum	= NF_INET_PRE_ROUTING,
		nf_nat_ipv4_ops[0].priority	= NF_IP_PRI_NAT_DST,
	},
	{
		nf_nat_ipv4_ops[1].hook		= iptable_nat_do_chain,
		nf_nat_ipv4_ops[1].pf		= NFPROTO_IPV4,
		nf_nat_ipv4_ops[1].hooknum	= NF_INET_POST_ROUTING,
		nf_nat_ipv4_ops[1].priority	= NF_IP_PRI_NAT_SRC,
	},
	{
		nf_nat_ipv4_ops[2].hook		= iptable_nat_do_chain,
		nf_nat_ipv4_ops[2].pf		= NFPROTO_IPV4,
		nf_nat_ipv4_ops[2].hooknum	= NF_INET_LOCAL_OUT,
		nf_nat_ipv4_ops[2].priority	= NF_IP_PRI_NAT_DST,
	},
	{
		nf_nat_ipv4_ops[3].hook		= iptable_nat_do_chain,
		nf_nat_ipv4_ops[3].pf		= NFPROTO_IPV4,
		nf_nat_ipv4_ops[3].hooknum	= NF_INET_LOCAL_IN,
		nf_nat_ipv4_ops[3].priority	= NF_IP_PRI_NAT_SRC,
	},
};

/**
 *  
 */
static int ipt_nat_register_lookups(struct net *net)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(nf_nat_ipv4_ops); i++) {
		ret = nf_nat_ipv4_register_fn(net, &nf_nat_ipv4_ops[i]);
		if (ret) {
			while (i)
				nf_nat_ipv4_unregister_fn(net, &nf_nat_ipv4_ops[--i]);

			return ret;
		}
	}

	return 0;
}

/**
 *  
 */
static void ipt_nat_unregister_lookups(struct net *rtoax_net)
{
	int i;

    /**
     *  
     */
	for (i = 0; i < ARRAY_SIZE(nf_nat_ipv4_ops); i++)
		nf_nat_ipv4_unregister_fn(rtoax_net, &nf_nat_ipv4_ops[i]);
}

/**
 *  初始化 iptables nat
 */
static int __net_init iptable_nat_table_init(struct net *rtoax_net)
{
	struct ipt_replace *repl;
	int ret;

	if (rtoax_net->ipv4.nat_table)
		return 0;

    /**
     *  初始化
     */
	repl = ipt_alloc_initial_table(&nf_nat_ipv4_table);
	if (repl == NULL)
		return -ENOMEM;

    /**
     *  
     */
	ret = ipt_register_table(rtoax_net, &nf_nat_ipv4_table, repl,
				            NULL, &rtoax_net->ipv4.nat_table);
	if (ret < 0) {
		kfree(repl);
		return ret;
	}

    /**
     *  
     */
	ret = ipt_nat_register_lookups(rtoax_net);
	if (ret < 0) {
		ipt_unregister_table(rtoax_net, rtoax_net->ipv4.nat_table, NULL);
		rtoax_net->ipv4.nat_table = NULL;
	}

	kfree(repl);
	return ret;
}

/**
 *  
 */
static void __net_exit iptable_nat_net_pre_exit(struct net *rtoax_net)
{
	if (rtoax_net->ipv4.nat_table)
		ipt_nat_unregister_lookups(rtoax_net);
}

static void __net_exit iptable_nat_net_exit(struct net *rtoax_net)
{
	if (!rtoax_net->ipv4.nat_table)
		return;
	ipt_unregister_table_exit(rtoax_net, rtoax_net->ipv4.nat_table);
	rtoax_net->ipv4.nat_table = NULL;
}

/**
 *  
 */
static struct pernet_operations iptable_nat_net_ops = {
	iptable_nat_net_ops.pre_exit = iptable_nat_net_pre_exit,
	iptable_nat_net_ops.exit	= iptable_nat_net_exit,
};

/**
 *  
 */
static int __init iptable_nat_init(void)
{
	int ret = register_pernet_subsys(&iptable_nat_net_ops);

	if (ret)
		return ret;

    /**
     *  
     */
	ret = iptable_nat_table_init(&init_net);
	if (ret)
		unregister_pernet_subsys(&iptable_nat_net_ops);
	return ret;
}

/**
 *  网络地址转换
 */
static void __exit iptable_nat_exit(void)
{
	unregister_pernet_subsys(&iptable_nat_net_ops);
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


module_init(iptable_nat_init);
module_exit(iptable_nat_exit);

MODULE_LICENSE("GPL");
