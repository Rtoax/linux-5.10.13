# SPDX-License-Identifier: GPL-2.0

# Prevent the noinstr section from being pestered by sanitizer and other goodies
# as long as these things cannot be disabled per function.
KASAN_SANITIZE := n
UBSAN_SANITIZE := n
KCOV_INSTRUMENT := n

# gcc/llvm 均支持全局canary:
#  gcc/llvm中编译选项-fstack-protector/-fstack-protector-strong均已支持,
#  开启后函数出入口会从全局变量__stack_chk_guard中获取全局canary
CFLAGS_REMOVE_common.o	 = -fstack-protector -fstack-protector-strong
CFLAGS_common.o		+= -fno-stack-protector

obj-$(CONFIG_GENERIC_ENTRY) 		+= common.o syscall_user_dispatch.o
obj-$(CONFIG_KVM_XFER_TO_GUEST_WORK)	+= kvm.o
