/*
 * RVM GeneralDriver register prefix, adapted from SnakeOS Generic.h.
 * Copyright (c) 2024-2026 by Rene W. Olsen. All rights reserved.
 * Only registers needed for early startup are described here.
 */
#ifndef _RVM_GENERIC_H_
#define _RVM_GENERIC_H_

#include <sys/types.h>

#define RVM_GENERIC_ADDR 0x0700000100000000UL
#define RVM_GENERIC_VIDEO_ADDR 0x0700000200000000UL
#define RVM_TERM_COLUMNS 100
#define RVM_TERM_ROWS 75
#define RVM_TERM_XAUTOWRAP 1

struct rvm_generic_timer {
	uint64_t control;
	uint64_t counter;
	uint64_t reset;
	uint64_t reserved;
};

struct rvm_general_driver {
	uint64_t int_mask;                 /* register 0 */
	uint64_t keyboard;
	uint64_t mouse;
	uint64_t reserved3;
	struct rvm_generic_timer timer[8]; /* registers 4..35 */
	uint64_t reserved36[4];
	uint64_t term_control;             /* register 40 */
	uint64_t term_apen;
	uint64_t term_bpen;
	int64_t term_xpos;
	int64_t term_ypos;
	uint64_t term_putchar;             /* register 45 */
	int64_t term_vscroll;
	int64_t term_hscroll;
	uint64_t reserved48[16];
	uint64_t video_control;            /* register 64 */
};

#endif /* _RVM_GENERIC_H_ */
