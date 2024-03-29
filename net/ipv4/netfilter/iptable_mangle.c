// SPDX-License-Identifier: GPL-2.0-only
/*
 * This is the 1999 rewrite of IP Firewalling, aiming for kernel 2.3.x.
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 * Copyright (C) 2000-2004 Netfilter Core Team <coreteam@netfilter.org>
 */
#include <linux/module.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/route.h>
#include <linux/ip.h>
#include <net/ip.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("iptables mangle table");

/**
 *  mangle表的主要功能是根据规则修改数据包的一些标志位，
 *  以便其他规则或程序可以利用这种标志对数据包进行过滤或策略路由。
 */
#define MANGLE_VALID_HOOKS ((1 << NF_INET_PRE_ROUTING) | \
            			    (1 << NF_INET_LOCAL_IN) | \
            			    (1 << NF_INET_FORWARD) | \
            			    (1 << NF_INET_LOCAL_OUT) | \
            			    (1 << NF_INET_POST_ROUTING))

static int __net_init iptable_mangle_table_init(struct net *net);

/**
 *  
 */
static const struct xt_table packet_mangler = {
	packet_mangler.name		= "mangle",
	packet_mangler.valid_hooks	= MANGLE_VALID_HOOKS,
	packet_mangler.me		= THIS_MODULE,
	packet_mangler.af		= NFPROTO_IPV4,
	packet_mangler.priority	= NF_IP_PRI_MANGLE,
	packet_mangler.table_init	= iptable_mangle_table_init,
};

static unsigned int
ipt_mangle_out(struct sk_buff *skb, const struct nf_hook_state *state)
{
	unsigned int ret;
	const struct iphdr *iph;
	u_int8_t tos;
	__be32 saddr, daddr;
	u_int32_t mark;
	int err;

	/* Save things which could affect route */
	mark = skb->mark;
	iph = ip_hdr(skb);
	saddr = iph->saddr;
	daddr = iph->daddr;
	tos = iph->tos;

	ret = ipt_do_table(skb, state, state->net->ipv4.iptable_mangle);
	/* Reroute for ANY change. */
	if (ret != NF_DROP && ret != NF_STOLEN) {
		iph = ip_hdr(skb);

		if (iph->saddr != saddr ||
		    iph->daddr != daddr ||
		    skb->mark != mark ||
		    iph->tos != tos) {
			err = ip_route_me_harder(state->net, state->sk, skb, RTN_UNSPEC);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
	}

	return ret;
}

/* The work comes in here from netfilter.c. */
static unsigned int
iptable_mangle_hook(void *priv,
		     struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	if (state->hook == NF_INET_LOCAL_OUT)
		return ipt_mangle_out(skb, state);
	return ipt_do_table(skb, state, state->net->ipv4.iptable_mangle);
}

/**
 *  
 */
static struct nf_hook_ops __read_mostly *mangle_ops ;
static int __net_init iptable_mangle_table_init(struct net *net)
{
	struct ipt_replace *repl;
	int ret;

	if (net->ipv4.iptable_mangle)
		return 0;

    /**
     *  
     */
	repl = ipt_alloc_initial_table(&packet_mangler);
	if (repl == NULL)
		return -ENOMEM;

    /**
     *  
     */
	ret = ipt_register_table(net, &packet_mangler, repl, mangle_ops,
				                &net->ipv4.iptable_mangle);
	kfree(repl);
	return ret;
}

/**
 *  
 */
static void __net_exit iptable_mangle_net_pre_exit(struct net *net)
{
	if (net->ipv4.iptable_mangle)
		ipt_unregister_table_pre_exit(net, net->ipv4.iptable_mangle, mangle_ops);
}

/**
 *  
 */
static void __net_exit iptable_mangle_net_exit(struct net *net)
{
	if (!net->ipv4.iptable_mangle)
		return;
	ipt_unregister_table_exit(net, net->ipv4.iptable_mangle);
	net->ipv4.iptable_mangle = NULL;
}

/**
 *  
 */
static struct pernet_operations iptable_mangle_net_ops = {
	iptable_mangle_net_ops.pre_exit = iptable_mangle_net_pre_exit,
	iptable_mangle_net_ops.exit = iptable_mangle_net_exit,
};

/**
 *  mangle表的主要功能是根据规则修改数据包的一些标志位，
 *  以便其他规则或程序可以利用这种标志对数据包进行过滤或策略路由。
 */
static int __init iptable_mangle_init(void)
{
	int ret;

	mangle_ops = xt_hook_ops_alloc(&packet_mangler, iptable_mangle_hook);
	if (IS_ERR(mangle_ops)) {
		ret = PTR_ERR(mangle_ops);
		return ret;
	}

	ret = register_pernet_subsys(&iptable_mangle_net_ops);
	if (ret < 0) {
		kfree(mangle_ops);
		return ret;
	}

	ret = iptable_mangle_table_init(&init_net);
	if (ret) {
		unregister_pernet_subsys(&iptable_mangle_net_ops);
		kfree(mangle_ops);
	}

	return ret;
}

/**
 *  mangle表的主要功能是根据规则修改数据包的一些标志位，
 *  以便其他规则或程序可以利用这种标志对数据包进行过滤或策略路由。
 */
static void __exit iptable_mangle_fini(void)
{
	unregister_pernet_subsys(&iptable_mangle_net_ops);
	kfree(mangle_ops);
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


module_init(iptable_mangle_init);
module_exit(iptable_mangle_fini);
