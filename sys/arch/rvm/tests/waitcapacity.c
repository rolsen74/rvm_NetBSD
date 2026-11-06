/* Every schedulable LWP retains an MI timeout and arms an MD deadline. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/errno.h>
#include <uvm/uvm.h>
#include <machine/bootwait.h>
#include <machine/bootsched.h>
#include <machine/asyncirq.h>
#include <machine/clock.h>
const char wc_bad[] = "Wait capacity regression at line 0x";
const char wc_ok[] = "RVM mixed MI/MD timeout capacity and full LWP cleanup verified.\n";
const char wc_name[] = "wait-capacity";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(wc_bad); rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp workers[16] __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static kmutex_t lock __attribute__((section(".data"))) = { 0 };
static kcondvar_t ready __attribute__((section(".data"))) = { 0 };
static kcondvar_t go __attribute__((section(".data"))) = { 0 };
static unsigned int arrived __attribute__((section(".data"))) = 0;
static unsigned int released __attribute__((section(".data"))) = 0;
static uint64_t deadline __attribute__((section(".data"))) = 0;
static void worker(void *arg)
{
	REQUIRE((uintptr_t)arg < 16);
	REQUIRE(kpause(wc_name, false, 1, NULL) == EWOULDBLOCK);
	mutex_enter(&lock); arrived++; cv_signal(&ready);
	while (!released) cv_wait(&go, &lock);
	mutex_exit(&lock);
	REQUIRE(rvm_bootsched_sleep_until(deadline) == 1);
	rvm_bootsched_finish();
}
void rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int i, guard;
	int64_t pages;
	rvm_normal_early_init(bi); REQUIRE(rvm_boot_stage == 32);
	pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(NULL, NULL) && rvm_wait_init());
	mutex_init(&lock, MUTEX_DEFAULT, IPL_NONE); cv_init(&ready, wc_name); cv_init(&go, wc_name);
	for (i = 0; i < 16; i++) {
		vaddr_t stack = uvm_uarea_system_alloc(curcpu());
		if (!stack) stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
		REQUIRE(stack != 0);
		workers[i].l_cpu = curcpu(); workers[i].l_proc = &process;
		workers[i].l_stat = LSIDL; workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		uvm_lwp_setuarea(&workers[i], stack);
		uvm_lwp_fork(curlwp, &workers[i], NULL, 0, worker, (void *)(uintptr_t)i);
		REQUIRE(rvm_bootsched_ready(&workers[i]));
	}
	REQUIRE(rvm_async_clock_start()); rvm_async_preemption(1);
	mutex_enter(&lock);
	while (arrived != 16) cv_wait(&ready, &lock);
	deadline = rvm_clock_monotonic_ns() + 1000000000;
	released = 1; cv_broadcast(&go); mutex_exit(&lock);
	REQUIRE(rvm_bootsched_sleep_until(deadline) == 1);
	for (;;) {
		unsigned int finished = 0;
		for (i = 0; i < 16; i++) finished += workers[i].l_stat == LSZOMB;
		if (finished == 16) break;
		rvm_bootsched_yield();
	}
	guard = rvm_intr_suspend(); REQUIRE(rvm_async_clock_stop());
	for (i = 0; i < 16; i++) REQUIRE(rvm_bootsched_reap(&workers[i]));
	cv_destroy(&ready); cv_destroy(&go); mutex_destroy(&lock);
	REQUIRE(rvm_bootsched_fini()); rvm_intr_resume(guard);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && kernel_map->nentries == 0);
	REQUIRE(curcpu()->ci_irq_locks == 0 && curlwp->l_md.md_irq_hold == 0);
	rvm_early_puts(wc_ok);
}
