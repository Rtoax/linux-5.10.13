BPF JIT
=======


64 位 `x86_64`、arm64、ppc64、s390x、mips64、sparc64 和 32 位 arm `x86_32` 都附带了内核内 eBPF JIT 编译器，它们都是功能等效的，可以通过以下方式启用：

```
# echo 1 > /proc/sys/net/core/bpf_jit_enable
```

32位mips，ppc和sparc架构目前有一个cBPF JIT编译器。上述架构仍然具有cBPF JIT，以及Linux内核支持的所有其余架构，这些架构根本没有BPF JIT编译器，需要通过内核内解释器运行eBPF程序。

在内核的源代码树中，可以通过为 `HAVE_EBPF_JIT` 发出 grep 来轻松确定 eBPF JIT 支持：

```
# git grep HAVE_EBPF_JIT arch/
arch/arm/Kconfig:       select HAVE_EBPF_JIT   if !CPU_ENDIAN_BE32
arch/arm64/Kconfig:     select HAVE_EBPF_JIT
arch/powerpc/Kconfig:   select HAVE_EBPF_JIT   if PPC64
arch/mips/Kconfig:      select HAVE_EBPF_JIT   if (64BIT && !CPU_MICROMIPS)
arch/s390/Kconfig:      select HAVE_EBPF_JIT   if PACK_STACK && HAVE_MARCH_Z196_FEATURES
arch/sparc/Kconfig:     select HAVE_EBPF_JIT   if SPARC64
arch/x86/Kconfig:       select HAVE_EBPF_JIT   if X86_64
```

JIT 编译器显著加快了 BPF 程序的执行速度，因为与解释器相比，它们降低了每条指令的成本。通常，指令可以与底层体系结构的本机指令 1：1 映射。这也减少了生成的可执行映像大小，因此对 CPU 的指令缓存更加友好。特别是在 CISC 指令集（如 x86）的情况下，JIT 经过优化，可为给定指令发出尽可能短的操作码，以缩小程序转换所需的总大小。


