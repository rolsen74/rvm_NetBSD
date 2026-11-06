/* Boundaries around real MI sleepq/CV algorithms; UP system LWPs only. */
const char rvm_wait_bad[] = "Invalid bootstrap MI sleep queue operation";
sleeptab_t sleeptab __attribute__((section(".data"))) = { 0 };
sleepqlock_t sleepq_locks[SLEEPTAB_HASH_SIZE] __attribute__((section(".data"))) = { 0 };
static unsigned int wait_initialized __attribute__((section(".data"))) = 0;
static kcondvar_t *cvs[512] __attribute__((section(".data"))) = { 0 };
static struct lwp *timed_lwps[17] __attribute__((section(".data"))) = { 0 };

static unsigned int wait_guard(void)
{
	if (curlwp == NULL) {
		if (rvm_boot_get_cr() != RVM_CR_MMU) panic(rvm_wait_bad);
		return UINT_MAX;
	}
	return rvm_intr_suspend();
}
static void wait_unguard(unsigned int guard)
{ if (guard != UINT_MAX) rvm_intr_resume(guard); }
static int cv_slot(kcondvar_t *cv)
{
	unsigned int i;
	for (i = 0; i < __arraycount(cvs); i++) if (cvs[i] == cv) return (int)i;
	return -1;
}
static void cv_checked(kcondvar_t *cv)
{
	if (cv == NULL || cv_slot(cv) == -1 || !rvm_mi_cv_is_valid(cv)) panic(rvm_wait_bad);
}
int rvm_wait_ready(void) { return wait_initialized; }
int rvm_wait_lock_valid(const struct kmutex *lock)
{
	unsigned int i;
	if (!wait_initialized) return 0;
	for (i = 0; i < SLEEPTAB_HASH_SIZE; i++) if (lock == &sleepq_locks[i].lock) return 1;
	return 0;
}
int rvm_wait_blocking(void)
{
	return wait_initialized && curlwp != NULL && curlwp->l_stat == LSSLEEP &&
	    curlwp->l_wmesg != NULL && (curlwp->l_syncobj == &cv_syncobj ||
	    curlwp->l_syncobj == &sleep_syncobj || curlwp->l_syncobj == &kpause_syncobj);
}
static void wait_context(void)
{
	if (!wait_initialized || !rvm_wait_scheduler_ready() || rvm_boot_get_cr() != RVM_CR_MMU ||
	    cpu_intr_p() || rvm_bootclock_busy() ||
	    (!rvm_async_clock_active() && !rvm_clock_active()) || curcpu()->ci_cpl != IPL_NONE ||
	    curlwp == NULL || curlwp->l_stat != LSONPROC || curlwp->l_nopreempt != 0 ||
	    curlwp->l_blcnt != 0 || curlwp->l_lwpctl != NULL || curlwp->l_target_cpu != NULL ||
	    (curlwp->l_pflag & (LP_INTR | LP_PREEMPTING)) ||
	    (curlwp->l_flag & LW_IDLE) || !(curlwp->l_flag & LW_SYSTEM) ||
	    curlwp->l_wchan != NULL || curlwp->l_sleepq != NULL) panic(rvm_wait_bad);
}
static void wait_timeout_prepare(struct lwp *l)
{
	unsigned int i;
	for (i = 0; i < __arraycount(timed_lwps); i++) if (timed_lwps[i] == l) return;
	for (i = 0; i < __arraycount(timed_lwps); i++) if (timed_lwps[i] == NULL) break;
	if (i == __arraycount(timed_lwps)) panic(rvm_wait_bad);
	callout_init(&l->l_timeout_ch, CALLOUT_MPSAFE);
	callout_setfunc(&l->l_timeout_ch, rvm_mi_sleepq_timeout, l);
	timed_lwps[i] = l;
	l->l_syncobj = &sched_syncobj;
}
void rvm_wait_lwp_fini(struct lwp *l)
{
	unsigned int i;
	for (i = 0; i < __arraycount(timed_lwps); i++) if (timed_lwps[i] == l) {
		if (l->l_wchan != NULL || l->l_sleepq != NULL || callout_pending(&l->l_timeout_ch))
			panic(rvm_wait_bad);
		callout_halt(&l->l_timeout_ch, NULL); callout_destroy(&l->l_timeout_ch);
		timed_lwps[i] = NULL;
	}
}
struct lwp *syncobj_noowner(wchan_t chan __unused) { return NULL; }
static void syncobj_init(const struct syncobj *object, const char *name, u_int flags,
    void (*unsleep)(struct lwp *, bool))
{
	struct syncobj *s = __UNCONST(object);
	memset(s, 0, sizeof(*s)); strlcpy(s->sobj_name, name, sizeof(s->sobj_name));
	s->sobj_flag = flags; s->sobj_boostpri = PRI_KERNEL;
	s->sobj_unsleep = unsleep; s->sobj_changepri = rvm_mi_sleepq_changepri;
	s->sobj_lendpri = rvm_mi_sleepq_lendpri; s->sobj_owner = syncobj_noowner;
}
int rvm_wait_init(void)
{
	unsigned int i, guard = wait_guard();
	if (!wait_initialized) {
		for (i = 0; i < SLEEPTAB_HASH_SIZE; i++) {
			mutex_init(&sleepq_locks[i].lock, MUTEX_DEFAULT, IPL_SCHED);
			rvm_mi_sleepq_init(&sleeptab.st_queue[i]);
		}
		syncobj_init(&cv_syncobj, "cv", SOBJ_SLEEPQ_SORTED, rvm_cv_unsleep);
		syncobj_init(&sleep_syncobj, "sleep", SOBJ_SLEEPQ_SORTED, sleepq_unsleep);
		syncobj_init(&kpause_syncobj, "kpause", SOBJ_SLEEPQ_NULL, sleepq_unsleep);
		syncobj_init(&sched_syncobj, "sched", SOBJ_SLEEPQ_SORTED, sleepq_unsleep);
		syncobj_init(&callout_syncobj, "callout", SOBJ_SLEEPQ_SORTED, sleepq_unsleep);
		wait_initialized = 1;
	}
	wait_unguard(guard); return 1;
}
void rvm_wait_unsleep(struct lwp *l, unsigned int unlock)
{
	if (l == NULL || l->l_wchan == NULL || !mutex_owned(l->l_mutex) ||
	    (l->l_syncobj != &cv_syncobj && l->l_syncobj != &sleep_syncobj &&
	    l->l_syncobj != &kpause_syncobj)) panic(rvm_wait_bad);
	(*l->l_syncobj->sobj_unsleep)(l, unlock != 0);
}
void cv_init(kcondvar_t *cv, const char *name)
{
	unsigned int guard = wait_guard(); int slot;
	if (cv == NULL || name == NULL || cv_slot(cv) != -1 || (slot = cv_slot(NULL)) == -1)
		panic(rvm_wait_bad);
	rvm_mi_cv_init(cv, name); cvs[slot] = cv; wait_unguard(guard);
}
void cv_destroy(kcondvar_t *cv)
{
	unsigned int guard = wait_guard(); cv_checked(cv);
	if (rvm_mi_cv_has_waiters(cv)) panic(rvm_wait_bad);
	cvs[cv_slot(cv)] = NULL; rvm_mi_cv_destroy(cv);
	cv->cv_opaque[0] = NULL; cv->cv_opaque[1] = NULL; wait_unguard(guard);
}
void cv_signal(kcondvar_t *cv)
{ unsigned int g = wait_guard(); cv_checked(cv); rvm_mi_cv_signal(cv); wait_unguard(g); }
void cv_broadcast(kcondvar_t *cv)
{ unsigned int g = wait_guard(); cv_checked(cv); rvm_mi_cv_broadcast(cv); wait_unguard(g); }
bool cv_has_waiters(kcondvar_t *cv)
{ unsigned int g = wait_guard(); bool result; cv_checked(cv); result = rvm_mi_cv_has_waiters(cv); wait_unguard(g); return result; }
bool cv_is_valid(kcondvar_t *cv)
{ unsigned int g = wait_guard(); bool result = cv != NULL && cv_slot(cv) != -1 && rvm_mi_cv_is_valid(cv); wait_unguard(g); return result; }
static void cv_wait_context(kcondvar_t *cv, kmutex_t *lock, int ticks)
{
	cv_checked(cv); wait_context();
	if (lock == NULL || lock->mtx_ipl != IPL_NONE || !mutex_owned(lock) ||
	    curcpu()->ci_irq_locks != 1 || ticks < 0) panic(rvm_wait_bad);
	wait_timeout_prepare(curlwp);
}
void cv_wait(kcondvar_t *cv, kmutex_t *lock)
{ unsigned int g = wait_guard(); cv_wait_context(cv, lock, 0); rvm_mi_cv_wait(cv, lock); wait_unguard(g); }
int cv_timedwait(kcondvar_t *cv, kmutex_t *lock, int ticks)
{ unsigned int g = wait_guard(); int result; cv_wait_context(cv, lock, ticks); result = rvm_mi_cv_timedwait(cv, lock, ticks); wait_unguard(g); return result; }
int cv_wait_sig(kcondvar_t *cv __unused, kmutex_t *lock __unused)
{ panic(rvm_wait_bad); }
int rvm_kpause(const char *message, unsigned int intr, int ticks, kmutex_t *lock)
{
	unsigned int g = wait_guard(); int result;
	wait_context();
	if (intr || ticks <= 0 || message == NULL ||
	    (lock != NULL && (lock->mtx_ipl != IPL_NONE || !mutex_owned(lock))) ||
	    curcpu()->ci_irq_locks != (lock == NULL ? 0U : 1U)) panic(rvm_wait_bad);
	wait_timeout_prepare(curlwp);
	result = rvm_mi_kpause(message, 0, ticks, lock);
	wait_unguard(g); return result;
}
int mtsleep(wchan_t channel, pri_t priority, const char *message, int ticks, kmutex_t *lock)
{
	unsigned int g = wait_guard(); int result;
	wait_context();
	if (channel == NULL || message == NULL || ticks < 0 || (priority & PCATCH) ||
	    lock == NULL || lock->mtx_ipl != IPL_NONE || !mutex_owned(lock) ||
	    curcpu()->ci_irq_locks != 1) panic(rvm_wait_bad);
	wait_timeout_prepare(curlwp);
	result = rvm_mi_mtsleep(channel, priority, message, ticks, lock);
	wait_unguard(g); return result;
}
void wakeup(wchan_t channel)
{
	unsigned int g = wait_guard();
	if (wait_initialized) {
		if (channel == NULL || cpu_intr_p()) panic(rvm_wait_bad);
		rvm_mi_wakeup(channel);
	}
	wait_unguard(g);
}
void sleepq_init(sleepq_t *sq) { rvm_mi_sleepq_init(sq); }
void rvm_sleepq_remove(sleepq_t *sq, lwp_t *l, unsigned int wake)
{ rvm_mi_sleepq_remove(sq, l, wake); }
void rvm_sleepq_unsleep(lwp_t *l, unsigned int unlock)
{ rvm_mi_sleepq_unsleep(l, unlock); }
void rvm_sleepq_enqueue(sleepq_t *sq, wchan_t channel, const char *message,
    syncobj_t *object, unsigned int catch_p)
{
	if (catch_p) panic(rvm_wait_bad);
	rvm_mi_sleepq_enqueue(sq, channel, message, object, 0);
}
int rvm_sleepq_block(int ticks, unsigned int catch_p, syncobj_t *object, int nlocks)
{
	if (catch_p || ticks < 0 || nlocks != 0) panic(rvm_wait_bad);
	return rvm_mi_sleepq_block(ticks, 0, object, 0);
}
void sleepq_wake(sleepq_t *sq, wchan_t channel, u_int count, kmutex_t *lock)
{ rvm_mi_sleepq_wake(sq, channel, count, lock); }
void sleepq_timeout(void *arg) { rvm_mi_sleepq_timeout(arg); }
void sleepq_changepri(lwp_t *l, pri_t pri) { rvm_mi_sleepq_changepri(l, pri); }
void sleepq_lendpri(lwp_t *l, pri_t pri) { rvm_mi_sleepq_lendpri(l, pri); }
int sleepq_enter(sleepq_t *sq, lwp_t *l, kmutex_t *lock)
{ return rvm_mi_sleepq_enter(sq, l, lock); }

int cv_timedwaitbt(kcondvar_t *cv, kmutex_t *lock, struct bintime *bt, const struct bintime *epsilon)
{
	unsigned int g = wait_guard(); int result;
	cv_wait_context(cv, lock, 0);
	if (bt == NULL || epsilon == NULL || bt->sec < 0 || epsilon->sec < 0) panic(rvm_wait_bad);
	result = rvm_mi_cv_timedwaitbt(cv, lock, bt, epsilon);
	wait_unguard(g); return result;
}
int cv_timedwait_sig(kcondvar_t *cv __unused, kmutex_t *lock __unused, int ticks __unused)
{ panic(rvm_wait_bad); }
int cv_timedwaitbt_sig(kcondvar_t *cv __unused, kmutex_t *lock __unused,
    struct bintime *bt __unused, const struct bintime *epsilon __unused)
{ panic(rvm_wait_bad); }
