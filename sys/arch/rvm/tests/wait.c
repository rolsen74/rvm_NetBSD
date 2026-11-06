/* Real MI condition queue ordering, timeout races and lifetime under IRQs. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/syncobj.h>
#include <sys/callout.h>
#include <sys/errno.h>
#include <uvm/uvm.h>
#include <machine/asyncirq.h>
#include <machine/bootwait.h>
#include <machine/bootsched.h>
const char w_bad[] = "MI sleep queue regression at line 0x";
const char w_ok[] = "RVM MI CV ordering, broadcast, timeout reuse and wait channels verified.\n";
const char w_name[] = "wait-test";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(w_bad); rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp workers[4] __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static kmutex_t lock __attribute__((section(".data"))) = { 0 };
static kcondvar_t cv __attribute__((section(".data"))) = { 0 };
static volatile unsigned int waiting __attribute__((section(".data"))) = 0;
static unsigned int completed __attribute__((section(".data"))) = 0;
static unsigned char channels[256] __aligned(256) __attribute__((section(".data"))) = { 0 };
static unsigned int woke __attribute__((section(".data"))) = 0;
static unsigned int order[4] __attribute__((section(".data"))) = { 0 };
static void waiter(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	volatile uint64_t canary = 0x434f4e4457414900UL + id;
	mutex_enter(&lock); waiting++;
	cv_wait(&cv, &lock);
	REQUIRE(mutex_owned(&lock) && canary == 0x434f4e4457414900UL + id);
	REQUIRE(curlwp->l_sleepq == NULL && curlwp->l_wchan == NULL);
	order[completed++] = id;
	mutex_exit(&lock); rvm_bootsched_finish();
}
static void signal_cv(void *arg)
{
	REQUIRE(arg == &cv && !cpu_intr_p());
	mutex_enter(&lock); cv_signal(&cv); mutex_exit(&lock);
}
static void channel_waiter(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	mutex_enter(&lock);
	REQUIRE(mtsleep(&channels[id == 1 ? 1 : 0], 0, w_name, 0, &lock) == 0);
	REQUIRE(mutex_owned(&lock)); woke |= 1U << id;
	mutex_exit(&lock); rvm_bootsched_finish();
}
void rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int i, guard, rounds;
	int64_t pages;
	callout_t signal;
	rvm_normal_early_init(bi); REQUIRE(rvm_boot_stage == 32);
	REQUIRE(rvm_cv_verify() && rvm_cv_timeout_verify() && rvm_kpause_verify() && rvm_wchan_verify());
	REQUIRE(rvm_cv_bintime_verify());
	pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(NULL, NULL) && rvm_wait_init());
	mutex_init(&lock, MUTEX_DEFAULT, IPL_NONE); cv_init(&cv, w_name);
	REQUIRE(rvm_async_clock_start());
	guard = rvm_intr_suspend();
	for (i = 0; i < 4; i++) {
		vaddr_t stack = uvm_uarea_system_alloc(curcpu()); REQUIRE(stack != 0);
		workers[i].l_cpu = curcpu(); workers[i].l_proc = &process;
		workers[i].l_stat = LSIDL; workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		workers[i].l_priority = PRI_KERNEL + (i == 0 ? 2 : (i == 3 ? 1 : 4));
		uvm_lwp_setuarea(&workers[i], stack);
		uvm_lwp_fork(curlwp, &workers[i], NULL, 0, waiter, (void *)(uintptr_t)i);
		REQUIRE(rvm_bootsched_ready(&workers[i])); rvm_bootsched_yield();
		REQUIRE(waiting == i + 1 && workers[i].l_stat == LSSLEEP);
	}
	/* Reinsert a waiter after lending a priority above the existing head. */
	rvm_wait_lwp_lock(&workers[3]);
	(*workers[3].l_syncobj->sobj_lendpri)(&workers[3], PRI_KERNEL + 8);
	rvm_wait_lwp_unlock(&workers[3]);
	mutex_enter(&lock); REQUIRE(cv_has_waiters(&cv)); cv_signal(&cv); mutex_exit(&lock);
	rvm_bootsched_yield(); REQUIRE(completed == 1 && order[0] == 3);
	mutex_enter(&lock); cv_signal(&cv); mutex_exit(&lock);
	rvm_bootsched_yield(); REQUIRE(completed == 2 && order[1] == 1);
	mutex_enter(&lock); cv_broadcast(&cv); REQUIRE(!cv_has_waiters(&cv)); mutex_exit(&lock);
	while (completed != 4) rvm_bootsched_yield();
	REQUIRE(order[2] == 2 && order[3] == 0);
	for (i = 0; i < 4; i++) REQUIRE(rvm_bootsched_reap(&workers[i]));
	/* Both wait channels hash to the same lock and queue. Wake only matches. */
	for (i = 0; i < 3; i++) {
		vaddr_t stack = uvm_uarea_system_alloc(curcpu()); REQUIRE(stack != 0);
		memset(&workers[i], 0, sizeof(workers[i]));
		workers[i].l_cpu = curcpu(); workers[i].l_proc = &process;
		workers[i].l_stat = LSIDL; workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		uvm_lwp_setuarea(&workers[i], stack);
		uvm_lwp_fork(curlwp, &workers[i], NULL, 0, channel_waiter, (void *)(uintptr_t)i);
		REQUIRE(rvm_bootsched_ready(&workers[i])); rvm_bootsched_yield();
		REQUIRE(workers[i].l_stat == LSSLEEP);
	}
	wakeup(&channels[0]); rvm_bootsched_yield();
	REQUIRE(woke == 5 && workers[1].l_stat == LSSLEEP && workers[1].l_wchan == &channels[1]);
	wakeup(&channels[1]); rvm_bootsched_yield(); REQUIRE(woke == 7);
	for (i = 0; i < 3; i++) REQUIRE(rvm_bootsched_reap(&workers[i]));
	rvm_intr_resume(guard);
	callout_init(&signal, CALLOUT_MPSAFE);
	for (rounds = 0; rounds < 12; rounds++) {
		int result;
		if (rounds & 1) callout_reset(&signal, 1, signal_cv, &cv);
		mutex_enter(&lock);
		result = cv_timedwait(&cv, &lock, (rounds & 1) ? 5 : 2);
		REQUIRE(result == ((rounds & 1) ? 0 : EWOULDBLOCK));
		REQUIRE(mutex_owned(&lock) && !cv_has_waiters(&cv) && !callout_pending(&curlwp->l_timeout_ch));
		mutex_exit(&lock); callout_halt(&signal, NULL);
	}
	callout_destroy(&signal);
	guard = rvm_intr_suspend(); REQUIRE(rvm_async_clock_stop());
	cv_destroy(&cv); REQUIRE(!cv_is_valid(&cv)); mutex_destroy(&lock);
	REQUIRE(rvm_bootsched_fini()); rvm_intr_resume(guard);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && kernel_map->nentries == 0);
	REQUIRE(curcpu()->ci_irq_locks == 0 && curlwp->l_md.md_irq_hold == 0);
	rvm_early_puts(w_ok);
}
