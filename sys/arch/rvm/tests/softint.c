/* Generic softint queue semantics and real timer -> softclock LWP delivery. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/intr.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <uvm/uvm.h>
#include <machine/asyncirq.h>
#include <machine/bootsched.h>
#include <machine/bootsoftint.h>
#include <machine/bootclock.h>
#include <machine/bootwait.h>
#include <machine/clock.h>
#include <machine/uarea.h>
const char si_bad[] = "Softint regression at line 0x";
const char si_ok[] = "RVM MI softint FIFO, reschedule, IRQ softclock, waits and rollback verified.\n";
const char si_label[] = "softint-test";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(si_bad); rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static unsigned int order[128] __attribute__((section(".data"))) = { 0 };
static unsigned int norder __attribute__((section(".data"))) = 0;
static unsigned int repeats __attribute__((section(".data"))) = 0;
static void *self_cookie __attribute__((section(".data"))) = NULL;
static struct lwp *controller __attribute__((section(".data"))) = NULL;
static callout_t timer __attribute__((section(".data"))) = { 0 };
static volatile unsigned int fired __attribute__((section(".data"))) = 0;
static unsigned int target __attribute__((section(".data"))) = 0;
static kmutex_t lock __attribute__((section(".data"))) = { 0 };
static kcondvar_t cv __attribute__((section(".data"))) = { 0 };
static void record(void *arg)
{
	volatile uint64_t canary = 0x534f4654494e5453UL;
	REQUIRE(cpu_softintr_p() && !cpu_intr_p() && curlwp != controller);
	REQUIRE(rvm_softint_member(curlwp) && curcpu()->ci_cpl == IPL_NONE);
	REQUIRE(norder < __arraycount(order));
	order[norder++] = (uintptr_t)arg;
	if ((uintptr_t)arg == 42 && repeats++ < 3) {
		softint_schedule(self_cookie); softint_schedule(self_cookie);
	}
	REQUIRE(canary == 0x534f4654494e5453UL);
}
static void tick(void *arg)
{
	REQUIRE(arg == &timer && cpu_softintr_p() && !cpu_intr_p());
	REQUIRE(curlwp != controller && rvm_softint_member(curlwp));
	fired++;
	mutex_enter(&lock); cv_broadcast(&cv); mutex_exit(&lock);
	if (fired < target) callout_reset(&timer, 1, tick, &timer);
}
void rvm_early_init(const struct rvm_bootinfo *bi)
{
	void *cookies[64]; vaddr_t stacks[14];
	unsigned int i, round, guard;
	int64_t pages, partial_pages;
	vsize_t mapsize; int entries;
	uint64_t limit;
	rvm_normal_early_init(bi); REQUIRE(rvm_boot_stage == 32);
	REQUIRE(rvm_softint_verify());
	pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	mapsize = kernel_map->size; entries = kernel_map->nentries;
	for (round = 0; round < 3; round++) {
		REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(NULL, NULL) && rvm_wait_init());
		controller = curlwp; REQUIRE(!cpu_softintr_p());
		REQUIRE(rvm_softint_init() && !rvm_softint_init());
		REQUIRE(!rvm_bootsched_fini());
		norder = repeats = 0;
		for (i = 0; i < 3; i++) {
			cookies[i] = softint_establish(SOFTINT_NET | SOFTINT_MPSAFE, record, (void *)(uintptr_t)i);
			REQUIRE(cookies[i] != NULL);
			softint_schedule(cookies[i]); softint_schedule_cpu(cookies[i], curcpu());
		}
		rvm_bootsched_yield(); REQUIRE(norder == 3);
		for (i = 0; i < 3; i++) { REQUIRE(order[i] == i); softint_disestablish(cookies[i]); }
		self_cookie = softint_establish(SOFTINT_BIO | SOFTINT_MPSAFE, record, (void *)42);
		softint_schedule(self_cookie); rvm_bootsched_yield();
		REQUIRE(norder == 7 && repeats == 4); softint_disestablish(self_cookie);
		/* Disestablishment drains a queued callback before recycling its slot. */
		self_cookie = softint_establish(SOFTINT_CLOCK | SOFTINT_MPSAFE, record, (void *)7);
		softint_schedule(self_cookie); softint_disestablish(self_cookie);
		REQUIRE(norder == 8 && order[7] == 7);
		/* Every client slot is usable, and a full table fails without mutation. */
		for (i = 0; i < 63; i++) {
			cookies[i] = softint_establish(SOFTINT_CLOCK | SOFTINT_MPSAFE, record, NULL);
			REQUIRE(cookies[i] != NULL);
		}
		REQUIRE(softint_establish(SOFTINT_CLOCK | SOFTINT_MPSAFE, record, NULL) == NULL);
		REQUIRE(!rvm_softint_fini());
		for (i = 0; i < 63; i++) softint_disestablish(cookies[i]);
		mutex_init(&lock, MUTEX_DEFAULT, IPL_NONE); cv_init(&cv, si_label);
		callout_init(&timer, CALLOUT_MPSAFE);
		REQUIRE(rvm_callout_softint_start() && !rvm_callout_softint_start());
		REQUIRE(rvm_async_clock_start()); rvm_async_preemption(0);
		fired = 0; target = 6; callout_reset(&timer, 1, tick, &timer);
		limit = rvm_clock_monotonic_ns() + 3000000000UL;
		while (fired < target && rvm_clock_monotonic_ns() < limit) __asm volatile("" ::: "memory");
		REQUIRE(fired == target); /* No voluntary yield: the IRQ return dispatches. */
		guard = rvm_intr_suspend();
		target += 3; callout_reset(&timer, 1, tick, &timer);
		mutex_enter(&lock);
		while (fired < target) REQUIRE(cv_timedwait(&cv, &lock, 50) == 0);
		mutex_exit(&lock);
		rvm_intr_resume(guard);
		REQUIRE(rvm_async_clock_stop());
		callout_halt(&timer, NULL); callout_destroy(&timer);
		REQUIRE(rvm_callout_softint_stop() && !rvm_callout_softint_stop());
		cv_destroy(&cv); mutex_destroy(&lock);
		REQUIRE(rvm_softint_fini() && !rvm_softint_fini());
		REQUIRE(rvm_bootsched_fini());
		REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages);
		REQUIRE(kernel_map->size == mapsize && kernel_map->nentries == entries);
	}
	/* Exhaust guarded-stack slots halfway through softint initialization. */
	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(NULL, NULL));
	for (i = 0; i < 13; i++) { stacks[i] = uvm_uarea_system_alloc(NULL); REQUIRE(stacks[i]); }
	partial_pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	REQUIRE(!rvm_softint_init() && !rvm_softint_ready());
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == partial_pages);
	REQUIRE(curcpu()->ci_data.cpu_softcpu == NULL && curcpu()->ci_data.cpu_softints == 0);
	for (i = 0; i < 13; i++) uvm_uarea_system_free(stacks[i]);
	REQUIRE(rvm_softint_init() && rvm_softint_fini() && rvm_bootsched_fini());
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages);
	REQUIRE(kernel_map->size == mapsize && kernel_map->nentries == entries);
	rvm_early_puts(si_ok); rvm_boot_stop();
}
