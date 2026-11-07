/* Uniprocessor IPL/interrupt guards used by normal MI kernel services. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <machine/asyncirq.h>
#include <machine/boot.h>
#include <machine/irq.h>
#include <machine/psl.h>

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
	uint64_t cr = rvm_intr_disable();
	struct cpu_info *ci = curcpu();

	if (ci->ci_cpl < IPL_SCHED && ci->ci_intr_depth == 0 &&
	    ci->ci_mtx_count == 0 &&
	    (curlwp == NULL || curlwp->l_md.md_irq_hold == 0))
		rvm_irq_set_cr(cr | RVM_CR_INTERRUPT);
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
	KASSERT(ci == curcpu());
	atomic_or_uint(&ci->ci_want_resched, flags);
	if (l != NULL) l->l_md.md_astpending = 1;
}

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
	/* Keep IRQs live while idle. A HALT transition needs an atomic
	 * pending-work check/return protocol before it can replace this loop.
	 */
	rvm_intr_apply();
	while (curcpu()->ci_want_resched == 0)
		__asm volatile("" ::: "memory");
}
