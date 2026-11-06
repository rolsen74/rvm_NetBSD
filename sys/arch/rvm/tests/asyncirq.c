/* Hardware interrupts during ordinary C and complete sleeping-LWP lifecycle. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/mutex.h>
#include <sys/kmem.h>
#include <sys/callout.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <uvm/uvm.h>
#include <machine/asyncirq.h>
#include <machine/clock.h>
#include <machine/psl.h>
#include <machine/bootsched.h>
const char ai_bad[] = "Asynchronous clock regression at line 0x";
const char ai_ok[] = "RVM asynchronous masks, callbacks, preemption and sleeping LWPs verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(ai_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp workers[3] __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static volatile unsigned int callbacks __attribute__((section(".data"))) = 0;
static volatile unsigned int worker_callbacks[3] __attribute__((section(".data"))) = { 0 };
static volatile unsigned int done[3] __attribute__((section(".data"))) = { 0 };
static void mark(void *arg)
{
	REQUIRE(arg == (void *)&callbacks && !cpu_intr_p() && curcpu()->ci_cpl == IPL_NONE);
	REQUIRE(rvm_boot_get_cr() == RVM_CR_MMU && curlwp->l_md.md_irq_hold != 0);
	REQUIRE(!rvm_async_clock_stop());
	callbacks++;
}
static void worker_timeout(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	REQUIRE(id < 3 && !cpu_intr_p() && curcpu()->ci_cpl == IPL_NONE);
	worker_callbacks[id]++;
}
static void worker(void *arg)
{
	unsigned int id = (uintptr_t)arg, i;
	callout_t timeout;
	struct timespec ts;
	size_t size = PAGE_SIZE + 73 + id * 8;
	volatile uint64_t canary = 0x4153594e43495200UL + id;
	unsigned char *buf;
	buf = kmem_zalloc(size, KM_NOSLEEP);
	REQUIRE(buf != NULL && buf[0] == 0 && buf[size - 1] == 0);
	buf[0] = id + 1; buf[size - 1] = id + 0x60;
	callout_init(&timeout, CALLOUT_MPSAFE);
	callout_reset(&timeout, 3, worker_timeout, (void *)(uintptr_t)id);
	for (i = 0; i < 8; i++) {
		uint64_t deadline = rvm_clock_monotonic_ns() + (id + 1) * 17000000;
		REQUIRE(rvm_bootsched_sleep_until(deadline) == 1);
		REQUIRE(rvm_clock_monotonic_ns() >= deadline);
		REQUIRE(canary == 0x4153594e43495200UL + id && buf[0] == id + 1 && buf[size - 1] == id + 0x60);
		curlwp->l_tcgen = 17; nanouptime(&ts);
		REQUIRE(curlwp->l_tcgen == 17); curlwp->l_tcgen = 0;
		REQUIRE(rvm_rtc_read(&ts));
		preempt_point(); DELAY(2000);
	}
	REQUIRE(worker_callbacks[id] == 1);
	callout_stop(&timeout); callout_destroy(&timeout);
	kmem_free(buf, size);
	done[id] = 1;
	rvm_bootsched_finish();
}
void rvm_early_init(const struct rvm_bootinfo *bi)
{
	kmutex_t lock;
	callout_t callout;
	unsigned int i, guard, nested;
	int oldipl, ticks;
	uint64_t events, catchups;
	int64_t pages;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32 && rvm_async_irq_verify() && rvm_async_preempt_verify());
	REQUIRE(rvm_async_register_verify());
	pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	mutex_init(&lock, MUTEX_SPIN, IPL_VM);
	callout_init(&callout, CALLOUT_MPSAFE);
	callout_reset(&callout, 2, mark, (void *)&callbacks);
	REQUIRE(rvm_async_clock_start());
	guard = rvm_intr_suspend();
	REQUIRE(!rvm_async_clock_start() && !rvm_clock_start());
	rvm_intr_resume(guard);
	/* Lower logical IPL admits hardclock but defers softclock callbacks. */
	oldipl = splvm(); events = rvm_async_clock_events();
	DELAY(65000);
	REQUIRE(rvm_async_clock_events() > events && callbacks == 0);
	splx(oldipl); DELAY(30000);
	guard = rvm_intr_suspend(); REQUIRE(callbacks == 1);
	callout_stop(&callout); callout_destroy(&callout);
	rvm_intr_resume(guard);
	/* Even an IPL_VM checked lock masks the clock during its state update. */
	catchups = rvm_async_clock_catchups();
	mutex_enter(&lock); events = rvm_async_clock_events(); ticks = getticks();
	REQUIRE(!mutex_tryenter(&lock)); DELAY(65000);
	REQUIRE(rvm_async_clock_events() == events && getticks() == ticks);
	mutex_exit(&lock); DELAY(30000);
	guard = rvm_intr_suspend();
	REQUIRE(rvm_async_clock_events() > events && rvm_async_clock_catchups() > catchups);
	nested = rvm_intr_suspend(); events = rvm_async_clock_events();
	DELAY(30000); rvm_intr_resume(nested);
	REQUIRE(rvm_async_clock_events() == events && rvm_boot_get_cr() == RVM_CR_MMU);
	REQUIRE(rvm_async_clock_stop() && !rvm_async_clock_stop());
	mutex_destroy(&lock); rvm_intr_resume(guard);

	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(NULL, NULL));
	for (i = 0; i < 3; i++) {
		vaddr_t stack = uvm_uarea_system_alloc(curcpu()); REQUIRE(stack != 0);
		workers[i].l_cpu = curcpu(); workers[i].l_proc = &process;
		workers[i].l_stat = LSIDL; workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		uvm_lwp_setuarea(&workers[i], stack);
		uvm_lwp_fork(curlwp, &workers[i], NULL, 0, worker, (void *)(uintptr_t)i);
		REQUIRE(rvm_bootsched_ready(&workers[i]));
	}
	REQUIRE(rvm_async_clock_start()); rvm_async_preemption(1);
	while (workers[0].l_stat != LSZOMB || workers[1].l_stat != LSZOMB ||
	    workers[2].l_stat != LSZOMB) {
		uint64_t deadline = rvm_clock_monotonic_ns() + 27000000;
		REQUIRE(rvm_bootsched_sleep_until(deadline) == 1);
	}
	guard = rvm_intr_suspend();
	REQUIRE(rvm_async_clock_stop());
	for (i = 0; i < 3; i++) {
		REQUIRE(done[i] && workers[i].l_stat == LSZOMB && workers[i].l_rtime.frac != 0);
		REQUIRE(rvm_bootsched_reap(&workers[i]));
	}
	REQUIRE(rvm_bootsched_fini()); rvm_intr_resume(guard);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && kernel_map->nentries == 0);
	REQUIRE(curcpu()->ci_irq_locks == 0 && curlwp->l_md.md_irq_hold == 0);
	rvm_early_puts(ai_ok);
}
