/* Sustained mixed allocation/sleep/preemption load; duration set by harness. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/kmem.h>
#include <sys/callout.h>
#include <uvm/uvm.h>
#include <machine/asyncirq.h>
#include <machine/clock.h>
#include <machine/bootsched.h>
#define STRESS_WORKERS 8
const char as_bad[] = "Asynchronous stress regression at line 0x";
const char as_ok[] = "RVM sustained asynchronous allocation/sleep/preemption stress verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(as_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp workers[STRESS_WORKERS] __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static uint64_t stop_ns __attribute__((section(".data"))) = 0;
static uint64_t transactions[STRESS_WORKERS] __attribute__((section(".data"))) = { 0 };
static volatile unsigned int expirations[STRESS_WORKERS] __attribute__((section(".data"))) = { 0 };
static void timeout(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	REQUIRE(id < STRESS_WORKERS && !cpu_intr_p()); expirations[id]++;
}
static void worker(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	unsigned int random = 731 + id * 31;
	volatile uint64_t canary = 0x4153594e43535400UL + id;
	callout_t timer;
	callout_init(&timer, CALLOUT_MPSAFE);
	while (rvm_clock_monotonic_ns() < stop_ns) {
		size_t size;
		unsigned char *p;
		uint64_t deadline;
		random = random * 1103515245U + 12345;
		size = 33 + (random & 0x1ffff);
		p = kmem_zalloc(size, KM_NOSLEEP); REQUIRE(p != NULL);
		REQUIRE(p[0] == 0 && p[size / 2] == 0 && p[size - 1] == 0);
		p[0] = id + 1; p[size / 2] = id + 0x30; p[size - 1] = id + 0x70;
		callout_reset(&timer, 2 + (random & 3), timeout, (void *)(uintptr_t)id);
		deadline = rvm_clock_monotonic_ns() + (1 + (random % 37)) * 1000000;
		REQUIRE(rvm_bootsched_sleep_until(deadline) == 1);
		REQUIRE(rvm_clock_monotonic_ns() >= deadline && canary == 0x4153594e43535400UL + id);
		REQUIRE(p[0] == id + 1 && p[size / 2] == id + 0x30 && p[size - 1] == id + 0x70);
		callout_stop(&timer);
		kmem_free(p, size);
		transactions[id]++;
	}
	callout_destroy(&timer);
	rvm_bootsched_finish();
}
void rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int i, guard, finished;
	int64_t pages;
	uint64_t events, ticks;
	rvm_normal_early_init(bi); REQUIRE(rvm_boot_stage == 32);
	pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(NULL, NULL));
	for (i = 0; i < STRESS_WORKERS; i++) {
		vaddr_t stack = uvm_uarea_system_alloc(curcpu()); REQUIRE(stack != 0);
		workers[i].l_cpu = curcpu(); workers[i].l_proc = &process;
		workers[i].l_stat = LSIDL; workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		uvm_lwp_setuarea(&workers[i], stack);
		uvm_lwp_fork(curlwp, &workers[i], NULL, 0, worker, (void *)(uintptr_t)i);
		REQUIRE(rvm_bootsched_ready(&workers[i]));
	}
	events = rvm_async_clock_events(); ticks = rvm_async_clock_ticks();
	stop_ns = rvm_clock_monotonic_ns() + RVM_ASYNC_STRESS_NS;
	REQUIRE(rvm_async_clock_start()); rvm_async_preemption(1);
	do {
		REQUIRE(rvm_bootsched_sleep_until(rvm_clock_monotonic_ns() + 31000000) == 1);
		finished = 0;
		for (i = 0; i < STRESS_WORKERS; i++) finished += workers[i].l_stat == LSZOMB;
	} while (finished != STRESS_WORKERS);
	guard = rvm_intr_suspend(); REQUIRE(rvm_async_clock_stop());
	for (i = 0; i < STRESS_WORKERS; i++) {
		REQUIRE(transactions[i] > 10 && rvm_bootsched_reap(&workers[i]));
	}
	REQUIRE(rvm_bootsched_fini()); rvm_intr_resume(guard);
	REQUIRE(rvm_async_clock_events() > events + 100 && rvm_async_clock_ticks() > ticks + 100);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && kernel_map->nentries == 0);
	REQUIRE(curcpu()->ci_irq_locks == 0 && curlwp->l_md.md_irq_hold == 0);
	/* Stopping/draining async mode must also fence a return to the old timer. */
	REQUIRE(rvm_clock_start() && rvm_clock_wait() && rvm_clock_wait() && rvm_clock_stop());
	rvm_early_puts(as_ok);
}
