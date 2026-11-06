/* IRQ guards are per LWP; scheduler spin-lock holds remain per CPU. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <machine/psl.h>
#include <machine/asyncirq.h>
const char rvm_intr_bad[] = "Invalid RVM interrupt guard";
unsigned int
rvm_intr_suspend(void)
{
	uint64_t cr = rvm_intr_disable();
	unsigned int old;
	if ((cr != RVM_CR_MMU && cr != (RVM_CR_MMU | RVM_CR_INTERRUPT)) ||
	    curlwp == NULL || curlwp->l_cpu != curcpu() ||
	    curlwp->l_md.md_irq_hold == UINT_MAX) panic(rvm_intr_bad);
	old = curlwp->l_md.md_irq_hold++;
	return old;
}
void
rvm_intr_apply(void)
{
	rvm_intr_disable();
	if (rvm_async_clock_active() && rvm_async_clock_mask() != 0 &&
	    curlwp != NULL && curlwp == curcpu()->ci_onproc &&
	    curlwp->l_md.md_irq_hold == 0 && curcpu()->ci_irq_locks == 0 &&
	    curcpu()->ci_mtx_count == 0 && !cpu_intr_p() &&
	    curcpu()->ci_cpl < IPL_SCHED) {
		rvm_irq_set_inte(rvm_async_clock_mask());
		rvm_irq_set_cr(RVM_CR_MMU | RVM_CR_INTERRUPT);
	}
}
void
rvm_intr_resume(unsigned int old)
{
	rvm_intr_disable();
	if (curlwp == NULL || old == UINT_MAX || curlwp->l_md.md_irq_hold != old + 1)
		panic(rvm_intr_bad);
	curlwp->l_md.md_irq_hold = old;
	rvm_intr_apply();
}
