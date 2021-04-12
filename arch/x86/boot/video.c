// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 * ----------------------------------------------------------------------- */

/*
 * Select video mode
 */

#include <uapi/asm/boot.h>

#include "boot.h"
#include "video.h"
#include "vesa.h"

static u16 video_segment;

static void store_cursor_position(void)
{
	struct biosregs ireg, oreg;

    //初始化两个类型biosregs为AH = 0x3的变量
	initregs(&ireg);

    //调用0x10BIOS中断
	ireg.ah = 0x03;
	intcall(0x10, &ireg, &oreg);

    //成功执行中断后，它将在DL和DH寄存器中返回行和列
    //行和列将存储在结构的orig_x和orig_y字段中boot_params.screen_info
	boot_params.screen_info.orig_x = oreg.dl;
	boot_params.screen_info.orig_y = oreg.dh;

	if (oreg.ch & 0x20)
		boot_params.screen_info.flags |= VIDEO_FLAGS_NOCURSOR;

	if ((oreg.ch & 0x1f) > (oreg.cl & 0x1f))
		boot_params.screen_info.flags |= VIDEO_FLAGS_NOCURSOR;
}

static void store_video_mode(void)
{//获取当前的视频模式并将其存储在中boot_params.screen_info.orig_video_mode
	struct biosregs ireg, oreg;

	/* N.B.: the saving of the video page here is a bit silly,
	   since we pretty much assume page 0 everywhere. */
	initregs(&ireg);
	ireg.ah = 0x0f;
	intcall(0x10, &ireg, &oreg);

	/* Not all BIOSes are clean with respect to the top bit */
	boot_params.screen_info.orig_video_mode = oreg.al & 0x7f; //设置显示内存的起始地址
	boot_params.screen_info.orig_video_page = oreg.bh;
}

/*
 * Store the video mode parameters for later usage by the kernel.
 * This is done by asking the BIOS except for the rows/columns
 * parameters in the default 80x25 mode -- these are set directly,
 * because some very obscure BIOSes supply insane values.
 *///将视频模式参数存储在 boot_params.screen_info 
static void store_mode_params(void)
{
	u16 font_size;
	int x, y;

	/* For graphics mode, it is up to the mode-setting driver
	   (currently only video-vesa.c) to store the parameters */
	if (graphic_mode)
		return;

    //获取有关游标的信息并进行存储
	store_cursor_position();

    //获取当前的视频模式并将其存储在中boot_params.screen_info.orig_video_mode
	store_video_mode();

    //检查当前的视频模式并设置 video_segment
	if (boot_params.screen_info.orig_video_mode == 0x07) {
		/* MDA, HGC, or VGA in monochrome mode */
        //0xb000单色模式下当前视频模式是MDA，HGC还是VGA
		video_segment = 0xb000;
	} else {
		/* CGA, EGA, VGA and so forth */
        //0xb800当前视频模式下是彩色模式
		video_segment = 0xb800;
	}

    //存储字体大小
	set_fs(0);  //使用set_fs函数将0放入寄存器FS中
	//读取位于地址处的值0x485（此存储位置用于获取字体大小）
	font_size = rdfs16(0x485); /* Font size, BIOS area */
	boot_params.screen_info.orig_video_points = font_size;

    //按0x44a地址获取列数，按0x484地址获取行数，
	x = rdfs16(0x44a);
	y = (adapter == ADAPTER_CGA) ? 25 : rdfs8(0x484)+1;

	if (force_x)
		x = force_x;
	if (force_y)
		y = force_y;

	boot_params.screen_info.orig_video_cols  = x;
	boot_params.screen_info.orig_video_lines = y;
}

static unsigned int get_entry(void)
{
	char entry_buf[4];
	int i, len = 0;
	int key;
	unsigned int v;

	do {
		key = getchar();

		if (key == '\b') {
			if (len > 0) {
				puts("\b \b");
				len--;
			}
		} else if ((key >= '0' && key <= '9') ||
			   (key >= 'A' && key <= 'Z') ||
			   (key >= 'a' && key <= 'z')) {
			if (len < sizeof(entry_buf)) {
				entry_buf[len++] = key;
				putchar(key);
			}
		}
	} while (key != '\r');
	putchar('\n');

	if (len == 0)
		return VIDEO_CURRENT_MODE; /* Default */

	v = 0;
	for (i = 0; i < len; i++) {
		v <<= 4;
		key = entry_buf[i] | 0x20;
		v += (key > '9') ? key-'a'+10 : key-'0';
	}

	return v;
}

static void display_menu(void)
{
	struct card_info *card;
	struct mode_info *mi;
	char ch;
	int i;
	int nmodes;
	int modes_per_line;
	int col;

	nmodes = 0;
	for (card = video_cards; card < video_cards_end; card++)
		nmodes += card->nmodes;

	modes_per_line = 1;
	if (nmodes >= 20)
		modes_per_line = 3;

	for (col = 0; col < modes_per_line; col++)
		puts("Mode: Resolution:  Type: ");
	putchar('\n');

	col = 0;
	ch = '0';
	for (card = video_cards; card < video_cards_end; card++) {
		mi = card->modes;
		for (i = 0; i < card->nmodes; i++, mi++) {
			char resbuf[32];
			int visible = mi->x && mi->y;
			u16 mode_id = mi->mode ? mi->mode :
				(mi->y << 8)+mi->x;

			if (!visible)
				continue; /* Hidden mode */

			if (mi->depth)
				sprintf(resbuf, "%dx%d", mi->y, mi->depth);
			else
				sprintf(resbuf, "%d", mi->y);

			printf("%c %03X %4dx%-7s %-6s",
			       ch, mode_id, mi->x, resbuf, card->card_name);
			col++;
			if (col >= modes_per_line) {
				putchar('\n');
				col = 0;
			}

			if (ch == '9')
				ch = 'a';
			else if (ch == 'z' || ch == ' ')
				ch = ' '; /* Out of keys... */
			else
				ch++;
		}
	}
	if (col)
		putchar('\n');
}

#define H(x)	((x)-'a'+10)
#define SCAN	((H('s')<<12)+(H('c')<<8)+(H('a')<<4)+H('n'))

static unsigned int mode_menu(void)
{
	int key;
	unsigned int sel;

	puts("Press <ENTER> to see video modes available, "
	     "<SPACE> to continue, or wait 30 sec\n");

	kbd_flush();
	while (1) {
		key = getchar_timeout();
		if (key == ' ' || key == 0)
			return VIDEO_CURRENT_MODE; /* Default */
		if (key == '\r')
			break;
		putchar('\a');	/* Beep! */
	}


	for (;;) {
		display_menu();

		puts("Enter a video mode or \"scan\" to scan for "
		     "additional modes: ");
		sel = get_entry();
		if (sel != SCAN)
			return sel;

		probe_cards(1);
	}
}

/* Save screen content to the heap */
static struct saved_screen {
	int x, y;
	int curx, cury;
	u16 *data;
} saved;

static void save_screen(void)
{//将屏幕内容保存到堆的函数
//此函数收集我们在先前函数中获得的所有数据（如行，列和填充），并将其存储在saved_screen结构中

	/* Should be called after store_mode_params() */
	saved.x = boot_params.screen_info.orig_video_cols;
	saved.y = boot_params.screen_info.orig_video_lines;
	saved.curx = boot_params.screen_info.orig_x;
	saved.cury = boot_params.screen_info.orig_y;

    //检查堆是否有可用空间
	if (!heap_free(saved.x*saved.y*sizeof(u16)+512))
		return;		/* Not enough heap to save the screen */

    //在堆中分配足够的空间并进行存储saved_screen
	saved.data = GET_HEAP(u16, saved.x*saved.y);

	set_fs(video_segment);
	copy_from_fs(saved.data, 0, saved.x*saved.y*sizeof(u16));
}

            //将前面保存的当前屏幕信息还原到屏幕上
static void restore_screen(void)
{
	/* Should be called after store_mode_params() */
	int xs = boot_params.screen_info.orig_video_cols;
	int ys = boot_params.screen_info.orig_video_lines;
	int y;
	addr_t dst = 0;
	u16 *src = saved.data;
	struct biosregs ireg;

	if (graphic_mode)
		return;		/* Can't restore onto a graphic mode */

	if (!src)
		return;		/* No saved screen contents */

	/* Restore screen contents */

	set_fs(video_segment);
	for (y = 0; y < ys; y++) {
		int npad;

		if (y < saved.y) {
			int copy = (xs < saved.x) ? xs : saved.x;
			copy_to_fs(dst, src, copy*sizeof(u16));
			dst += copy*sizeof(u16);
			src += saved.x;
			npad = (xs < saved.x) ? 0 : xs-saved.x;
		} else {
			npad = xs;
		}

		/* Writes "npad" blank characters to
		   video_segment:dst and advances dst */
		asm volatile("pushw %%es ; "
			     "movw %2,%%es ; "
			     "shrw %%cx ; "
			     "jnc 1f ; "
			     "stosw \n\t"
			     "1: rep;stosl ; "
			     "popw %%es"
			     : "+D" (dst), "+c" (npad)
			     : "bdS" (video_segment),
			       "a" (0x07200720));
	}

	/* Restore cursor position */
	if (saved.curx >= xs)
		saved.curx = xs-1;
	if (saved.cury >= ys)
		saved.cury = ys-1;

	initregs(&ireg);
	ireg.ah = 0x02;		/* Set cursor position */
	ireg.dh = saved.cury;
	ireg.dl = saved.curx;
	intcall(0x10, &ireg, NULL);

	store_cursor_position();
}

//视频模式设置
void set_video(void)
{
    //从 boot_params.hdr 结构中获取视频模式：
    //vid_mode是引导程序填充的必填字段。
    //您可以在内核中找到有关它的信息 boot protocol
	u16 mode = boot_params.hdr.vid_mode;

	RESET_HEAP();   /* ( HEAP = _end ) */

    //将视频模式参数存储在 boot_params.screen_info 
	store_mode_params();

    //将屏幕内容保存到堆的函数
	save_screen();

    //遍历所有video_cards并收集卡提供的模式数量
	probe_cards(0);

	for (;;) {
		if (mode == ASK_VGA)
			mode = mode_menu();

        //set_mode函数检查mode和调用该raw_set_mode函数
		if (!set_mode(mode))
			break;

		printf("Undefined video mode number: %x\n", mode);
		mode = ASK_VGA;
	}
    //设置视频模式后，将其传递给boot_params.hdr.vid_mode
	boot_params.hdr.vid_mode = mode;

    //简单存储EDID（Extended Display Identification DATA）内核使用的信息
	vesa_store_edid();

    //将视频模式参数存储在 boot_params.screen_info 
	store_mode_params();

    //将前面保存的当前屏幕信息还原到屏幕上
	if (do_restore)
		restore_screen();
}
