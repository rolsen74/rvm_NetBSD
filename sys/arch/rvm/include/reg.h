

/*
 * NetBSD/RVM - user-visible integer register state.
 *
 * Initial port scaffold.  The layout is intended for ptrace(2), core
 * dumps, and debugger register access; it is NOT the hardware exception
 * frame and is NOT the kernel process control block (PCB).
 *
 * TODO: Keep in sync with process_read_regs/process_write_regs,
 *       signal mcontext, and the RVM debugger's register numbering.
 */

#ifndef _RVM_REG_H_
#define _RVM_REG_H_

#include <sys/types.h>

#define RVM_NGREG		64
#define RVM_REG_RET0	0
#define RVM_REG_RET1	1
#define RVM_REG_ARG0	2
#define RVM_REG_ARG7	9
#define RVM_REG_RODATA	50
#define RVM_REG_DATA	51
#define RVM_REG_BSS		52
#define RVM_REG_OSBASE	53
#define RVM_REG_LIBBASE 54
#define RVM_REG_JCP		61
#define RVM_REG_FP		62
#define RVM_REG_SP		63

/*
 * General-purpose state as seen by a user process.
 *
 * r_regs[0..62] = R0..R62.
 * r_regs[63]    = R63 (the user-mode active SP, i.e. USP).
 *
 * R63 is the active stack pointer; the CPU switches its backing
 * register between USP and SSP according to supervisor/user mode.
 * SSP is deliberately not exposed in this user-register structure.
 *
 * r_sr contains saved process status; privileged SR bits must be
 * validated and masked by process_write_regs() before restoration.
 * The SR bit layout is not defined here yet.
 */
struct reg
{
	uint64_t r_regs[RVM_NGREG];
	uint64_t r_pc;
	uint64_t r_sr;
};

/*
 * No floating-point register structure is declared until an RVM FPU ABI
 * is defined.  NetBSD ptrace/core-dump integration must account for this.
 */

#endif /* _RVM_REG_H_ */
