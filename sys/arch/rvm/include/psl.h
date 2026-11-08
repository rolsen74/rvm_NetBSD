

/*
 * RVM processor status and control register definitions.
 *
 * Based on the RVM CPU v0000 control-register definitions supplied
 * by Rene W. Olsen (revision 1.0, 24-Oct-2025).
 *
 * The interrupt enable is CR bit 2, NOT a flag in SR.
 */

#ifndef _RVM_PSL_H_
#define _RVM_PSL_H_

/* SR condition/status flags (bits 0..15). */
#define PSL_C				 ( 1ULL << 0 ) /* Carry */
#define PSL_V				 ( 1ULL << 1 ) /* Overflow */
#define PSL_Z				 ( 1ULL << 2 ) /* Zero */
#define PSL_N				 ( 1ULL << 3 ) /* Negative */
#define PSL_X				 ( 1ULL << 4 ) /* Extend */
#define PSL_S				 ( 1ULL << 5 ) /* Supervisor */
#define PSL_H				 ( 1ULL << 6 ) /* Halted */

#define PSL_ALLCC			 ( PSL_X | PSL_N | PSL_Z | PSL_V | PSL_C )
#define PSL_FLAG_MASK		 0x000000ffULL

/* SR interrupt level: bits 16..21 (current VMEmu status mask: 6 bits). */
#define PSL_IPL_SHIFT		 16
#define PSL_IPL_MASK		 0x003f0000ULL
#define PSL_IPL( sr )		 ( ( ( sr ) & PSL_IPL_MASK ) >> PSL_IPL_SHIFT )

/* A clear supervisor bit indicates execution in user mode. */
#define USERMODE( sr )		 ( ( ( sr ) & PSL_S ) == 0 )
#define SUPERVISORMODE( sr ) ( ( ( sr ) & PSL_S ) != 0 )

/* CR enable bits. Unlike earlier documentation, there is no SR I flag. */
#define RVM_CR_DATACACHE	 ( 1ULL << 0 )
#define RVM_CR_INSTCACHE	 ( 1ULL << 1 )
#define RVM_CR_INTERRUPT	 ( 1ULL << 2 )
#define RVM_CR_MMU			 ( 1ULL << 3 )
#define RVM_CR_TLB             ( 1ULL << 4 )
#define RVM_CR_FLAG_MASK	 0x0000001fULL

/* Control-register selectors for MFCtrl/MTCtrl. */
#define RVM_CREG_SR			 0
#define RVM_CREG_PC			 1
#define RVM_CREG_CR			 2
#define RVM_CREG_USP		 3
#define RVM_CREG_SSP		 4
#define RVM_CREG_VBR		 5
#define RVM_CREG_MST		 6
#define RVM_CREG_MUT		 7
#define RVM_CREG_INST		 8
#define RVM_CREG_INTE		 9
#define RVM_CREG_UD1		 10
#define RVM_CREG_UD2		 11

/* RTE restores the saved interrupt level and SR, per VMEmu behavior.
 * Bootstrap spl*() masks the asynchronous clock through CR at IPL_SCHED.
 * User-mode and nested device interrupt integration remain pending.
 */

#endif /* _RVM_PSL_H_ */
