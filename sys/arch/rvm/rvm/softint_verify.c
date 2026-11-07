/* Boot smoke test: priority, coalescing, separate stacks and full cleanup. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/intr.h>
#include <uvm/uvm.h>
#include <machine/asyncirq.h>
#include <machine/bootsched.h>
#include <machine/bootsoftint.h>
static unsigned int count __attribute__((section(".data"))) = 0;
static unsigned int order[8] __attribute__((section(".data"))) = { 0 };
static struct lwp *controller __attribute__((section(".data"))) = NULL;
static unsigned int failed __attribute__((section(".data"))) = 0;
static void record(void *arg)
{
	if (count >= __arraycount(order) || curlwp == controller ||
	    !rvm_softint_member(curlwp) || cpu_intr_p() ||
	    !(curlwp->l_pflag & LP_INTR) || curcpu()->ci_cpl != IPL_NONE) {
		failed = 1; return;
	}
	order[count++] = (uintptr_t)arg;
}
int rvm_softint_verify(void)
{
	void *cookies[4];
	unsigned int i;
	int64_t pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_bootsched_init() || !rvm_bootsched_idle_init(NULL, NULL) ||
	    !rvm_softint_init()) return 0;
	controller = curlwp; count = failed = 0;
	for (i = 0; i < 4; i++) {
		cookies[i] = softint_establish(i | SOFTINT_MPSAFE, record, (void *)(uintptr_t)i);
		if (!cookies[i]) return 0;
		softint_schedule(cookies[i]); softint_schedule(cookies[i]);
	}
	rvm_bootsched_yield();
	if (failed || count != 4 || order[0] != SOFTINT_SERIAL || order[1] != SOFTINT_NET ||
	    order[2] != SOFTINT_BIO || order[3] != SOFTINT_CLOCK || rvm_softint_pending()) return 0;
	for (i = 0; i < 4; i++) softint_disestablish(cookies[i]);
	return rvm_softint_fini() && rvm_bootsched_fini() &&
	    curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages;
}

#include <sys/callout.h>
#include <sys/condvar.h>
#include <machine/bootclock.h>
#include <machine/bootwait.h>
static kmutex_t clock_lock __attribute__((section(".data"))) = { 0 };
static kcondvar_t clock_cv __attribute__((section(".data"))) = { 0 };
static callout_t clock_timer __attribute__((section(".data"))) = { 0 };
static unsigned int clock_count __attribute__((section(".data"))) = 0;
static unsigned int clock_failed __attribute__((section(".data"))) = 0;
const char rvm_softclock_label[] = "rvm-softclock";
static void clock_callback(void *arg)
{
	if (arg != &clock_timer || !cpu_softintr_p() || cpu_intr_p() ||
	    !rvm_softint_member(curlwp)) clock_failed = 1;
	mutex_enter(&clock_lock); clock_count++;
	cv_broadcast(&clock_cv); mutex_exit(&clock_lock);
	if (clock_count < 3) callout_reset(&clock_timer, 1, clock_callback, &clock_timer);
}
int rvm_softclock_verify(void)
{
	unsigned int guard;
	int64_t pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_bootsched_init() || !rvm_bootsched_idle_init(NULL, NULL) ||
	    !rvm_wait_init() || !rvm_softint_init()) return 0;
	mutex_init(&clock_lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&clock_cv, rvm_softclock_label); callout_init(&clock_timer, CALLOUT_MPSAFE);
	clock_count = clock_failed = 0;
	if (!rvm_callout_softint_start() || !rvm_async_clock_start()) return 0;
	guard = rvm_intr_suspend();
	callout_reset(&clock_timer, 1, clock_callback, &clock_timer);
	mutex_enter(&clock_lock);
	while (clock_count < 3) {
		if (cv_timedwait(&clock_cv, &clock_lock, 100) != 0) return 0;
	}
	mutex_exit(&clock_lock);
	if (!rvm_async_clock_stop()) return 0;
	callout_halt(&clock_timer, NULL); callout_destroy(&clock_timer);
	if (!rvm_callout_softint_stop()) return 0;
	cv_destroy(&clock_cv); mutex_destroy(&clock_lock);
	if (clock_failed || !rvm_softint_fini() || !rvm_bootsched_fini()) return 0;
	rvm_intr_resume(guard);
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages;
}
