/* Real GenericDriver clocks, MI wall/uptime and concurrent deadline sleepers. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <sys/callout.h>
#include <uvm/uvm.h>
#include <machine/clock.h>
#include <machine/boottime.h>
#include <machine/bootsched.h>
#include <machine/irq.h>
const char pc_bad[] = "Platform clock regression at line 0x";
const char pc_ok[] = "RVM platform clocks, concurrent timed sleeps and busy runtime verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(pc_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp workers[2] __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static callout_t early __attribute__((section(".data"))) = { 0 };
static unsigned int done[2] __attribute__((section(".data"))) = { 0 };
static uint64_t busy_start[2] __attribute__((section(".data"))) = { 0 };
static uint64_t busy_end[2] __attribute__((section(".data"))) = { 0 };
static struct lwp *parent __attribute__((section(".data"))) = NULL;
static void wake_parent(void *arg)
{
	REQUIRE(arg == parent && !cpu_intr_p() && curcpu()->ci_cpl == IPL_NONE);
	REQUIRE(rvm_bootsched_ready(parent));
}
static uint64_t runtime_ns(const struct bintime *bt)
{
	struct timespec ts;
	bintime2timespec(bt, &ts);
	return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
static uint64_t live_runtime_ns(struct lwp *l)
{
	struct bintime total = l->l_rtime, now;
	binuptime(&now); bintime_add(&total, &now); bintime_sub(&total, &l->l_stime);
	return runtime_ns(&total);
}
static void worker(void *arg)
{
	unsigned int id = (uintptr_t)arg, n;
	volatile uint64_t canary = 0x434c4f434b535441UL + id;
	busy_start[id] = rvm_clock_monotonic_ns();
	DELAY(12000 + id * 1000);
	busy_end[id] = rvm_clock_monotonic_ns();
	for (n = 0; n < 3; n++) {
		uint64_t deadline = rvm_clock_monotonic_ns() + (id + 1) * 20000000;
		REQUIRE(rvm_bootsched_sleep_until(deadline) == 1);
		REQUIRE(rvm_clock_monotonic_ns() >= deadline);
		REQUIRE(canary == 0x434c4f434b535441UL + id);
	}
	done[id] = 1;
	rvm_bootsched_finish();
}
void rvm_early_init(const struct rvm_bootinfo *bi)
{
	struct timespec rtc, wall, live, later, cached, target;
	struct bintime bt;
	uint64_t raw, end, elapsed, accounted, parent_runtime;
	unsigned int i;
	int ticks;
	int64_t pages;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32 && tc_getfrequency() == RVM_TIMECOUNTER_FREQUENCY);
	pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	REQUIRE(!rvm_clock_active() && rvm_rtc_verify() && rvm_delay_verify());
	REQUIRE(rvm_wallclock_verify() && rvm_deadline_verify() && rvm_tsleep_verify());
	REQUIRE(rvm_rtc_read(&rtc)); nanotime(&wall);
	REQUIRE(wall.tv_sec >= rtc.tv_sec && wall.tv_sec <= rtc.tv_sec + 2);
	raw = rvm_clock_monotonic_ns(); ticks = getticks();
	getnanouptime(&cached); nanouptime(&live);
	curlwp->l_tcgen = 17; DELAY(5000); nanouptime(&later);
	REQUIRE(curlwp->l_tcgen == 17); curlwp->l_tcgen = 0;
	REQUIRE(getticks() == ticks && timespeccmp(&live, &cached, >=));
	REQUIRE(timespeccmp(&later, &live, >) && rvm_clock_monotonic_ns() >= raw + 5000000);
	/* Calendar changes must neither charge nor erase the running LWP's time. */
	target.tv_sec = rtc.tv_sec + 86400; target.tv_nsec = 987654321;
	tc_setclock(&target); nanotime(&wall); nanouptime(&later);
	REQUIRE(wall.tv_sec >= target.tv_sec && timespeccmp(&later, &live, >=));
	inittodr(0);
	REQUIRE(rvm_bootsched_sleep_until(0) == -1); /* scheduler inactive */
	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(NULL, NULL));
	REQUIRE(rvm_clock_start());
	parent = curlwp;
	REQUIRE(rvm_bootsched_sleep_until(0) == 1);
	splhigh();
	REQUIRE(rvm_bootsched_sleep_until(UINT64_MAX) == -1);
	REQUIRE(!rvm_clock_wait_until(UINT64_MAX));
	spl0();
	/* Early wake cancels a distant timeout; reuse slots repeatedly. */
	callout_init(&early, CALLOUT_MPSAFE);
	for (i = 0; i < 4; i++) {
		callout_reset(&early, 1, wake_parent, parent);
		REQUIRE(rvm_bootsched_sleep_until(rvm_clock_monotonic_ns() + 2000000000) == 0);
		callout_stop(&early);
	}
	callout_destroy(&early);
	for (i = 0; i < 2; i++) {
		vaddr_t stack;
		stack = uvm_uarea_system_alloc(NULL); REQUIRE(stack != 0);
		workers[i].l_cpu = curcpu(); workers[i].l_proc = &process;
		workers[i].l_stat = LSIDL; workers[i].l_flag = LW_SYSTEM;
		workers[i].l_pflag = LP_MPSAFE;
		uvm_lwp_setuarea(&workers[i], stack);
		uvm_lwp_fork(parent, &workers[i], NULL, 0, worker, (void *)(uintptr_t)i);
		REQUIRE(rvm_bootsched_ready(&workers[i]));
	}
	parent_runtime = live_runtime_ns(parent);
	raw = rvm_clock_monotonic_ns();
	/* Sleep the parent while both workers run and block on independent timers. */
	while (!done[0] || !done[1])
		REQUIRE(rvm_bootsched_sleep_until(rvm_clock_monotonic_ns() + 40000000) == 1);
	end = rvm_clock_monotonic_ns();
	REQUIRE(live_runtime_ns(parent) - parent_runtime + 5000000 < end - raw);
	for (i = 0; i < 2; i++) {
		elapsed = busy_end[i] - busy_start[i];
		accounted = runtime_ns(&workers[i].l_rtime);
		REQUIRE(elapsed >= 12000000 && accounted + 2000 >= elapsed);
		REQUIRE(accounted < end - busy_start[i]); /* blocked time excluded */
		REQUIRE(workers[i].l_stat == LSZOMB && rvm_bootsched_reap(&workers[i]));
	}
	bt = curcpu()->ci_data.cpu_idlelwp->l_rtime;
	REQUIRE(bt.sec == 0 && bt.frac == 0 && end > raw);
	REQUIRE(rvm_clock_stop() && rvm_bootsched_fini());
	REQUIRE(rvm_bootsched_sleep_until(0) == -1);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && kernel_map->nentries == 0);
	rvm_early_puts(pc_ok);
}
