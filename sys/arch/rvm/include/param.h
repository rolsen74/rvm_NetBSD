/*
 * NetBSD/RVM - machine-dependent parameters.
 *
 * Initial bootstrap definitions for the 64-bit RVM virtual CPU.
 * Verify stack alignment and kernel stack layout against the final
 * RVM NetBSD ABI before enabling user processes.
 */

#ifndef _RVM_PARAM_H_
#define _RVM_PARAM_H_

#ifdef _KERNEL_OPT
#include "opt_param.h"
#endif

/* Architecture and machine names. */
#define _MACHINE		 rvm
#define MACHINE			 "rvm"
#define _MACHINE_ARCH	 rvm64
#define MACHINE_ARCH	 "rvm64"

/*
 * RVM MMU pages are 64 KiB.  NetBSD's MI page macros are derived
 * from these machine-dependent constants in <sys/param.h>.
 */
#define PGSHIFT			 16
#define NBPG			 ( 1UL << PGSHIFT )
#define PGOFSET			 ( NBPG - 1 )

/*
 * Initial kernel stack/u-area allocation: two pages (128 KiB).
 * This is a bootstrap choice, not a property of the RVM hardware.
 */
#define UPAGES			 2
#define USPACE			 ( UPAGES * NBPG )
#define USPACE_ALIGN	 NBPG
#define UAREA_PCB_OFFSET 0

/*
 * RVM GCC's STACK_BOUNDARY is 32 bits.  This describes the compiler ABI;
 * bootstrap stacks may be aligned more strictly.
 */
#define STACK_ALIGNBYTES ( 4UL - 1 )

/* Start with a uniprocessor VM. */
#define MAXCPUS			 1

/* Network mbuf sizes (must evenly divide NBPG). */
#define MSIZE			 512
#ifndef MCLSHIFT
#define MCLSHIFT 11
#endif
#define MCLBYTES ( 1 << MCLSHIFT )

#ifndef MSGBUFSIZE
#define MSGBUFSIZE ( 64 * 1024 )
#endif

/* Simple physical-page conversions. */
#define rvm_btop( x ) ( (unsigned long)( x ) >> PGSHIFT )
#define rvm_ptob( x ) ( (unsigned long)( x ) << PGSHIFT )

/*
 * Pending work elsewhere in the port:
 * - Choose a kernel virtual address layout in vmparam.h.
 * - Complete managed/user pmap support for 64 KiB hardware pages.
 * - Register an RVM machine ID if a format/interface requires one.
 * - Implement DELAY() when the virtual timer is available.
 */

#endif /* _RVM_PARAM_H_ */
