/* SPDX-License-Identifier: GPL-2.0 */
/* Written 2000 by Andi Kleen */
#ifndef _ASM_X86_DESC_DEFS_H
#define _ASM_X86_DESC_DEFS_H

/*
 * Segment descriptor structure definitions, usable from both x86_64 and i386
 * archs.
 */

#ifndef __ASSEMBLY__

#include <linux/types.h>

/**
 * 8 byte segment descriptor
 *
 * https://0xax.gitbooks.io/linux-insides/content/Booting/linux-bootstrap-2.html
 *
 *  63         56         51   48    45           39        32
 * ------------------------------------------------------------
 * |             | |B| |A|       | |   | |0|E|W|A|            |
 * | BASE 31:24  |G|/|L|V| LIMIT |P|DPL|S|  TYPE | BASE 23:16 |
 * |             | |D| |L| 19:16 | |   | |1|C|R|A|            |
 * ------------------------------------------------------------
 *
 *  31                         16 15                         0
 * ------------------------------------------------------------
 * |                             |                            |
 * |        BASE 15:0            |       LIMIT 15:0           |
 * |                             |                            |
 * ------------------------------------------------------------
 */
struct desc_struct {
	u16	limit0;
	u16	base0;
	u16	base1: 8, type: 4, s: 1, dpl: 2, p: 1;
	u16	limit1: 4, avl: 1, l: 1, d: 1, g: 1, base2: 8;
} __attribute__((packed));

//https://0xax.gitbooks.io/linux-insides/content/Booting/linux-bootstrap-2.html
//
//Limit [20-bits]在位0-15和48-51之间分配。它定义了length_of_segment - 1。这取决于G（粒度）位。
//
//    如果G（位55）为0并且段限制为0，则段的大小为1字节
//    如果G为1，并且段限制为0，则段的大小为4096字节
//    如果G为0，并且段限制为0xfffff，则段的大小为1 MB
//    如果G为1，并且段限制为0xfffff，则段的大小为4 GB
//
//所以，这意味着
//
//    如果G为0，则将Limit解释为1 Byte，该段的最大大小可以为1 MB。
//    如果G为1，则将限制解释为4096字节= 4字节= 1页，该段的最大大小可以为4吉字节。
//    实际上，当G为1时，Limit的值向左移动12位。因此，20位+ 12位= 32位，而2^32 = 4 GB。
//
//Base [32位]在位16-31、32-39和56-63之间分配。它定义了段起始位置的物理地址。
//
//类型/属性[5位]由位40-44表示。它定义了段的类型以及如何访问它。
//
//    S位44的标志指定描述符类型。
//        如果S为0，则此段为系统段，
//        如果S为1，则此段为代码段或数据段（堆栈段为必须是读/写段的数据段）。
//
//要确定该段是代码段还是数据段，我们可以检查其Ex（bit 43）属性（在上图中标记为0）。
//如果为0，则该段为数据段，否则为代码段。

//段可以是以下类型之一：
//
//--------------------------------------------------------------------------------------
//|           Type Field        | Descriptor Type | Description                        |
//|-----------------------------|-----------------|------------------------------------|
//| Decimal                     |                 |                                    |
//|             0    E    W   A |                 |                                    |
//| 0           0    0    0   0 | Data            | Read-Only                          |
//| 1           0    0    0   1 | Data            | Read-Only, accessed                |
//| 2           0    0    1   0 | Data            | Read/Write                         |
//| 3           0    0    1   1 | Data            | Read/Write, accessed               |
//| 4           0    1    0   0 | Data            | Read-Only, expand-down             |
//| 5           0    1    0   1 | Data            | Read-Only, expand-down, accessed   |
//| 6           0    1    1   0 | Data            | Read/Write, expand-down            |
//| 7           0    1    1   1 | Data            | Read/Write, expand-down, accessed  |
//|                  C    R   A |                 |                                    |
//| 8           1    0    0   0 | Code            | Execute-Only                       |
//| 9           1    0    0   1 | Code            | Execute-Only, accessed             |
//| 10          1    0    1   0 | Code            | Execute/Read                       |
//| 11          1    0    1   1 | Code            | Execute/Read, accessed             |
//| 12          1    1    0   0 | Code            | Execute-Only, conforming           |
//| 14          1    1    0   1 | Code            | Execute-Only, conforming, accessed |
//| 13          1    1    1   0 | Code            | Execute/Read, conforming           |
//| 15          1    1    1   1 | Code            | Execute/Read, conforming, accessed |
//--------------------------------------------------------------------------------------
//
//从实模式转换为保护模式的算法是：
//
//    禁用中断
//    用lgdt指令描述并加载GDT
//    将CR0（控制寄存器0）中的PE（保护使能）位置1
//    跳转到保护模式代码


#define GDT_ENTRY_INIT(flags, base, limit)			\
	{							\
		.limit0		= (u16) (limit),		\
		.limit1		= ((limit) >> 16) & 0x0F,	\
		.base0		= (u16) (base),			\
		.base1		= ((base) >> 16) & 0xFF,	\
		.base2		= ((base) >> 24) & 0xFF,	\
		.type		= (flags & 0x0f),		\
		.s		= (flags >> 4) & 0x01,		\
		.dpl		= (flags >> 5) & 0x03,		\
		.p		= (flags >> 7) & 0x01,		\
		.avl		= (flags >> 12) & 0x01,		\
		.l		= (flags >> 13) & 0x01,		\
		.d		= (flags >> 14) & 0x01,		\
		.g		= (flags >> 15) & 0x01,		\
	}

enum {  //中断类型
	GATE_INTERRUPT = 0xE,   /* 中断门 */
	GATE_TRAP = 0xF,        /* 陷阱 */
	GATE_CALL = 0xC,
	GATE_TASK = 0x5,        /* 任务 */
};

enum {
	DESC_TSS = 0x9,
	DESC_LDT = 0x2,
	DESCTYPE_S = 0x10,	/* !system */
};

/* 任务门描述符: LDT or TSS descriptor in the GDT. */
struct ldttss_desc {    /* `Task State Segments` */
	u16	limit0;
	u16	base0;

	u16	base1 : 8, type : 5, dpl : 2, p : 1;
	u16	limit1 : 4, zero0 : 3, g : 1, base2 : 8;
#ifdef CONFIG_X86_64
	u32	base3;
	u32	zero1;
#endif
} __attribute__((packed));

typedef struct ldttss_desc ldt_desc;
typedef struct ldttss_desc tss_desc;    /* `Task State Segments` */

/**
 * 见下图 [32-48]
 */
//127                                                                             96
// --------------------------------------------------------------------------------
//|                                                                               |
//|                                Reserved                                       |
//|                                                                               |
// --------------------------------------------------------------------------------
//95                                                                              64
// --------------------------------------------------------------------------------
//|                                                                               |
//|                               Offset 63..32                                   |
//|                                                                               |
// --------------------------------------------------------------------------------
//63                               48 47      46  44   42    39             34    32
// --------------------------------------------------------------------------------
//|                                  |       |  D  |   |     |      |   |   |     |
//|       Offset 31..16              |   P   |  P  | 0 |Type |0 0 0 | 0 | 0 | IST |
//|                                  |       |  L  |   |     |      |   |   |     |
// --------------------------------------------------------------------------------
//31                                   15 16                                      0
// --------------------------------------------------------------------------------
//|                                      |                                        |
//|          Segment Selector            |                 Offset 15..0           |
//|                                      |                                        |
// --------------------------------------------------------------------------------
struct idt_bits {/* 中断 索引 */
	u16		ist	: 3,    //* `IST` - 中断堆栈表；`Interrupt Stack Table` 是 `x86_64` 中的新机制
	                    //代替传统的栈切换机制
			zero	: 5,
			type	: 5,    // `Type` 域描述了这一项的类型
                            //* 任务描述符
                            //* 中断描述符
                            //* 陷阱描述符
			dpl	: 2,    //* `DPL` - 描述符权限级别；还有个CPL(Current Privilege Level,`3`=用户空间级别)
			            //
			p	: 1;    //* `P` - 当前段标志；段存在标志；
} __attribute__((packed));

/**
 * 中断描述符表 数据 见 `arch/x86/kernel/idt.c`
 * 见下图 [32-48]
 */
struct idt_data {
    /**
     *  中断向量号, 如 `LOCAL_TIMER_VECTOR`
     */
	unsigned int	vector;
	unsigned int	segment;
	struct idt_bits	bits;
    /**
     *  处理函数地址, 如 `asm_sysvec_apic_timer_interrupt()`
     */
	const void	*addr;
};

//把IDT中的每一项叫做 `门(gate)`
//CPU使用一个特殊的 `GDTR` 寄存器来存放全局描述符表的地址，中断描述符表也有一个类似的寄存器 `IDTR`
//64位模式下 IDT 的每一项的结构如下：
//
//127                                                                             96
// --------------------------------------------------------------------------------
//|                                                                               |
//|                                Reserved                                       |
//|                                                                               |
// --------------------------------------------------------------------------------
//95                                                                              64
// --------------------------------------------------------------------------------
//|                                                                               |
//|                               Offset 63..32                                   |
//|                                                                               |
// --------------------------------------------------------------------------------
//63                               48 47      46  44   42    39             34    32
// --------------------------------------------------------------------------------
//|                                  |       |  D  |   |     |      |   |   |     |
//|       Offset 31..16              |   P   |  P  | 0 |Type |0 0 0 | 0 | 0 | IST |
//|                                  |       |  L  |   |     |      |   |   |     |
// --------------------------------------------------------------------------------
//31                                   15 16                                      0
// --------------------------------------------------------------------------------
//|                                      |                                        |
//|          Segment Selector            |                 Offset 15..0           |
//|                                      |                                        |
// --------------------------------------------------------------------------------
struct gate_struct {
	/**
	 * `Offset` - 处理程序入口点的偏移量；
	 */
	u16		offset_low;
	/**
	 * `Selector` - 目标代码段的段选择子；
	 */
	u16		segment;
	struct idt_bits	bits;
	u16		offset_middle;
#ifdef CONFIG_X86_64
	u32		offset_high;
	u32		reserved;
#endif
} __attribute__((packed));



typedef struct gate_struct gate_desc;

static inline unsigned long gate_offset(const gate_desc *g)
{
#ifdef CONFIG_X86_64
	return g->offset_low | ((unsigned long)g->offset_middle << 16) |
		((unsigned long) g->offset_high << 32);
#else
	return g->offset_low | ((unsigned long)g->offset_middle << 16);
#endif
}

static inline unsigned long gate_segment(const gate_desc *g)
{
	return g->segment;
}

struct desc_ptr {/*  */
	unsigned short size;
	unsigned long address;
} __attribute__((packed)) ;

#endif /* !__ASSEMBLY__ */

/* Boot IDT definitions */
#define	BOOT_IDT_ENTRIES	32

/* Access rights as returned by LAR */
#define AR_TYPE_RODATA		(0 * (1 << 9))
#define AR_TYPE_RWDATA		(1 * (1 << 9))
#define AR_TYPE_RODATA_EXPDOWN	(2 * (1 << 9))
#define AR_TYPE_RWDATA_EXPDOWN	(3 * (1 << 9))
#define AR_TYPE_XOCODE		(4 * (1 << 9))
#define AR_TYPE_XRCODE		(5 * (1 << 9))
#define AR_TYPE_XOCODE_CONF	(6 * (1 << 9))
#define AR_TYPE_XRCODE_CONF	(7 * (1 << 9))
#define AR_TYPE_MASK		(7 * (1 << 9))

#define AR_DPL0			(0 * (1 << 13))
#define AR_DPL3			(3 * (1 << 13))
#define AR_DPL_MASK		(3 * (1 << 13))

#define AR_A			(1 << 8)   /* "Accessed" */
#define AR_S			(1 << 12)  /* If clear, "System" segment */
#define AR_P			(1 << 15)  /* "Present" */
#define AR_AVL			(1 << 20)  /* "AVaiLable" (no HW effect) */
#define AR_L			(1 << 21)  /* "Long mode" for code segments */
#define AR_DB			(1 << 22)  /* D/B, effect depends on type */
#define AR_G			(1 << 23)  /* "Granularity" (limit in pages) */

#endif /* _ASM_X86_DESC_DEFS_H */
