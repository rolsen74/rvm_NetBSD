/*
 * RVM GeneralDriver register prefix, adapted from SnakeOS Generic.h.
 * Copyright (c) 2024-2026 by Rene W. Olsen. All rights reserved.
 * Only registers needed for early startup are described here.
 */
#ifndef _RVM_GENERIC_H_
#define _RVM_GENERIC_H_

#include <sys/types.h>

#define RVM_TIMER_INTERRUPT 0x1UL
#define RVM_TIMER_ACTIVE 0x8UL

#define RVM_GENERIC_ADDR 0x0700000000001000UL
#define RVM_GENERIC_VIDEO_ADDR 0x0700000200000000UL
#define RVM_TERM_COLUMNS 100
#define RVM_TERM_ROWS 75
#define RVM_TERM_XAUTOWRAP 1
#define RVM_TERM_CURSOR 4

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
	uint64_t entropy_status;           /* register 3: available=1, last read OK=2 */
	struct rvm_generic_timer timer[8]; /* registers 4..35 */
	uint64_t monotonic_ns;             /* register 36: ns since VM start */
	uint64_t rtc_seconds;              /* register 37: latches fractional ns */
	uint64_t rtc_nanoseconds;          /* register 38: consumes latched ns */
	uint64_t entropy_data;             /* register 39: fresh host random word */
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
