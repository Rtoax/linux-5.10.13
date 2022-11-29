/**
 * https://docs.cilium.io/en/stable/bpf/
 *
 * $ clang -O2 -Wall -target bpf -c tc-example.c -o tc-example.o
 * $
 * $ sudo tc qdisc add dev em1 clsact
 * $ sudo tc filter add dev em1 ingress bpf da obj tc-example.o sec ingress
 * $ sudo tc filter add dev em1 egress bpf da obj tc-example.o sec egress
 * $
 * $ tc filter show dev em1 ingress
 * filter protocol all pref 49152 bpf
 * filter protocol all pref 49152 bpf handle 0x1 tc-example.o:[ingress] direct-action id 1 tag c5f7825e5dac396f
 * $
 * $ sudo tc filter show dev em1 egress
 * filter protocol all pref 49152 bpf
 * filter protocol all pref 49152 bpf handle 0x1 tc-example.o:[egress] direct-action id 2 tag b2fd5adc0f262714
 *
 * $ mount | grep bpf
 * $ sysfs on /sys/fs/bpf type sysfs (rw,nosuid,nodev,noexec,relatime,seclabel)
 * bpf on /sys/fs/bpf type bpf (rw,relatime,mode=0700)
 *
 * $ tree /sys/fs/bpf/
 * /sys/fs/bpf/
 * +-- ip -> /sys/fs/bpf/tc/
 * +-- tc
 * |   +-- globals
 * |       +-- acc_map
 * +-- xdp -> /sys/fs/bpf/tc/
 */
#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <stdint.h>
#include <iproute2/bpf_elf.h>

#ifndef __section
# define __section(NAME)                  \
   __attribute__((section(NAME), used))
#endif

#ifndef __inline
# define __inline                         \
   inline __attribute__((always_inline))
#endif

#ifndef lock_xadd
# define lock_xadd(ptr, val)              \
   ((void)__sync_fetch_and_add(ptr, val))
#endif

#ifndef BPF_FUNC
# define BPF_FUNC(NAME, ...)              \
   (*NAME)(__VA_ARGS__) = (void *)BPF_FUNC_##NAME
#endif

static void *BPF_FUNC(map_lookup_elem, void *map, const void *key);

struct bpf_elf_map acc_map __section("maps") = {
    .type           = BPF_MAP_TYPE_ARRAY,
    .size_key       = sizeof(uint32_t),
    .size_value     = sizeof(uint32_t),
    .pinning        = PIN_GLOBAL_NS,
    .max_elem       = 2,
};

static __inline int account_data(struct __sk_buff *skb, uint32_t dir)
{
    uint32_t *bytes;

    bytes = map_lookup_elem(&acc_map, &dir);
    if (bytes)
            lock_xadd(bytes, skb->len);

    return TC_ACT_OK;
}

__section("ingress")
int tc_ingress(struct __sk_buff *skb)
{
    return account_data(skb, 0);
}

__section("egress")
int tc_egress(struct __sk_buff *skb)
{
    return account_data(skb, 1);
}

char __license[] __section("license") = "GPL";

