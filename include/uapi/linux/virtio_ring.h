#ifndef _UAPI_LINUX_VIRTIO_RING_H
#define _UAPI_LINUX_VIRTIO_RING_H
/* An interface for efficient virtio implementation, currently for use by KVM,
 * but hopefully others soon.  Do NOT change this since it will
 * break existing servers and clients.
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright Rusty Russell IBM Corporation 2007. */
#ifndef __KERNEL__
#include <stdint.h>
#endif
#include <linux/types.h>
#include <linux/virtio_types.h>

/**
 *  struct vring_desc.flags
 */
/* This marks a buffer as continuing via the next field. */
#define VRING_DESC_F_NEXT	1
/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE	2
/* This means the buffer contains a list of buffer descriptors. */
#define VRING_DESC_F_INDIRECT	4

/*
 * Mark a descriptor as available or used in packed ring.
 * Notice: they are defined as shifts instead of shifted values.
 */
#define VRING_PACKED_DESC_F_AVAIL	7
#define VRING_PACKED_DESC_F_USED	15

/* The Host uses this in used->flags to advise the Guest: don't kick me when
 * you add a buffer.  It's unreliable, so it's simply an optimization.  Guest
 * will still kick if it's out of buffers. */
#define VRING_USED_F_NO_NOTIFY	1
/* The Guest uses this in avail->flags to advise the Host: don't interrupt me
 * when you consume a buffer.  It's unreliable, so it's simply an
 * optimization.  */
#define VRING_AVAIL_F_NO_INTERRUPT	1

/* Enable events in packed ring. */
#define VRING_PACKED_EVENT_FLAG_ENABLE	0x0
/* Disable events in packed ring. */
#define VRING_PACKED_EVENT_FLAG_DISABLE	0x1
/*
 * Enable events for a specific descriptor in packed ring.
 * (as specified by Descriptor Ring Change Event Offset/Wrap Counter).
 * Only valid if VIRTIO_RING_F_EVENT_IDX has been negotiated.
 */
#define VRING_PACKED_EVENT_FLAG_DESC	0x2

/*
 * Wrap counter bit shift in event suppression structure
 * of packed ring.
 */
#define VRING_PACKED_EVENT_F_WRAP_CTR	15

/* We support indirect buffer descriptors */
#define VIRTIO_RING_F_INDIRECT_DESC	28

/* The Guest publishes the used index for which it expects an interrupt
 * at the end of the avail ring. Host should ignore the avail->flags field. */
/* The Host publishes the avail index for which it expects a kick
 * at the end of the used ring. Guest should ignore the used->flags field. */
#define VIRTIO_RING_F_EVENT_IDX		29

/* Alignment requirements for vring elements.
 * When using pre-virtio 1.0 layout, these fall out naturally.
 */
#define VRING_AVAIL_ALIGN_SIZE 2
#define VRING_USED_ALIGN_SIZE 4
#define VRING_DESC_ALIGN_SIZE 16

/* Virtio ring descriptors: 16 bytes.  These can chain together via "next". */
/**
 * 传统的纯模拟设备在工作的时候，会触发频繁的陷入陷出， 而且IO请求的内容要进行多次拷贝传递，
 * 严重影响了设备的IO性能。 virtio为了提升设备的IO性能，采用了共享内存机制， 前端驱动会提前
 * 申请好一段物理地址空间用来存放IO请求，然后将这段地址的GPA告诉QEMU。 前端驱动在下发IO请求
 * 后，QEMU可以直接从共享内存中取出请求，然后将完成后的结果又直接写到虚拟机对应地址上去。 整
 * 个过程中可以做到直投直取，省去了不必要的数据拷贝开销。
 *
 * Virtqueue是整个virtio方案的灵魂所在。每个virtqueue都包含3张表， Descriptor Table存
 * 放了IO请求描述符，Available Ring记录了当前哪些描述符是可用的， Used Ring记录了哪些描述
 * 符已经被后端使用了。
 *
 * 每个virtqueue由3个部分组成：
 * +-------------------+--------------------------------+-----------------------+
 * | Descriptor Table  |   Available Ring  (padding)    |       Used Ring       |
 * +-------------------+--------------------------------+-----------------------+
 *
 * - Descriptor Table：存放IO传输请求信息；
 * - Available Ring：记录了Descriptor Table表中的I/O请求下发信息，前端Driver可写后端只读；
 * - Used Ring：记录Descriptor Table表中已被提交到硬件的信息，前端Driver只读后端可写。
 *
 *
 *          +------------------------------------+
 *          |       virtio  guest driver         |
 *          +-----------------+------------------+
 *            /               |              ^
 *           /                |               \
 *          put            update             get
 *         /                  |                 \
 *        V                   V                  \
 *   +----------+      +------------+        +----------+
 *   |          |      |            |        |          |
 *   +----------+      +------------+        +----------+
 *   | available|      | descriptor |        |   used   |
 *   |   ring   |      |   table    |        |   ring   |
 *   +----------+      +------------+        +----------+
 *   |          |      |            |        |          |
 *   +----------+      +------------+        +----------+
 *   |          |      |            |        |          |
 *   +----------+      +------------+        +----------+
 *        \                   ^                   ^
 *         \                  |                  /
 *         get             update              put
 *           \                |                /
 *            V               |               /
 *           +----------------+-------------------+
 *           |       virtio host backend          |
 *           +------------------------------------+
 *
 * https://kernelgo.org/virtio-overview.html
 */
/**
 * Descriptor Table：存放IO传输请求信息,是一个一个的virtq_desc元素，每个virq_desc元素
 * 占用16个字节。
 *
 * +-----------------------------------------------------------+
 * |                        addr/gpa [0:63]                    |
 * +-------------------------+-----------------+---------------+
 * |         len [0:31]      |  flags [0:15]   |  next [0:15]  |
 * +-------------------------+-----------------+---------------+
 */
struct vring_desc {
	/**
	 *  指向存储数据的存储块的首地址
	 *  填充时，需要将 GVA 转化为 GPA
	 */
	/**
	 * Address (guest-physical).
	 * addr占用64bit存放了单个IO请求的GPA地址信息，例如addr可能表示某个DMA buffer的起
	 * 始地址。
	 *
	 * struct vring_desc.addr = GPA
	 *
	 * 该字段可能为
	 * struct virtio_blk_outhdr {}
	 */
	__virtio64 addr;
	/**
	 * Length. len占用32bit表示IO请求的长度
	 */
	__virtio32 len;

	/**
	 *  属性，如 `VRING_DESC_F_WRITE`
	 */
	/**
	 * The flags as indicated above.
	 * flags的取值有3种，
	 * 1. VRING_DESC_F_NEXT 表示这个IO请求和下一个virtq_desc描述的是连续的，
	 * 2. VRING_DESC_F_WRITE 表示这段buffer是write only的，
	 * 3. VRING_DESC_F_INDIRECT 表示这段buffer里面放的内容是另外一组buffer的
	 *    virtq_desc（相当于重定向）
	 */
	__virtio16 flags;

	/**
	 * 如果 flags 标志有 `VRING_DESC_F_NEXT`, 则该字段有效，指向下一个 virtq_desc
	 * 的索引号
	 */
	/* We chain unused descriptors via this, too */
	__virtio16 next;
};
/**
 * Available Ring：记录了Descriptor Table表中的I/O请求下发信息，前端Driver可写后端只读；
 *
 * 可用描述符区域，在 CPU 从 guest 切换到 host模式后，模拟设备将检查可用描述符区域，如果有
 * 可用的描述符，就一次进行消费。
 *
 * Available Ring是前端驱动用来告知后端那些IO buffer是的请求需要处理，每个Ring中包含一个
 * virtq_avail占用8个字节
 *
 * +--------------+-------------+--------------+---------------------+
 * | flags [0:15] |  idx [0:15] |  ring[0:15]  |  used_event [0:15]  |
 * +--------------+-------------+--------------+---------------------+
 */
struct vring_avail {
	/**
	 * flags取值：
	 * VRING_AVAIL_F_NO_INTERRUPT 表示前端驱动告诉后端：“当你消耗完一个IO buffer的
	 *                            时候，不要立刻给我发中断”（防止中断过多影响效率）。
	 */
	__virtio16 flags;
	/**
	 *  记录 ring 中的位置，表示下次前端驱动要放置Descriptor Entry的地方。
	 */
	__virtio16 idx;
	/**
	 *  驱动每次将IO request 转换为一个可用的 描述符链后，
	 *  就会向 数组 ring 中 追加一个元素
	 *  最初，ring 是空的
	 */
	__virtio16 ring[];
};

/* u32 is used here for ids for padding reasons. */
struct vring_used_elem {
	/* Index of start of used descriptor chain. */
	__virtio32 id;
	/* Total length of the descriptor chain which was used (written to) */
	__virtio32 len;
};

typedef struct vring_used_elem __attribute__((aligned(VRING_USED_ALIGN_SIZE)))
	vring_used_elem_t;

/**
 * Used Ring：记录Descriptor Table表中已被提交到硬件的信息，前端Driver只读后端可写。
 *
 * 已用描述符，设备将已经处理的描述符记录起来，反馈给驱动，这个“已用”也是相对于驱动而言的。
 */
struct vring_used {
	/**
	 * flags的值如果为 VIRTIO_RING_F_EVENT_IDX，并且前后端协商 VIRTIO_RING_F_EVENT_IDX
	 * feature 成功。那么 Guest 会将 used ring index 放在 available ring 的末尾，
	 * 告诉后端说： “Hi 小老弟，当你处理完这个请求的时候，给我发个中断通知我一下”， 同时 host
	 * 也会将 avail_event index 放到 used ring 的末尾，告诉guest说： “Hi 老兄，记得把
	 * 这个 idx 的请求 kick 给我哈”。VIRTIO_RING_F_EVENT_IDX 对virtio通知/中断有一定的
	 * 优化，在某些场景下能够提升IO性能。
	 */
	__virtio16 flags;
	__virtio16 idx;
	vring_used_elem_t ring[];
};

/*
 * The ring element addresses are passed between components with different
 * alignments assumptions. Thus, we might need to decrease the compiler-selected
 * alignment, and so must use a typedef to make sure the aligned attribute
 * actually takes hold:
 *
 * https://gcc.gnu.org/onlinedocs//gcc/Common-Type-Attributes.html#Common-Type-Attributes
 *
 * When used on a struct, or struct member, the aligned attribute can only
 * increase the alignment; in order to decrease it, the packed attribute must
 * be specified as well. When used as part of a typedef, the aligned attribute
 * can both increase and decrease alignment, and specifying the packed
 * attribute generates a warning.
 */
typedef struct vring_desc __attribute__((aligned(VRING_DESC_ALIGN_SIZE)))
	vring_desc_t;
typedef struct vring_avail __attribute__((aligned(VRING_AVAIL_ALIGN_SIZE)))
	vring_avail_t;
typedef struct vring_used __attribute__((aligned(VRING_USED_ALIGN_SIZE)))
	vring_used_t;

/**
 * 每个virtqueue由3个部分组成：
 * +-------------------+--------------------------------+-----------------------+
 * | Descriptor Table  |   Available Ring  (padding)    |       Used Ring       |
 * +-------------------+--------------------------------+-----------------------+
 *
 * - Descriptor Table：存放IO传输请求信息；
 * - Available Ring：记录了Descriptor Table表中的I/O请求下发信息，前端Driver可写后端只读；
 * - Used Ring：记录Descriptor Table表中已被提交到硬件的信息，前端Driver只读后端可写。
 *
 *          +------------------------------------+
 *          |       virtio  guest driver         |
 *          +-----------------+------------------+
 *            /               |              ^
 *           /                |               \
 *          put            update             get
 *         /                  |                 \
 *        V                   V                  \
 *   +----------+      +------------+        +----------+
 *   |          |      |            |        |          |
 *   +----------+      +------------+        +----------+
 *   | available|      | descriptor |        |   used   |
 *   |   ring   |      |   table    |        |   ring   |
 *   +----------+      +------------+        +----------+
 *   |          |      |            |        |          |
 *   +----------+      +------------+        +----------+
 *   |          |      |            |        |          |
 *   +----------+      +------------+        +----------+
 *        \                   ^                   ^
 *         \                  |                  /
 *         get             update              put
 *           \                |                /
 *            V               |               /
 *           +----------------+-------------------+
 *           |       virtio host backend          |
 *           +------------------------------------+
 *
 * 相关函数：
 * - 初始化 `vring_init()`
 */
struct vring {
	unsigned int num;

	vring_desc_t *desc;

	vring_avail_t *avail;

	vring_used_t *used;
};

#ifndef VIRTIO_RING_NO_LEGACY

/* The standard layout for the ring is a continuous chunk of memory which looks
 * like this.  We assume num is a power of 2.
 *
 * struct vring
 * {
 *	// The actual descriptors (16 bytes each)
 *	struct vring_desc desc[num];
 *
 *	// A ring of available descriptor heads with free-running index.
 *	__virtio16 avail_flags;
 *	__virtio16 avail_idx;
 *	__virtio16 available[num];
 *	__virtio16 used_event_idx;
 *
 *	// Padding to the next align boundary.
 *	char pad[];
 *
 *	// A ring of used descriptor heads with free-running index.
 *	__virtio16 used_flags;
 *	__virtio16 used_idx;
 *	struct vring_used_elem used[num];
 *	__virtio16 avail_event_idx;
 * };
 */
/* We publish the used event index at the end of the available ring, and vice
 * versa. They are at the end for backwards compatibility. */
#define vring_used_event(vr) ((vr)->avail->ring[(vr)->num])
#define vring_avail_event(vr) (*(__virtio16 *)&(vr)->used->ring[(vr)->num])
/**
 *
 */
static inline void vring_init(struct vring *vr, unsigned int num, void *p,
			      unsigned long align)
{
	/**
	 *  +-------+ <- p <- vr->desc
	 *  |       | \
	 *  +-------+ |
	 *  |       | |
	 *  +-------+ |
	 *  |       | +- num
	 *  +-------+ |
	 *  |       | |
	 *  +-------+ |
	 *  |       | /
	 *  +-------+ <- vr->avail
	 *  |       |
	 *  +-------+
	 *  |       |
	 *  +-------+
	 *
	 *
	 */
	vr->num = num;
	vr->desc = p;
	/**
	 *  可用
	 */
	vr->avail = (struct vring_avail *)((char *)p + num * sizeof(struct vring_desc));
	vr->used = (void *)(((uintptr_t)&vr->avail->ring[num] + sizeof(__virtio16)
		+ align-1) & ~(align - 1));
}

static inline unsigned vring_size(unsigned int num, unsigned long align)
{
	return ((sizeof(struct vring_desc) * num + sizeof(__virtio16) * (3 + num)
		 + align - 1) & ~(align - 1))
		+ sizeof(__virtio16) * 3 + sizeof(struct vring_used_elem) * num;
}

#endif /* VIRTIO_RING_NO_LEGACY */

/* The following is used with USED_EVENT_IDX and AVAIL_EVENT_IDX */
/* Assuming a given event_idx value from the other side, if
 * we have just incremented index from old to new_idx,
 * should we trigger an event? */
static inline int vring_need_event(__u16 event_idx, __u16 new_idx, __u16 old)
{
	/* Note: Xen has similar logic for notification hold-off
	 * in include/xen/interface/io/ring.h with req_event and req_prod
	 * corresponding to event_idx + 1 and new_idx respectively.
	 * Note also that req_event and req_prod in Xen start at 1,
	 * event indexes in virtio start at 0. */
	return (__u16)(new_idx - event_idx - 1) < (__u16)(new_idx - old);
}

struct vring_packed_desc_event {
	/* Descriptor Ring Change Event Offset/Wrap Counter. */
	__le16 off_wrap;
	/* Descriptor Ring Change Event Flags. */
	__le16 flags;
};

struct vring_packed_desc {
	/* Buffer Address. */
	__le64 addr;
	/* Buffer Length. */
	__le32 len;
	/* Buffer ID. */
	__le16 id;
	/* The flags depending on descriptor type. */
	__le16 flags;
};

#endif /* _UAPI_LINUX_VIRTIO_RING_H */
