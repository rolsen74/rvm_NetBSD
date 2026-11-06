

/*
 * NetBSD/RVM - machine-dependent process control block.
 *
 * Initial port scaffold.  The CPU has 64 64-bit integer registers;
 * r63 is the active stack pointer.  On a change of privilege level,
 * the CPU selects USP or SSP for r63 automatically.
 *
 * This describes a *saved kernel execution context*, not the complete
 * exception frame or the user-visible register set.  Define those in
 * machine/frame.h and machine/reg.h when trap handling is implemented.
 */

#ifndef _RVM_PCB_H_
#define _RVM_PCB_H_

#include <sys/types.h>

#define RVM_NGREG  64
#define RVM_REG_FP 62
#define RVM_REG_SP 63

#ifndef _LOCORE
struct pcb
{
	/*
	 * Kernel cpu_switchto saves R10-R62 in the stack switchframe.
	 * Only pcb_gpr[SP] is populated here; other entries remain reserved.
	 * pcb_gpr[RVM_REG_SP] is the saved *supervisor* stack pointer
	 * for kernel-to-kernel context switches.
	 */
	uint64_t pcb_gpr[RVM_NGREG];

	/* Separate hardware stack pointers. */
	uint64_t pcb_usp; /* Inactive user stack pointer */
	uint64_t pcb_ssp; /* Supervisor stack pointer */

	/* Software continuation for the kernel context switch. */
	uint64_t pcb_pc;

	/*
	 * MMU context and status are provisional: decide which control
	 * registers must be saved after documenting the MMU interface.
	 */
	uint64_t pcb_mmu_table;
	uint64_t pcb_sr;

	/* Used to recover from expected kernel access faults (copyin/out). */
	void * pcb_onfault;
};
#endif /* !_LOCORE */

#endif /* _RVM_PCB_H_ */
