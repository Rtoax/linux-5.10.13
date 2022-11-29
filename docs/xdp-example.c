/**
 * https://docs.cilium.io/en/stable/bpf/
 *
 * 1.
 * $ clang -O2 -Wall -target bpf -c xdp-example.c -o xdp-example.o
 * $ sudo ip link set dev em1 xdp obj xdp-example.o
 * $ llvm-objdump -S --no-show-raw-insn xdp-example.o
 * $ llvm-objdump -S xdp-example.o
 * $
 * $ sudo ip link set dev em1 xdp obj xdp-example.o verb
 *
 * 2.
 * $ clang -O2 -S -Wall -target bpf -c xdp-example.c -o xdp-example.S
 * $ cat xdp-example.S
 * $ llvm-mc -triple bpf -filetype=obj -o xdp-example.o xdp-example.S # LLVM 6.0
 *
 * 3.
 * $ clang -O2 -Wall -target bpf -emit-llvm -c xdp-example.c -o xdp-example.bc
 * $ llc xdp-example.bc -march=bpf -filetype=obj -o xdp-example.o
 * $
 * $ clang -O2 -Wall -emit-llvm -S -c xdp-example.c -o -
 *
 * 4. DWARF, pahole
 * $ llc -march=bpf -mattr=help |& grep dwarfris
 * $ clang -O2 -g -Wall -target bpf -emit-llvm -c xdp-example.c -o xdp-example.bc
 * $ llc xdp-example.bc -march=bpf -mattr=dwarfris -filetype=obj -o xdp-example.o
 * $ clang -target bpf -O2 -g -c -Xclang -target-feature -Xclang +dwarfris -c xdp-example.c -o xdp-example.o
 * $ pahole xdp-example.o
 *
 * 5.
 * $ clang -target bpf -O2 -Wall -g -c -Xclang -target-feature -Xclang +dwarfris -c xdp-example.c -o xdp-example.o
 * $ pahole -J xdp-example.o
 * $ readelf -a xdp-example.o | grep BTF
 */
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
