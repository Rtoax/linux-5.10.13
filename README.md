Linux内核完全注释
=================

# Linux-5.10.13

> 注意：此代码不可编译

## 包含内容

* 注释掉了为空的函数
* 删除掉了"不相关"代码
* arch 只保留 `x86_64` 和 `arm64`
* 删除重声明的结构，如 `list_head`
* 删除 `BITS_PER_LONG == 32` 分支代码, 恒为假
* 保留 `BITS_PER_LONG == 64` 分支代码, 恒为真
* 删除 `__BIG_ENDIAN` 分支代码, 恒为假
* 保留 `__LITTLE_ENDIAN` 分支代码, 恒为真
* 删除或注释 `CONFIG_X86_32` 部分代码, 保留 `CONFIG_X86_64`
* 具体配置请参见 `config.h` 或 `config`


# 注释来源

1. 我的个人理解
2. 《奔跑吧，Linux内核》系列图书
3. 《深入理解Linux内核》
4. 《Linux设备驱动程序》
5. 《Linux内核网络协议》
6. 《深度探索Linux系统虚拟化》
7. 《基于名字空间的安全程序设计》
8. 《Linux二进制分析》
9. 《Qemu/KVM源码解析与应用》
10. https://github.com/rtoax/notes.git
11. 还有很多



## 配置

见 `config` 文件


# 原 README

Linux kernel
============

There are several guides for kernel developers and users. These guides can
be rendered in a number of formats, like HTML and PDF. Please read
Documentation/admin-guide/README.rst first.

In order to build the documentation, use ``make htmldocs`` or
``make pdfdocs``.  The formatted documentation can also be read online at:

    https://www.kernel.org/doc/html/latest/

There are various text files in the Documentation/ subdirectory,
several of them using the Restructured Text markup notation.

Please read the Documentation/process/changes.rst file, as it contains the
requirements for building and running the kernel, and information about
the problems which may result by upgrading your kernel.
