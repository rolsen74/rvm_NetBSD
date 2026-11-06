/* MI hardclock adapter. Uptime uses the independent GenericDriver counter. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/callout.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <machine/boot.h>
#include <machine/pmap.h>
#include <machine/psl.h>
#include <machine/irq.h>
#include <machine/bootclock.h>
#include <machine/boottime.h>
#include <machine/clock.h>
#include <machine/bootsched.h>

static unsigned int initialized __attribute__((section(".data"))) = 0;
static uint64_t uptime_ticks __attribute__((section(".data"))) = 0;
const char rvm_hardclock_bad[] = "Invalid bootstrap MI hardclock operation";
int rvm_bootclock_ready(void) { return initialized; }
int rvm_bootclock_busy(void) { return rvm_callout_busy(); }
int
rvm_bootclock_init(void)
{
	if (initialized) return 1;
	if (rvm_clock_active() || rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() ||
	    curcpu()->ci_cpl != IPL_NONE || !rvm_callout_cpu_init()) return 0;
	hz = RVM_CLOCK_HZ;
	curcpu()->ci_schedstate.spc_ticks = RVM_CLOCK_HZ / 10;
	curcpu()->ci_schedstate.spc_schedticks = RVM_CLOCK_HZ / 16;
	initialized = 1;
	return 1;
}
void
rvm_bootclock_intr(vaddr_t pc, uint64_t sr)
{
	struct clockframe cf;
	if (!initialized) return;
	if (rvm_boot_get_cr() != RVM_CR_MMU || curcpu()->ci_intr_depth != 1 ||
	    curcpu()->ci_cpl != IPL_HIGH || USERMODE(sr)) panic(rvm_hardclock_bad);
	cf.cf_pc = pc; cf.cf_usermode = 0; cf.cf_intr_depth = 1;
	hardclock(&cf);
}
void
rvm_bootclock_ticktock(void)
{
	if (!initialized || curcpu()->ci_intr_depth != 1 || curcpu()->ci_cpl != IPL_HIGH)
		panic(rvm_hardclock_bad);
	uptime_ticks++;
	rvm_timecounter_tick();
}
void
rvm_bootclock_binuptime(struct bintime *bt)
{
	if (rvm_timecounter_ready()) { binuptime(bt); return; }
	bt->sec = uptime_ticks / RVM_CLOCK_HZ;
	bt->frac = (uptime_ticks % RVM_CLOCK_HZ) * (UINT64_MAX / RVM_CLOCK_HZ + 1);
}
void
rvm_bootclock_poll(void)
{
	if (initialized) rvm_callout_poll();
}
static callout_t wakeup __attribute__((section(".data"))) = { 0 };
static unsigned int fired __attribute__((section(".data"))) = 0;
static void
wake_lwp(void *arg)
{
	if (cpu_intr_p() || curcpu()->ci_cpl != IPL_NONE ||
	    !rvm_bootsched_ready(arg)) panic(rvm_hardclock_bad);
	fired++;
}
int
rvm_bootclock_verify(void)
{
	int before;
	int64_t freepages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_bootclock_init() || !rvm_bootsched_init() ||
	    !rvm_bootsched_idle_init(NULL, NULL) || !rvm_clock_start()) return 0;
	before = getticks(); fired = 0;
	callout_init(&wakeup, CALLOUT_MPSAFE);
	callout_reset(&wakeup, 3, wake_lwp, curlwp);
	rvm_bootsched_sleep();
	if (fired != 1 || getticks() != before + 3 || callout_pending(&wakeup) ||
	    !callout_expired(&wakeup)) return 0;
	callout_ack(&wakeup); callout_stop(&wakeup); callout_destroy(&wakeup);
	return rvm_clock_stop() && rvm_bootsched_fini() &&
	    curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == freepages;
}

int
rvm_timecounter_verify(void)
{
	struct bintime before, after;
	struct timespec ts;
	struct timeval tv;
	if (!rvm_timecounter_init()) return 0;
	binuptime(&before);
	if (before.sec < 1 || tc_getfrequency() != RVM_TIMECOUNTER_FREQUENCY || !rvm_clock_start()) return 0;
	if (!rvm_clock_wait() || !rvm_clock_wait() || !rvm_clock_stop()) return 0;
	binuptime(&after); nanouptime(&ts); microuptime(&tv);
	return bintimecmp(&after, &before, >) && ts.tv_sec >= after.sec &&
	    tv.tv_sec >= ts.tv_sec && ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000 &&
	    tv.tv_usec >= 0 && tv.tv_usec < 1000000;
}
