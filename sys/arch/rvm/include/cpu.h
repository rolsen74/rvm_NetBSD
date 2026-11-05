

/*
 * NetBSD/RVM - CPU-dependent kernel interfaces.
 *
 * Initial single-processor bootstrap version.
 * This is a porting scaffold, not a complete NetBSD CPU implementation.
 */

#ifndef _RVM_CPU_H_
#define _RVM_CPU_H_

#if defined( _KERNEL ) || defined( _KMEMUSER )

#include <sys/cpu_data.h>

struct lwp;
struct trapframe;

/*
 * Values are populated by the timer/exception entry path.
 * Using an explicit user-mode flag avoids assuming an RVM status-bit layout.
 */
struct clockframe
{
	vaddr_t cf_pc;
	int		cf_usermode;
	int		cf_intr_depth;
};

#define CLKF_USERMODE( cf ) ( ( cf )->cf_usermode != 0 )
#define CLKF_PC( cf )		( ( cf )->cf_pc )
#define CLKF_INTR( cf )		( ( cf )->cf_intr_depth > 1 )

struct cpu_info
{
	struct cpu_data		  ci_data;	 /* NetBSD machine-independent CPU state */
	struct lwp *		  ci_curlwp; /* current lightweight process */
	struct lwp *		  ci_onproc; /* currently running LWP */
	volatile unsigned int ci_intr_depth;
	int					  ci_cpl; /* current interrupt priority */
	int					  ci_want_resched;
};

#endif /* _KERNEL || _KMEMUSER */

#ifdef _KERNEL

/*
 * One CPU for now.  SMP and per-CPU register access can be added later.
 * Define cpu_info_primary in machdep.c (or a future cpu.c).
 */
extern struct cpu_info cpu_info_primary;

#define curcpu() ( &cpu_info_primary )
#define curlwp	 ( curcpu ()->ci_curlwp )

static inline int
cpu_number ( void )
{
	return 0;
}

#define CPU_IS_PRIMARY( ci ) ( 1 )
#define CPU_INFO_ITERATOR	 int
#define CPU_INFO_FOREACH( cii, ci )                                                                                             \
	( cii ) = 0, ( ci ) = &cpu_info_primary;                                                                                    \
	( cii ) == 0;                                                                                                               \
	( cii )++

static inline int
cpu_intr_p ( void )
{
	return curcpu ()->ci_intr_depth != 0;
}

/*
 * TODO: provide interrupt priority/spl operations in machine/intr.h.
 * TODO: provide low-level idle, context switch and clock support.
 * TODO: revisit CPU_INFO_FOREACH when SMP support is introduced.
 */

#endif /* _KERNEL */

#endif /* _RVM_CPU_H_ */
