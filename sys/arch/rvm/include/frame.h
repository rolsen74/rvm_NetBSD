/*
 * NetBSD/RVM - hardware exception frames and software trapframe.
 *
 * Copyright (c) 2026 Rene W. Olsen
 *
 * Based on RVM CPU overview.md and v0000 control-register definitions.
 * Initial machine-dependent interface; not yet integrated with locore.S.
 */

#ifndef _RVM_FRAME_H_
#define _RVM_FRAME_H_

#include <machine/psl.h>

/*
 * Documented hardware exception frame, offsets relative to frame start:
 *
 *   +0   U16  version
 *   +2   U16  vector number
 *   +4   U16  interrupt level (version 10: instruction size)
 *   +6   U16  saved status (low 16 bits of SR, as documented)
 *   +8   U64  saved PC
 *   +16  U64  MMU table address
 *   +24  U64  physical fault address (versions 1 and 10)
 *        U64  virtual address       (version 3)
 *   +32  U64  physical address      (version 3)
 *
 * Verified against current VMEmu exception entry and RTE: version 0 and 2
 * headers store SR flags in bits 0..7 and SR interrupt priority in bits
 * 16..21 of the first big-endian word. The +4 halfword contains those
 * priority bits in place; this is not a truncated 16-bit saved SR.
 * Supervisor version-2 return and a version-0 timer IRQ/HALT return are
 * exercised in VMEmu. Nested/user IRQs and fault variants remain unverified.
 * The checked returning prefix (63 GPRs + version-2 hardware frame) is
 * defined separately in machine/trap.h; struct trapframe below is provisional.
 */
#define RVM_EF_VERSION_OFFSET	  0
#define RVM_EF_VECTOR_OFFSET	  2
#define RVM_EF_LEVEL_OFFSET		  4
#define RVM_EF_SR_OFFSET		  6
#define RVM_EF_PC_OFFSET		  8
#define RVM_EF_MMU_TABLE_OFFSET	  16
#define RVM_EF_ADDRESS_OFFSET	  24
#define RVM_EF_PHYS_ADDR_OFFSET	  32

#define RVM_EF_VERSION_INTERRUPT  0
#define RVM_EF_VERSION_PHYS_FAULT 1
#define RVM_EF_VERSION_SYSCALL	  2
#define RVM_EF_VERSION_MMU_FAULT  3
#define RVM_EF_VERSION_TEMP		  10

#define RVM_EF_SIZE_V0			  24
#define RVM_EF_SIZE_V1			  32
#define RVM_EF_SIZE_V2			  24
#define RVM_EF_SIZE_V3			  40
#define RVM_EF_SIZE_V10			  32

/* Exception vector IDs. */
#define RVM_VECTOR_ILLEGAL		  0
#define RVM_VECTOR_PRIVILEGE	  1
#define RVM_VECTOR_MMU_EXEC		  2
#define RVM_VECTOR_MMU_READ		  3
#define RVM_VECTOR_MMU_WRITE	  4
#define RVM_VECTOR_MMU_ENABLE	  5
#define RVM_VECTOR_MMU_TABLE	  6
#define RVM_VECTOR_MEMORY_READ	  7
#define RVM_VECTOR_MEMORY_WRITE	  8
#define RVM_VECTOR_DIVIDE		  9
#define RVM_VECTOR_SYSCALL		  10
#define RVM_VECTOR_STACK		  11
#define RVM_VECTOR_IRQ_FIRST	  32
#define RVM_VECTOR_IRQ_LAST		  95
#define RVM_VECTOR_IRQ_COUNT	  64

#ifndef _LOCORE
#include <sys/types.h>

/*
 * Software trapframe assembled by locore.S; not the raw CPU frame.
 *
 * R0..R62 are stored here. R63 is the currently active SP and is
 * represented via USP/SSP. Hardware selects the active SP according
 * to supervisor/user mode. The exact point at which the supervisor
 * SP is captured must be defined by locore.S.
 *
 * tf_sr is a normalized 64-bit status value; the raw hardware header also
 * retains interrupt priority in bits 16..21.
 *
 * NOTE: No fixed assembly offsets or ABI layout guarantees yet.
 */
struct trapframe
{
	uint64_t tf_r[63];
	uint64_t tf_usp;
	uint64_t tf_ssp;
	uint64_t tf_pc;
	uint64_t tf_mmu_table;
	uint64_t tf_fault_va;
	uint64_t tf_fault_pa;
	uint64_t tf_sr;
	uint16_t tf_vector;
	uint16_t tf_level; /* version 10: instruction size */
	uint16_t tf_version;
	uint16_t tf_reserved;
};

/* Test the saved execution mode, NOT the trap handler's current mode. */
#define RVM_TF_USERMODE( tf ) USERMODE ( ( tf )->tf_sr )

/*
 * TODO before locore.S:
 * - Verify user-mode and fault frame handling beyond supervisor version 2.
 * - Verify IRQ entry/return and nesting before enabling hardware interrupts.
 * - Define restart/advance rules for recoverable faults.
 * - Confirm which MMU table address is stored in the frame.
 * - Confirm how to recover/save general registers before using them.
 * - Assign fixed C/assembly trapframe offsets after ABI review.
 */
#endif /* !_LOCORE */

#endif /* _RVM_FRAME_H_ */
