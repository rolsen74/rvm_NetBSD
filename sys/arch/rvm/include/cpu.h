

/*
 * NetBSD/RVM - CPU-dependent kernel interfaces.
 *
 * Multi-processor bootstrap version.
 */

#ifndef _RVM_CPU_H_
#define _RVM_CPU_H_

#if defined( _KERNEL ) || defined( _KMEMUSER )

#include <sys/cpu_data.h>
#include <sys/device_if.h>

struct lwp;
struct proc;
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
	struct lwp *		  ci_curlwp; /* current lightweight process */
	struct cpu_data		  ci_data;	 /* NetBSD machine-independent CPU state */
	cpuid_t ci_cpuid;
	unsigned int ci_flags;
	volatile unsigned int ci_atomic_active;
	unsigned int ci_pmap_depth;
	device_t ci_dev;
	struct lwp *		  ci_onproc; /* currently running LWP */
	volatile unsigned int ci_intr_depth;
	int					  ci_cpl; /* current interrupt priority */
	unsigned int ci_want_resched;
	unsigned int ci_irq_locks; /* IRQ-off checked mutexes held on this CPU */
	int ci_mtx_oldspl;
	int ci_mtx_count; /* MI scheduler spin-lock handoff count */
	uint64_t ci_last_tick;
};

#endif /* _KERNEL || _KMEMUSER */

#ifdef _KERNEL

extern struct cpu_info cpu_info_primary;
#ifdef MULTIPROCESSOR
extern struct cpu_info cpu_info_secondary[MAXCPUS - 1];
extern unsigned int rvm_cpu_count;
extern volatile uint64_t rvm_secondary_arrived[MAXCPUS];
struct cpu_info *rvm_curcpu(void);
void cpu_boot_secondary_processors(void);
#endif
void cpu_signotify(struct lwp *);
void cpu_need_proftick(struct lwp *);
void cpu_proc_fork(struct proc *, struct proc *);
void *cpu_uarea_alloc(bool);
bool cpu_uarea_free(void *);

#ifdef MULTIPROCESSOR
#define curcpu() rvm_curcpu()
#else
#define curcpu() ( &cpu_info_primary )
#endif
#define curlwp	 ( curcpu ()->ci_curlwp )

static inline int
cpu_number ( void )
{
	return curcpu()->ci_cpuid;
}

#define CPU_IS_PRIMARY( ci ) ( ( ci ) == &cpu_info_primary )
#define CPU_INFO_ITERATOR	 int
#ifdef MULTIPROCESSOR
#define CPU_INFO_FOREACH( cii, ci ) \
	( cii ) = 0, ( ci ) = &cpu_info_primary; \
	( cii ) < rvm_cpu_count; \
	( cii )++, ( ci ) = ( cii ) == 0 ? &cpu_info_primary : \
	    &cpu_info_secondary[( cii ) - 1]
#else
#define CPU_INFO_FOREACH( cii, ci )                                                                                             \
	( cii ) = 0, ( ci ) = &cpu_info_primary;                                                                                    \
	( cii ) == 0;                                                                                                               \
	( cii )++
#endif

static inline bool
cpu_intr_p ( void )
{
	return curcpu ()->ci_intr_depth != 0;
}

/*
 * Supervisor timer entry/return and UP timer preemption are implemented.
 * TODO: user-mode/nested device interrupts and full kernel scheduler startup.
 * TODO: revisit CPU_INFO_FOREACH when SMP support is introduced.
 */

#endif /* _KERNEL */

#endif /* _RVM_CPU_H_ */
