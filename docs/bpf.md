bpf
===


# workflow

The typical workflow is that BPF programs are written in C, compiled by LLVM into object / ELF files, which are parsed by user space BPF ELF loaders (such as iproute2 or others), and pushed into the kernel through the BPF system call. The kernel verifies the BPF instructions and JITs them, returning a new file descriptor for the program, which then can be attached to a subsystem (e.g. networking). If supported, the subsystem could then further offload the BPF program to hardware (e.g. NIC).

```c
#include <linux/bpf.h>

#ifndef __section
# define __section(NAME)                  \
   __attribute__((section(NAME), used))
#endif

__section("prog")
int xdp_drop(struct xdp_md *ctx)
{
    return XDP_DROP;
}

char __license[] __section("license") = "GPL";
```

It can then be compiled and loaded into the kernel as follows:

```
$ clang -O2 -Wall -target bpf -c xdp-example.c -o xdp-example.o
$ sudo ip link set dev em1 xdp obj xdp-example.o
```


# Refs

* [https://docs.cilium.io/en/stable/bpf/](https://docs.cilium.io/en/stable/bpf/)


