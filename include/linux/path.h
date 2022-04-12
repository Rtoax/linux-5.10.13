/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PATH_H
#define _LINUX_PATH_H

struct dentry;
struct vfsmount;

/**
 *
 */
struct path {   /* 路径 */
    /**
     *  mount 的子结构，表示实体所在目录树的安装方式
     */
	struct vfsmount *mnt;
    /**
     *  实体自身的 dentry 结构，表示实体本身
     */
	struct dentry *dentry;
} __randomize_layout;

extern void path_get(const struct path *);
extern void path_put(const struct path *);

static inline int path_equal(const struct path *path1, const struct path *path2)
{
	return path1->mnt == path2->mnt && path1->dentry == path2->dentry;
}

static inline void path_put_init(struct path *path)
{
	path_put(path);
	*path = (struct path) { };
}

#endif  /* _LINUX_PATH_H */
