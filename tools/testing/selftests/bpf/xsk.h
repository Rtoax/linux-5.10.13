/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

/*
 * AF_XDP user-space access library.
 *
 * Copyright (c) 2018 - 2019 Intel Corporation.
 * Copyright (c) 2019 Facebook
 *
 * Author(s): Magnus Karlsson <magnus.karlsson@intel.com>
 */

#ifndef __XSK_H
#define __XSK_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/if_xdp.h>

#include <bpf/libbpf.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This whole API has been deprecated and moved to libxdp that can be found at
 * https://github.com/xdp-project/xdp-tools. The APIs are exactly the same so
 * it should just be linking with libxdp instead of libbpf for this set of
 * functionality. If not, please submit a bug report on the aforementioned page.
 */

/* Load-Acquire Store-Release barriers used by the XDP socket
 * library. The following macros should *NOT* be considered part of
 * the xsk.h API, and is subject to change anytime.
 *
 * LIBRARY INTERNAL
 */

#define __XSK_READ_ONCE(x) (*(volatile typeof(x) *)&x)
#define __XSK_WRITE_ONCE(x, v) (*(volatile typeof(x) *)&x) = (v)

#if defined(__i386__) || defined(__x86_64__)
# define libbpf_smp_store_release(p, v)					\
	do {								\
		asm volatile("" : : : "memory");			\
		__XSK_WRITE_ONCE(*p, v);				\
	} while (0)
# define libbpf_smp_load_acquire(p)					\
	({								\
		typeof(*p) ___p1 = __XSK_READ_ONCE(*p);			\
		asm volatile("" : : : "memory");			\
		___p1;							\
	})
#elif defined(__aarch64__)
# define libbpf_smp_store_release(p, v)					\
		asm volatile ("stlr %w1, %0" : "=Q" (*p) : "r" (v) : "memory")
# define libbpf_smp_load_acquire(p)					\
	({								\
		typeof(*p) ___p1;					\
		asm volatile ("ldar %w0, %1"				\
			      : "=r" (___p1) : "Q" (*p) : "memory");	\
		___p1;							\
	})
#elif defined(__riscv)
# define libbpf_smp_store_release(p, v)					\
	do {								\
		asm volatile ("fence rw,w" : : : "memory");		\
		__XSK_WRITE_ONCE(*p, v);				\
	} while (0)
# define libbpf_smp_load_acquire(p)					\
	({								\
		typeof(*p) ___p1 = __XSK_READ_ONCE(*p);			\
		asm volatile ("fence r,rw" : : : "memory");		\
		___p1;							\
	})
#endif

#ifndef libbpf_smp_store_release
#define libbpf_smp_store_release(p, v)					\
	do {								\
		__sync_synchronize();					\
		__XSK_WRITE_ONCE(*p, v);				\
	} while (0)
#endif

#ifndef libbpf_smp_load_acquire
#define libbpf_smp_load_acquire(p)					\
	({								\
		typeof(*p) ___p1 = __XSK_READ_ONCE(*p);			\
		__sync_synchronize();					\
		___p1;							\
	})
#endif

/* LIBRARY INTERNAL -- END */

/* Do not access these members directly. Use the functions below. */
#define DEFINE_XSK_RING(name) \
struct name { \
	__u32 cached_prod; \
	__u32 cached_cons; \
	__u32 mask; \
	__u32 size; \
	__u32 *producer; \
	__u32 *consumer; \
	void *ring; \
	__u32 *flags; \
}

DEFINE_XSK_RING(xsk_ring_prod);
DEFINE_XSK_RING(xsk_ring_cons);

/* For a detailed explanation on the memory barriers associated with the
 * ring, please take a look at net/xdp/xsk_queue.h.
 */

struct xsk_umem;
struct xsk_socket;

/**
 * Use this function to get a pointer to a slot in the fill ring to set the
 * address of a packet buffer.
 *
 * https://docs.ebpf.io/ebpf-library/libxdp/functions/xsk_ring_prod__fill_addr/
 */
static inline __u64 *xsk_ring_prod__fill_addr(struct xsk_ring_prod *fill,
					      __u32 idx)
{
	__u64 *addrs = (__u64 *)fill->ring;

	return &addrs[idx & fill->mask];
}

/**
 * This function is to read the address of a specific entry in the consumer ring.
 *
 * https://docs.ebpf.io/ebpf-library/libxdp/functions/xsk_ring_cons__comp_addr/
 */
static inline const __u64 *
xsk_ring_cons__comp_addr(const struct xsk_ring_cons *comp, __u32 idx)
{
	const __u64 *addrs = (const __u64 *)comp->ring;

	return &addrs[idx & comp->mask];
}

/**
 * This function allow to access a specific transmit descriptor in the TX ring.
 *
 * https://docs.ebpf.io/ebpf-library/libxdp/functions/xsk_ring_prod__tx_desc/
 */
static inline struct xdp_desc *xsk_ring_prod__tx_desc(struct xsk_ring_prod *tx,
						      __u32 idx)
{
	struct xdp_desc *descs = (struct xdp_desc *)tx->ring;

	return &descs[idx & tx->mask];
}

/**
 * This function is used to retrieve(取回) the receive descriptor at a specific
 * index in the Rx ring.
 *
 * https://docs.ebpf.io/ebpf-library/libxdp/functions/xsk_ring_cons__rx_desc/
 */
static inline const struct xdp_desc *
xsk_ring_cons__rx_desc(const struct xsk_ring_cons *rx, __u32 idx)
{
	const struct xdp_desc *descs = (const struct xdp_desc *)rx->ring;

	return &descs[idx & rx->mask];
}

/**
 * This function function checks if the kernel needs to be woken up to process
 * the producer ring.
 *
 * https://docs.ebpf.io/ebpf-library/libxdp/functions/xsk_ring_prod__needs_wakeup/
 */
static inline int xsk_ring_prod__needs_wakeup(const struct xsk_ring_prod *r)
{
	return *r->flags & XDP_RING_NEED_WAKEUP;
}

static inline __u32 xsk_prod_nb_free(struct xsk_ring_prod *r, __u32 nb)
{
	__u32 free_entries = r->cached_cons - r->cached_prod;

	if (free_entries >= nb)
		return free_entries;

	/* Refresh the local tail pointer.
	 * cached_cons is r->size bigger than the real consumer pointer so
	 * that this addition can be avoided in the more frequently
	 * executed code that computs free_entries in the beginning of
	 * this function. Without this optimization it whould have been
	 * free_entries = r->cached_prod - r->cached_cons + r->size.
	 */
	r->cached_cons = libbpf_smp_load_acquire(r->consumer);
	r->cached_cons += r->size;

	return r->cached_cons - r->cached_prod;
}

/**
 * called in
 * xsk_ring_cons__peek()
 */
static inline __u32 xsk_cons_nb_avail(struct xsk_ring_cons *r, __u32 nb)
{
	/**
	 * number = producer - consumer
	 */
	__u32 entries = r->cached_prod - r->cached_cons;

	if (entries == 0) {
		r->cached_prod = libbpf_smp_load_acquire(r->producer);
		entries = r->cached_prod - r->cached_cons;
	}

	return (entries > nb) ? nb : entries;
}

/**
 * Reserve one or more slots in a producer ring.
 *
 * https://docs.ebpf.io/ebpf-library/libxdp/functions/xsk_ring_prod__reserve/
 */
static inline __u32 xsk_ring_prod__reserve(struct xsk_ring_prod *prod, __u32 nb,
					   __u32 *idx)
{
	if (xsk_prod_nb_free(prod, nb) < nb)
		return 0;

	*idx = prod->cached_prod;
	prod->cached_prod += nb;

	return nb;
}

/**
 * Submit the filled slots so the kernel can process them.
 *
 * https://docs.ebpf.io/ebpf-library/libxdp/functions/xsk_ring_prod__submit/
 */
static inline void xsk_ring_prod__submit(struct xsk_ring_prod *prod, __u32 nb)
{
	/* Make sure everything has been written to the ring before indicating
	 * this to the kernel by writing the producer pointer.
	 */
	libbpf_smp_store_release(prod->producer, *prod->producer + nb);
}

/**
 * Check for new packets in the ring.
 *
 * __u32 returns the number of packets that are available in the consumer ring (idx).
 *
 * It can be less than or equal to the number of packets requested to peek.
 */
/* peek 窥视 */
static inline __u32 xsk_ring_cons__peek(struct xsk_ring_cons *cons, __u32 nb,
					__u32 *idx)
{
	__u32 entries = xsk_cons_nb_avail(cons, nb);

	if (entries > 0) {
		*idx = cons->cached_cons;
		cons->cached_cons += entries;
	}

	return entries;
}

/**
 * Cancel the reservation of one or more slots in a consumer ring.
 */
static inline void xsk_ring_cons__cancel(struct xsk_ring_cons *cons, __u32 nb)
{
	cons->cached_cons -= nb;
}

/**
 * This function releases a specified number of packets that have been processed
 * from the consumer ring back to the kernel. Indicates to the kernel that these
 * packets have been consumed and the buffers can be reused for new incoming
 * packets.
 *
 * https://docs.ebpf.io/ebpf-library/libxdp/functions/xsk_ring_cons__release/
 */
static inline void xsk_ring_cons__release(struct xsk_ring_cons *cons, __u32 nb)
{
	/* Make sure data has been read before indicating we are done
	 * with the entries by updating the consumer pointer.
	 */
	libbpf_smp_store_release(cons->consumer, *cons->consumer + nb);

}

static inline void *xsk_umem__get_data(void *umem_area, __u64 addr)
{
	return &((char *)umem_area)[addr];
}

/**
 * This function extract the memory address in unaligned mode.
 */
static inline __u64 xsk_umem__extract_addr(__u64 addr)
{
	return addr & XSK_UNALIGNED_BUF_ADDR_MASK;
}

static inline __u64 xsk_umem__extract_offset(__u64 addr)
{
	return addr >> XSK_UNALIGNED_BUF_OFFSET_SHIFT;
}

static inline __u64 xsk_umem__add_offset_to_addr(__u64 addr)
{
	return xsk_umem__extract_addr(addr) + xsk_umem__extract_offset(addr);
}

int xsk_umem__fd(const struct xsk_umem *umem);
int xsk_socket__fd(const struct xsk_socket *xsk);

#define XSK_RING_CONS__DEFAULT_NUM_DESCS      2048
#define XSK_RING_PROD__DEFAULT_NUM_DESCS      2048
#define XSK_UMEM__DEFAULT_FRAME_SHIFT    12 /* 4096 bytes */
#define XSK_UMEM__DEFAULT_FRAME_SIZE     (1 << XSK_UMEM__DEFAULT_FRAME_SHIFT)
#define XSK_UMEM__DEFAULT_FRAME_HEADROOM 0
#define XSK_UMEM__DEFAULT_FLAGS 0

struct xsk_umem_config {
	__u32 fill_size;
	__u32 comp_size;
	__u32 frame_size;
	__u32 frame_headroom;
	__u32 flags;
};

int xsk_setup_xdp_prog_xsk(struct xsk_socket *xsk, int *xsks_map_fd);
int xsk_setup_xdp_prog(int ifindex, int *xsks_map_fd);
int xsk_socket__update_xskmap(struct xsk_socket *xsk, int xsks_map_fd);

/* Flags for the libbpf_flags field. */
#define XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD (1 << 0)

struct xsk_socket_config {
	__u32 rx_size;
	__u32 tx_size;
	__u32 libbpf_flags;
	__u32 xdp_flags;
	__u16 bind_flags;
};

/* Set config to NULL to get the default configuration. */
int xsk_umem__create(struct xsk_umem **umem,
		     void *umem_area, __u64 size,
		     struct xsk_ring_prod *fill,
		     struct xsk_ring_cons *comp,
		     const struct xsk_umem_config *config);
int xsk_socket__create(struct xsk_socket **xsk,
		       const char *ifname, __u32 queue_id,
		       struct xsk_umem *umem,
		       struct xsk_ring_cons *rx,
		       struct xsk_ring_prod *tx,
		       const struct xsk_socket_config *config);
int xsk_socket__create_shared(struct xsk_socket **xsk_ptr,
			      const char *ifname,
			      __u32 queue_id, struct xsk_umem *umem,
			      struct xsk_ring_cons *rx,
			      struct xsk_ring_prod *tx,
			      struct xsk_ring_prod *fill,
			      struct xsk_ring_cons *comp,
			      const struct xsk_socket_config *config);

/* Returns 0 for success and -EBUSY if the umem is still in use. */
int xsk_umem__delete(struct xsk_umem *umem);
void xsk_socket__delete(struct xsk_socket *xsk);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __XSK_H */
