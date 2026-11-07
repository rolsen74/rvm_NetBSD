#include <machine/asyncirq.h>
#include <machine/bootwait.h>
#include <machine/bootsoftint.h>
/* Included after the selected MI bodies; private layout stays in that unit. */
#define RVM_CALLOUT_MAX 64
static kmutex_t callout_lock_store __attribute__((section(".data"))) = { 0 };
static callout_t *registered[RVM_CALLOUT_MAX] __attribute__((section(".data"))) = { 0 };
static unsigned int initialized __attribute__((section(".data"))) = 0;
static unsigned int dispatching __attribute__((section(".data"))) = 0;
const char rvm_callout_bad[] = "Invalid bootstrap MI callout operation";

static int
slot(callout_t *cs)
{
	unsigned int i;
	for (i = 0; i < RVM_CALLOUT_MAX; i++) if (registered[i] == cs) return (int)i;
	return -1;
}
static void
callout_context(void)
{
	if (!initialized || rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() ||
	    (curcpu()->ci_cpl != IPL_NONE &&
	    !(curcpu()->ci_cpl == IPL_SCHED && rvm_wait_blocking())) || curlwp == NULL || curlwp != curcpu()->ci_onproc)
		panic(rvm_callout_bad);
}
static callout_impl_t *
checked(callout_t *cs)
{
	callout_impl_t *c = (void *)cs;
	callout_context();
	if (cs == NULL || slot(cs) == -1 || c->c_magic != CALLOUT_MAGIC ||
	    c->c_cpu != &callout_cpu0 || !(c->c_flags & CALLOUT_MPSAFE)) panic(rvm_callout_bad);
	return c;
}
int
rvm_callout_cpu_init(void)
{
	unsigned int i;
	if (initialized) return curcpu()->ci_data.cpu_callout == &callout_cpu0;
	if (curcpu()->ci_data.cpu_callout != NULL || rvm_boot_get_cr() != RVM_CR_MMU ||
	    cpu_intr_p() || curcpu()->ci_cpl != IPL_NONE) return 0;
	mutex_init(&callout_lock_store, MUTEX_SPIN, IPL_HIGH);
	callout_cpu0.cc_lock = &callout_lock_store;
	callout_cpu0.cc_cpu = curcpu();
	CIRCQ_INIT(&callout_cpu0.cc_todo);
	for (i = 0; i < BUCKETS; i++) CIRCQ_INIT(&callout_cpu0.cc_wheel[i]);
	callout_sih = &callout_cpu0;
	curcpu()->ci_data.cpu_callout = &callout_cpu0;
	initialized = 1;
	return 1;
}
int rvm_callout_busy(void) { return dispatching; }
void
rvm_callout_poll(void)
{
	callout_context();
	if (dispatching) panic(rvm_callout_bad);
	if (!soft_pending) return;
	dispatching = 1; soft_pending = 0;
	callout_softclock(NULL);
	dispatching = 0;
}
static void
masked_callout_init(callout_t *cs, u_int flags)
{
	int index;
	callout_context();
	if (cs == NULL || flags != CALLOUT_MPSAFE || slot(cs) != -1 ||
	    (index = slot(NULL)) == -1) panic(rvm_callout_bad);
	memset(cs, 0, sizeof(*cs));
	rvm_mi_callout_init(cs, flags);
	registered[index] = cs;
}
static void
masked_callout_destroy(callout_t *cs)
{
	callout_impl_t *c = checked(cs);
	int index = slot(cs);
	if ((c->c_flags & CALLOUT_PENDING) || callout_running_somewhere_else(c, &callout_cpu0))
		panic(rvm_callout_bad);
	rvm_mi_callout_destroy(cs); registered[index] = NULL;
}
static void
schedule_ok(callout_t *cs, int ticks)
{
	checked(cs);
	if (ticks <= 0) panic(rvm_callout_bad);
}
static void masked_callout_reset(callout_t *cs, int ticks, void (*fn)(void *), void *arg)
{
	schedule_ok(cs, ticks);
	if (fn == NULL) panic(rvm_callout_bad);
	rvm_mi_callout_reset(cs, ticks, fn, arg);
}
static void masked_callout_setfunc(callout_t *cs, void (*fn)(void *), void *arg)
{
	checked(cs);
	if (fn == NULL) panic(rvm_callout_bad);
	rvm_mi_callout_setfunc(cs, fn, arg);
}
static void masked_callout_schedule(callout_t *cs, int ticks)
{
	schedule_ok(cs, ticks);
	if (((callout_impl_t *)cs)->c_func == NULL) panic(rvm_callout_bad);
	rvm_mi_callout_schedule(cs, ticks);
}
static bool masked_callout_stop(callout_t *cs) { checked(cs); return rvm_mi_callout_stop(cs); }
static bool masked_callout_pending(callout_t *cs) { checked(cs); return rvm_mi_callout_pending(cs); }
static bool masked_callout_active(callout_t *cs) { checked(cs); return rvm_mi_callout_active(cs); }
static bool masked_callout_expired(callout_t *cs) { checked(cs); return rvm_mi_callout_expired(cs); }
static bool masked_callout_invoking(callout_t *cs) { checked(cs); return rvm_mi_callout_invoking(cs); }
static void masked_callout_ack(callout_t *cs) { checked(cs); rvm_mi_callout_ack(cs); }
void
callout_hardclock(void)
{
	if (!initialized || !cpu_intr_p() || curcpu()->ci_cpl != IPL_HIGH ||
	    rvm_boot_get_cr() != RVM_CR_MMU) panic(rvm_callout_bad);
	rvm_mi_callout_hardclock();
}

/* Public callout operations remain usable with the asynchronous clock enabled.
 * Keep the guard through validation, wheel updates and ownership bookkeeping. */
void callout_init(callout_t *c, u_int flags)
{ unsigned int g = rvm_intr_suspend(); masked_callout_init(c, flags); rvm_intr_resume(g); }
void callout_destroy(callout_t *c)
{ unsigned int g = rvm_intr_suspend(); masked_callout_destroy(c); rvm_intr_resume(g); }
void callout_reset(callout_t *c, int ticks, void (*fn)(void *), void *arg)
{ unsigned int g = rvm_intr_suspend(); masked_callout_reset(c, ticks, fn, arg); rvm_intr_resume(g); }
void callout_setfunc(callout_t *c, void (*fn)(void *), void *arg)
{ unsigned int g = rvm_intr_suspend(); masked_callout_setfunc(c, fn, arg); rvm_intr_resume(g); }
void callout_schedule(callout_t *c, int ticks)
{ unsigned int g = rvm_intr_suspend(); masked_callout_schedule(c, ticks); rvm_intr_resume(g); }
void callout_ack(callout_t *c)
{ unsigned int g = rvm_intr_suspend(); masked_callout_ack(c); rvm_intr_resume(g); }
bool callout_stop(callout_t *c)
{ unsigned int g = rvm_intr_suspend(); bool result = masked_callout_stop(c); rvm_intr_resume(g); return result; }
bool callout_pending(callout_t *c)
{ unsigned int g = rvm_intr_suspend(); bool result = masked_callout_pending(c); rvm_intr_resume(g); return result; }
bool callout_active(callout_t *c)
{ unsigned int g = rvm_intr_suspend(); bool result = masked_callout_active(c); rvm_intr_resume(g); return result; }
bool callout_expired(callout_t *c)
{ unsigned int g = rvm_intr_suspend(); bool result = masked_callout_expired(c); rvm_intr_resume(g); return result; }
bool callout_invoking(callout_t *c)
{ unsigned int g = rvm_intr_suspend(); bool result = masked_callout_invoking(c); rvm_intr_resume(g); return result; }

bool callout_halt(callout_t *c, void *interlock)
{
	unsigned int g = rvm_intr_suspend(); bool result;
	checked(c);
	if (dispatching || interlock != NULL) panic(rvm_callout_bad);
	result = rvm_mi_callout_halt(c, interlock);
	rvm_intr_resume(g); return result;
}

/* Optional normal softclock context on a dedicated generic softint LWP. */
static void softclock_dispatch(void *arg)
{
	unsigned int g = rvm_intr_suspend();
	callout_context();
	if (arg != &callout_cpu0 || real_softclock == NULL || dispatching ||
	    !cpu_softintr_p()) panic(rvm_callout_bad);
	dispatching = 1;
	callout_softclock(NULL);
	dispatching = 0;
	rvm_intr_resume(g);
}
int rvm_callout_softint_start(void)
{
	unsigned int g = rvm_intr_suspend();
	callout_context();
	if (!rvm_softint_ready() || real_softclock != NULL || dispatching || cpu_softintr_p()) {
		rvm_intr_resume(g); return 0;
	}
	rvm_callout_poll();
	real_softclock = softint_establish(SOFTINT_CLOCK | SOFTINT_MPSAFE,
	    softclock_dispatch, &callout_cpu0);
	rvm_intr_resume(g); return real_softclock != NULL;
}
int rvm_callout_softint_stop(void)
{
	unsigned int g = rvm_intr_suspend();
	callout_context();
	if (real_softclock == NULL || dispatching || cpu_softintr_p()) {
		rvm_intr_resume(g); return 0;
	}
	softint_disestablish(real_softclock); real_softclock = NULL;
	rvm_intr_resume(g); return 1;
}
