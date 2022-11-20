/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2018 Facebook */
#ifndef _UAPI__LINUX_BTF_H__
#define _UAPI__LINUX_BTF_H__

#include <linux/types.h>

/**
 *  识别码
 *
 * $ od -x  /sys/kernel/btf/vmlinux | more
 * 0000000 eb9f 0001 0018 0000 0000 0000 b428 0027
 * 0000020 b428 0027 0a77 001b 0001 0000 0000 0100
 * 0000040 0008 0000 0040 0000 0000 0000 0000 0a00
 * [...]
 */
#define BTF_MAGIC	0xeB9F
#define BTF_VERSION	1

/**
 *  BTF  头
 *
 * $ od -x  /sys/kernel/btf/vmlinux | more
 * 0000000 eb9f 0001 0018 0000 0000 0000 b428 0027
 * 0000020 b428 0027 0a77 001b 0001 0000 0000 0100
 * 0000040 0008 0000 0040 0000 0000 0000 0000 0a00
 * [...]
 */
struct btf_header {
	__u16	magic;
	__u8	version;
	__u8	flags;
	__u32	hdr_len;

	/* All offsets are in bytes relative to the end of this header */
	__u32	type_off;	/* offset of type section	*/
	__u32	type_len;	/* length of type section	*/
	__u32	str_off;	/* offset of string section	*/
	__u32	str_len;	/* length of string section	*/
};

/* Max # of type identifier */
#define BTF_MAX_TYPE	0x000fffff
/* Max offset into the string section */
#define BTF_MAX_NAME_OFFSET	0x00ffffff
/* Max # of struct/union/enum members or func args */
#define BTF_MAX_VLEN	0xffff

/**
 * Each type contains the following common data:
 *
 * struct btf_type encoding requirement:
 *
 * BTF_KIND_INT
 * 		name_off: any valid offset
 * 		info.kind_flag: 0
 * 		info.kind: BTF_KIND_INT
 * 		info.vlen: 0
 * 		size: the size of the int type in bytes.
 *
 * BTF_KIND_PTR
 * ...
 */
struct btf_type {
	__u32 name_off;
	/* "info" bits arrangement
	 * bits  0-15: vlen (e.g. # of struct's members)
	 * bits 16-23: unused
	 * bits 24-27: kind (e.g. int, ptr, array...etc)
	 * 		BTF_KIND_INT
	 * 		BTF_KIND_PTR
	 * 		...
	 * 		BTF_KIND_UNION
	 * 		BTF_KIND_ENUM
	 * bits 28-30: unused
	 * bit     31: kind_flag, currently used by
	 *             struct, union and fwd
	 */
	__u32 info;
	/* "size" is used by INT, ENUM, STRUCT, UNION and DATASEC.
	 * "size" tells the size of the type it is describing.
	 *
	 * "type" is used by PTR, TYPEDEF, VOLATILE, CONST, RESTRICT,
	 * FUNC, FUNC_PROTO and VAR.
	 * "type" is a type_id referring to another type.
	 */
	union {
		__u32 size;
		__u32 type;
	};
};

#define BTF_INFO_KIND(info)	(((info) >> 24) & 0x0f)
#define BTF_INFO_VLEN(info)	((info) & 0xffff)
#define BTF_INFO_KFLAG(info)	((info) >> 31)

/**
 * @brief BTF 类型
 *
 * @ref https://nakryiko.com/posts/btf-dedup/
 *
 * @note notes/ebpf/btf.md
 */
#define BTF_KIND_UNKN		0	/* Unknown	*/
/**
 * BTF_KIND_INT
 * struct btf_type encoding requirement:
 * 		name_off: any valid offset
 * 		info.kind_flag: 0
 * 		info.kind: BTF_KIND_INT
 * 		info.vlen: 0
 * 		size: the size of the int type in bytes.
 *
 * see btf_int_show()
 */
#define BTF_KIND_INT		1	/* Integer	*/
/**
 * struct btf_type encoding requirement:
 * 		name_off: 0
 * 		info.kind_flag: 0
 * 		info.kind: BTF_KIND_PTR
 * 		info.vlen: 0
 * 		type: the pointee type of the pointer
 * No additional type data follow btf_type.
 */
#define BTF_KIND_PTR		2	/* Pointer	*/
#define BTF_KIND_ARRAY		3	/* Array	*/
#define BTF_KIND_STRUCT		4	/* Struct	*/
#define BTF_KIND_UNION		5	/* Union	*/
#define BTF_KIND_ENUM		6	/* Enumeration	*/
/**
 * FWD 可能表示：
 *
 * 1.当前结构体中没有定义的结构，如：
 *  struct A;
 *  struct S {
 *    volatile struct A* const a_ptr;
 *  };
 *  这里的 struct A 就是一个 FWD 类型。
 */
#define BTF_KIND_FWD		7	/* Forward	*/
#define BTF_KIND_TYPEDEF	8	/* Typedef	*/
#define BTF_KIND_VOLATILE	9	/* Volatile	*/
#define BTF_KIND_CONST		10	/* Const	*/
#define BTF_KIND_RESTRICT	11	/* Restrict	*/
#define BTF_KIND_FUNC		12	/* Function	*/
#define BTF_KIND_FUNC_PROTO	13	/* Function Proto	*/
#define BTF_KIND_VAR		14	/* Variable	*/
#define BTF_KIND_DATASEC	15	/* Section	*/

/* 下面是 5.10.13 以后添加的 Kind */

#define BTF_KIND_FLOAT		16
#define BTF_KIND_DECL_TAG	17
#define BTF_KIND_TYPE_TAG	18
#define BTF_KIND_ENUM64		19
#define BTF_KIND_MAX		BTF_KIND_DATASEC
#define NR_BTF_KINDS		(BTF_KIND_MAX + 1)

/* For some specific BTF_KIND, "struct btf_type" is immediately
 * followed by extra data.
 */

/* BTF_KIND_INT is followed by a u32 and the following
 * is the 32 bits arrangement:
 */
#define BTF_INT_ENCODING(VAL)	(((VAL) & 0x0f000000) >> 24)
#define BTF_INT_OFFSET(VAL)	(((VAL) & 0x00ff0000) >> 16)
#define BTF_INT_BITS(VAL)	((VAL)  & 0x000000ff)

/* Attributes stored in the BTF_INT_ENCODING */
#define BTF_INT_SIGNED	(1 << 0) // 是否有符号
#define BTF_INT_CHAR	(1 << 1) // char
#define BTF_INT_BOOL	(1 << 2) // bool

/* BTF_KIND_ENUM is followed by multiple "struct btf_enum".
 * The exact number of btf_enum is stored in the vlen (of the
 * info in "struct btf_type").
 */
struct btf_enum {
	__u32	name_off;
	__s32	val;
};

/* BTF_KIND_ARRAY is followed by one "struct btf_array" */
struct btf_array {
	__u32	type;
	__u32	index_type;
	__u32	nelems;
};

/* BTF_KIND_STRUCT and BTF_KIND_UNION are followed
 * by multiple "struct btf_member".  The exact number
 * of btf_member is stored in the vlen (of the info in
 * "struct btf_type").
 */
struct btf_member {
	__u32	name_off;
	__u32	type;
	/* If the type info kind_flag is set, the btf_member offset
	 * contains both member bitfield size and bit offset. The
	 * bitfield size is set for bitfield members. If the type
	 * info kind_flag is not set, the offset contains only bit
	 * offset.
	 */
	__u32	offset;
};

/* If the struct/union type info kind_flag is set, the
 * following two macros are used to access bitfield_size
 * and bit_offset from btf_member.offset.
 */
#define BTF_MEMBER_BITFIELD_SIZE(val)	((val) >> 24)
#define BTF_MEMBER_BIT_OFFSET(val)	((val) & 0xffffff)

/* BTF_KIND_FUNC_PROTO is followed by multiple "struct btf_param".
 * The exact number of btf_param is stored in the vlen (of the
 * info in "struct btf_type").
 */
struct btf_param {
	__u32	name_off;
	__u32	type;
};

enum {
	BTF_VAR_STATIC = 0,
	BTF_VAR_GLOBAL_ALLOCATED = 1,
	BTF_VAR_GLOBAL_EXTERN = 2,
};

enum btf_func_linkage {
	BTF_FUNC_STATIC = 0,
	BTF_FUNC_GLOBAL = 1,
	BTF_FUNC_EXTERN = 2,
};

/* BTF_KIND_VAR is followed by a single "struct btf_var" to describe
 * additional information related to the variable such as its linkage.
 */
struct btf_var {
	__u32	linkage;
};

/* BTF_KIND_DATASEC is followed by multiple "struct btf_var_secinfo"
 * to describe all BTF_KIND_VAR types it contains along with it's
 * in-section offset as well as size.
 */
struct btf_var_secinfo {
	__u32	type;
	__u32	offset;
	__u32	size;
};

#endif /* _UAPI__LINUX_BTF_H__ */
