栈保护
=========


# 说明

* 在不考虑动态分配的情况下, 函数中使用的栈大小在编译阶段就已经确定了


# 编译选项

* CONFIG_STACKPROTECTOR

平台无关的编译选项,其决定是否开启 stack canary保护, 开启则默认指定编译选项 -fstack-protector，使用__stack_chk_guard 作为全局canary对比

* CONFIG_STACKPROTECTOR_STRONG

平台无关的编译选项,其决定是否开启strong保护,开启则额外指定编译选项 -fstack-protector-strong.

* CONFIG_STACKPROTECTOR_PER_TASK

平台相关的编译选项, 其决定是否开启内核per-task的stack canary保护(此时需编译器的per-cpu canary和对应硬件平台支持)


# stack canary简介

`stack canary`是一个比较久远的安全特性，linux内核在`2.6`版本便已经引入, 在`5.0`又引入了增强的`per-task stack canary`, 其原理比较简单,即:

每个函数执行前先向栈底插入一个canary值，以确保顺序的栈上溢在破坏到父函数栈帧前必须要先破坏canary。
每个函数返回时检测当前栈帧中的canary是否被修改,若被修改则代表发生了溢出(报错)

stack canary并不能检测到所有的栈溢出问题, 只有在满足:

* 攻击者不知当前插入当函数栈中canary的值(无infoleak)
* 攻击者只能顺序的覆盖栈中数据，无法跳过canary覆盖数据

函数入口需要向函数栈push一个原始的canary，函数出口需要将函数栈中的canary(后续称为stack_canary)和原始值做对比，在此过程中原始值需要保持不变并且可以被代码获取到:

* 默认stack canary使用全局符号(变量) `__stack_chk_guard` 作为原始的canary(后续称为全局canary), 在gcc/clang中均使用相同的名字.


# 编译器中全局canary的实现

这里以aarch64平台，gcc + -fstack-protector-strong为例,其实现逻辑如下(源码分析见备注2):

1. 函数入口将全局canary => stack_canary(stack_canary地址为编译期间预留在当前函数栈底的)
2. 函数出口对比全局canary和stack_canary是否还一致,一致则跳转到4)
3. 检测到栈溢出, 调用__stack_chk_fail函数
4. 函数返回


# 在aarch64内核中 per-task canary的思路可整理如下

1. 内核自身`sp_el0`记录`task_struct`(即current)地址，并随进程切换而切换
2. 在`task_struct`中增加一个成员`stack_canary`, 则此成员总是可以通过 (`sp_el0 + TSK_STACK_CANARY`)找到
3. 进程创建时总是为其生成一个新的canary记录到 `current->stack_canary`
4. 编译器开启`per-cpu canary`支持，基准的canary值总是来自`sp_el0 + TSK_STACK_CANARY`，也就是 `current->stack_canary`


# 参考连接

- https://blog.csdn.net/lidan113lidan/article/details/120318707
