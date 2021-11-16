/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NS_COMMON_H
#define _LINUX_NS_COMMON_H

struct proc_ns_operations;
/**
 *  名字空间超类
 */
struct ns_common {  /* namespace 公共信息 */
	atomic_long_t stashed;
    /**
     *  
     */
	const struct proc_ns_operations *ops;
    /**
     *  namespace 的 编号
     */
	unsigned int inum;
};
typedef struct ns_common *ns_common_t; //+++

#endif
