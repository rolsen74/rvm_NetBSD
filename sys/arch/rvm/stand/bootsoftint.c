/* Lifecycle/validation around the unchanged MI generic softint dispatcher. */
#include <machine/bootwait.h>
#define RVM_SOFTHAND_MAX 64
static struct {
	struct cpu_info *cpu;
	softint_t interrupts[SOFTINT_COUNT];
	softhand_t handlers[RVM_SOFTHAND_MAX];
} storage __attribute__((section(".data"))) = { 0 };
__CTASSERT(offsetof(__typeof__(storage), handlers) == offsetof(softcpu_t, sc_hand));
static struct lwp soft_lwps[SOFTINT_COUNT] __attribute__((section(".data"))) = { 0 };
static struct proc soft_proc __attribute__((section(".data"))) = { 0 };
static unsigned int initialized __attribute__((section(".data"))) = 0;
const char rvm_softint_bad[] = "Invalid bootstrap MI software interrupt operation";

int rvm_softint_ready(void) { return initialized; }
int rvm_softint_pending(void) { return initialized && curcpu()->ci_data.cpu_softints != 0; }
int rvm_softint_member(struct lwp *l)
{
	unsigned int i;
	if (!initialized) return 0;
	for (i = 0; i < SOFTINT_COUNT; i++) if (l == &soft_lwps[i]) return 1;
	return 0;
}
static void context(void)
{
	if (rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() ||
	    curcpu()->ci_cpl != IPL_NONE || curcpu()->ci_irq_locks != 0 ||
	    curcpu()->ci_mtx_count != 0 || curlwp == NULL ||
	    curlwp != curcpu()->ci_onproc || rvm_softint_member(curlwp))
		panic(rvm_softint_bad);
}
static softhand_t *checked(void *cookie)
{
	uintptr_t offset = (uintptr_t)cookie;
	softhand_t *sh;
	if (!initialized || offset < offsetof(softcpu_t, sc_hand[1]) ||
	    offset >= sizeof(storage) ||
	    (offset - offsetof(softcpu_t, sc_hand)) % sizeof(softhand_t) != 0)
		panic(rvm_softint_bad);
	sh = (void *)((uint8_t *)&storage + offset);
	if (sh->sh_func == NULL) panic(rvm_softint_bad);
	return sh;
}
int rvm_softint_init(void)
{
	static const int ipls[] = { IPL_SOFTBIO, IPL_SOFTCLOCK, IPL_SOFTSERIAL, IPL_SOFTNET };
	const pri_t priorities[] = { PRI_SOFTBIO, PRI_SOFTCLOCK, PRI_SOFTSERIAL, PRI_SOFTNET };
	unsigned int i, guard = rvm_intr_suspend();
	vaddr_t stacks[SOFTINT_COUNT];
	context();
	if (initialized || !rvm_wait_scheduler_ready() || curcpu()->ci_data.cpu_softcpu != NULL ||
	    curcpu()->ci_data.cpu_softints != 0) { rvm_intr_resume(guard); return 0; }
	/* Allocate all stacks before publishing any interrupt LWP. */
	for (i = 0; i < SOFTINT_COUNT; i++) {
		stacks[i] = uvm_uarea_system_alloc(curcpu());
		if (!stacks[i]) {
			while (i != 0) uvm_uarea_system_free(stacks[--i]);
			rvm_intr_resume(guard); return 0;
		}
	}
	memset(&storage, 0, sizeof(storage)); memset(soft_lwps, 0, sizeof(soft_lwps));
	storage.cpu = curcpu();
	mutex_init(&softint_lock, MUTEX_DEFAULT, IPL_NONE);
	softint_max = RVM_SOFTHAND_MAX; softint_bytes = sizeof(storage);
	for (i = 0; i < SOFTINT_COUNT; i++) {
		softint_t *si = &storage.interrupts[i];
		struct lwp *l = &soft_lwps[i];
		SIMPLEQ_INIT(&si->si_q);
		si->si_cpu = curcpu(); si->si_lwp = l;
		si->si_ipl = ipls[i]; si->si_machdep = 1U << i;
		l->l_cpu = curcpu(); l->l_proc = &soft_proc; l->l_stat = LSIDL;
		l->l_flag = LW_SYSTEM; l->l_pflag = LP_MPSAFE | LP_INTR | LP_BOUND;
		l->l_priority = priorities[i]; l->l_private = si;
		uvm_lwp_setuarea(l, stacks[i]);
		uvm_lwp_fork(curlwp, l, NULL, 0, softint_thread, si);
		l->l_mutex = curcpu()->ci_schedstate.spc_lwplock;
	}
	curcpu()->ci_data.cpu_softcpu = &storage;
	initialized = 1;
	rvm_intr_resume(guard); return 1;
}
int rvm_softint_fini(void)
{
	unsigned int i, guard = rvm_intr_suspend();
	context();
	if (!initialized || curcpu()->ci_data.cpu_softints != 0) goto busy;
	for (i = 1; i < softint_max; i++) if (storage.handlers[i].sh_func != NULL) goto busy;
	for (i = 0; i < SOFTINT_COUNT; i++) {
		if (storage.interrupts[i].si_active || !SIMPLEQ_EMPTY(&storage.interrupts[i].si_q) ||
		    soft_lwps[i].l_stat != LSIDL || (soft_lwps[i].l_pflag & LP_RUNNING)) goto busy;
	}
	for (i = 0; i < SOFTINT_COUNT; i++) {
		cpu_lwp_free2(&soft_lwps[i]); uvm_lwp_exit(&soft_lwps[i]);
	}
	mutex_destroy(&softint_lock);
	curcpu()->ci_data.cpu_softcpu = NULL; initialized = 0;
	rvm_intr_resume(guard); return 1;
busy:
	rvm_intr_resume(guard); return 0;
}
void *softint_establish(u_int flags, void (*func)(void *), void *arg)
{
	unsigned int g = rvm_intr_suspend(); void *cookie;
	context();
	if (!initialized || func == NULL || (flags & SOFTINT_LVLMASK) >= SOFTINT_COUNT ||
	    (flags & ~SOFTINT_LVLMASK) != SOFTINT_MPSAFE) panic(rvm_softint_bad);
	cookie = rvm_mi_softint_establish(flags, func, arg);
	rvm_intr_resume(g); return cookie;
}
void softint_schedule(void *cookie)
{
	unsigned int g = rvm_intr_suspend();
	checked(cookie);
	rvm_mi_softint_schedule(cookie);
	rvm_intr_resume(g);
}
void softint_schedule_cpu(void *cookie, struct cpu_info *ci)
{
	if (ci != curcpu()) panic(rvm_softint_bad);
	softint_schedule(cookie);
}
void softint_disestablish(void *cookie)
{
	unsigned int g = rvm_intr_suspend();
	softhand_t *sh = checked(cookie);
	context();
	/* UP barrier: drain this level before freeing the handler record. */
	while (sh->sh_isr->si_active || (sh->sh_flags & SOFTINT_PENDING))
		rvm_bootsched_yield();
	mutex_enter(&softint_lock); sh->sh_func = NULL; mutex_exit(&softint_lock);
	rvm_intr_resume(g);
}
/* No user AST exists during supervisor-only bring-up. */
void cpu_signotify(struct lwp *l)
{
	if (l == NULL || !(l->l_flag & LW_SYSTEM)) panic(rvm_softint_bad);
}

bool cpu_softintr_p(void)
{
	return curlwp != NULL && (curlwp->l_pflag & LP_INTR) != 0;
}
