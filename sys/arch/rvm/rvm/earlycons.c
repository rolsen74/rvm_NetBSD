/*
 * Early GeneralDriver console, adapted from SnakeOS term_Setup.c and
 * term_PrintChar.c. Copyright (c) 2026 by Rene W. Olsen.
 * All rights reserved.
 */
#include <machine/boot.h>
#include <machine/generic.h>

static volatile struct rvm_general_driver * const gd =
    (volatile struct rvm_general_driver *)RVM_GENERIC_ADDR;
static unsigned int column __attribute__((section(".data"))) = 0;
static unsigned int row __attribute__((section(".data"))) = 0;

void
rvm_early_consinit(void)
{
	column = row = 0;
	gd->video_control = 1;
	gd->term_control = RVM_TERM_XAUTOWRAP;
	gd->term_apen = 0xffffffffU;
	gd->term_bpen = 0xff000000U;
	gd->term_xpos = 0;
	gd->term_ypos = 0;
}

static void
rvm_early_newline(void)
{
	column = 0;
	if (row == RVM_TERM_ROWS - 1) {
		/* VScroll uses APen as its fill colour, as in SnakeOS. */
		gd->term_apen = 0xff000000U;
		gd->term_vscroll = 8;
		gd->term_apen = 0xffffffffU;
	} else {
		row++;
	}
}

void
rvm_early_putchar(int ch)
{
	/* Mirror to VMRun stdout so startup is observable without a display. */
	rvm_emulator_putchar(ch);
	if (ch == '\n') {
		rvm_early_newline();
	} else if (ch == '\r') {
		column = 0;
	} else {
		if (column == RVM_TERM_COLUMNS) {
			rvm_early_newline();
		}
		gd->term_xpos = column;
		gd->term_ypos = row;
		gd->term_putchar = (unsigned char)ch;
		column++;
	}
}

void
rvm_early_puts(const char *s)
{
	while (*s != '\0') {
		rvm_early_putchar((unsigned char)*s++);
	}
}

void
rvm_early_puthex(uint64_t value)
{
	static const char hex[] = "0123456789abcdef";
	int shift;

	for (shift = 60; shift >= 0; shift -= 4) {
		rvm_early_putchar(hex[(value >> shift) & 15]);
	}
}
