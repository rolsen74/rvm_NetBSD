/* Uniprocessor IPL/interrupt guards used by normal MI kernel services. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/sched.h>
#ifdef MULTIPROCESSOR
#include <sys/ipi.h>
#include <sys/xcall.h>
#endif
#include <machine/asyncirq.h>
#include <machine/boot.h>
#include <machine/irq.h>
#include <machine/psl.h>
#include <machine/idle.h>

extern char rvm_idle_halt_pc[], rvm_idle_after_halt[];
static uint64_t idle_wakeups, idle_skips;
uint64_t rvm_idle_halt_wakeups(void) { return idle_wakeups; }
uint64_t rvm_idle_halt_skips(void) { return idle_skips; }

void
rvm_idle_irq_return(uint64_t *frame)
{
	if (frame[1] == (uintptr_t)rvm_idle_halt_pc || (frame[0] & PSL_H)) {
		KASSERT(!USERMODE(frame[0]) && curlwp != NULL && (curlwp->l_flag & LW_IDLE));
		KASSERT(frame[1] == (uintptr_t)rvm_idle_halt_pc ||
		    frame[1] == (uintptr_t)rvm_idle_after_halt);
		if (frame[0] & PSL_H)
			idle_wakeups++;
		else
			idle_skips++;
		/* An IRQ between enabling interrupts and HALT must skip the HALT.
		 * A wake from HALT must not restore the saved halted status at RTE. */
		frame[1] = (uintptr_t)rvm_idle_after_halt;
		frame[0] &= ~PSL_H;
	}
}

int
splraise(int ipl)
{
	int old;

	rvm_intr_disable();
	KASSERT(ipl >= IPL_NONE && ipl <= IPL_HIGH);
	old = curcpu()->ci_cpl;
	if (ipl > old) curcpu()->ci_cpl = ipl;
	rvm_intr_apply();
	return old;
}

void
splx(int ipl)
{
	rvm_intr_disable();
	KASSERT(ipl >= IPL_NONE && ipl <= IPL_HIGH);
	curcpu()->ci_cpl = ipl;
	rvm_intr_apply();
}
void spl0(void) { splx(IPL_NONE); }

unsigned int
rvm_intr_suspend(void)
{
	rvm_intr_disable();
	KASSERT(curlwp != NULL && curlwp->l_md.md_irq_hold != UINT_MAX);
	return curlwp->l_md.md_irq_hold++;
}

void
rvm_intr_apply(void)
{
	rvm_intr_disable();
	struct cpu_info *ci = curcpu();

	if (ci->ci_cpl < IPL_SCHED && ci->ci_intr_depth == 0 &&
	    ci->ci_mtx_count == 0 &&
	    (curlwp == NULL || curlwp->l_md.md_irq_hold == 0))
		__builtin_rvm_seti();
}

void
rvm_intr_resume(unsigned int previous)
{
	rvm_intr_disable();
	KASSERT(curlwp != NULL && curlwp->l_md.md_irq_hold == previous + 1);
	curlwp->l_md.md_irq_hold = previous;
	rvm_intr_apply();
}

void
cpu_need_resched(struct cpu_info *ci, struct lwp *l, int flags)
{
	atomic_or_uint(&ci->ci_want_resched, flags);
	if (l != NULL) l->l_md.md_astpending = 1;
}

#ifdef MULTIPROCESSOR
/* Bounded HALT and ICNT provide the wakeup; the handler polls each tick. */
void cpu_ipi(struct cpu_info *ci __unused) { }
void
xc_send_ipi(struct cpu_info *ci)
{
	if (ci != NULL) {
		atomic_or_uint(&ci->ci_flags, 1);
		return;
	}
	for (unsigned int i = 0; i < rvm_cpu_count; i++)
	{
		struct cpu_info *target = i == 0 ? &cpu_info_primary :
		    &cpu_info_secondary[i - 1];
		if (target != curcpu())
			atomic_or_uint(&target->ci_flags, 1);
	}
}
#endif

void
cpu_signotify(struct lwp *l)
{
	l->l_md.md_astpending = 1;
}

void
cpu_need_proftick(struct lwp *l)
{
	l->l_pflag |= LP_OWEUPC;
	cpu_signotify(l);
}

void
cpu_idle(void)
{
	void rvm_icnt_idle_ensure(void);
	rvm_intr_disable();
	KASSERT(CURCPU_IDLE_P() && (curlwp->l_flag & LW_IDLE));
	KASSERT(curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_intr_depth == 0 &&
	    curcpu()->ci_mtx_count == 0 && curlwp->l_md.md_irq_hold == 0);
#ifdef RVM_IDLE_SPIN_TEST
	/* Control kernel for comparing host CPU use with the same toolchain. */
	rvm_intr_apply();
	while (curcpu()->ci_want_resched == 0)
		__asm volatile("" ::: "memory");
#else
	/* The emulator advances ICNT during HALT and wakes us for its exception. */
	if (curcpu()->ci_want_resched == 0 && !sched_curcpu_runnable_p()) {
		rvm_icnt_idle_ensure();
		rvm_idle_halt();
	}
	rvm_intr_apply();
#endif
}
