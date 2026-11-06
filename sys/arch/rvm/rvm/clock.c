/* Host-backed time: independent of serviced IRQ counts and wall-clock steps. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <machine/boot.h>
#include <machine/pmap.h>
#include <machine/psl.h>
#include <machine/generic.h>
#include <machine/clock.h>
#include <machine/boottime.h>
#include <machine/bootclock.h>
#include <machine/irq.h>
#include <machine/asyncirq.h>

const char rvm_clock_bad[] = "Invalid RVM platform clock operation";
uint64_t
rvm_clock_monotonic_ns(void)
{
	volatile struct rvm_general_driver *gd = (void *)RVM_GENERIC_ADDR;
	return gd->monotonic_ns;
}

int
rvm_rtc_read(struct timespec *ts)
{
	volatile struct rvm_general_driver *gd = (void *)RVM_GENERIC_ADDR;
	uint64_t seconds, nanoseconds;
	unsigned int guard;
	int ok;
	/* No interleaving seconds/nanoseconds sequences on this UP kernel. */
	if (ts == NULL || (rvm_boot_get_cr() != RVM_CR_MMU &&
	    rvm_boot_get_cr() != (RVM_CR_MMU | RVM_CR_INTERRUPT)) || cpu_intr_p()) return 0;
	guard = rvm_intr_suspend();
	seconds = gd->rtc_seconds;
	nanoseconds = gd->rtc_nanoseconds;
	ok = seconds <= INT64_MAX && nanoseconds < 1000000000;
	if (ok) { ts->tv_sec = seconds; ts->tv_nsec = nanoseconds; }
	rvm_intr_resume(guard);
	return ok;
}

void
inittodr(time_t base)
{
	unsigned int guard = rvm_intr_suspend();
	struct timespec ts;
	if (!rvm_timecounter_ready()) panic(rvm_clock_bad);
	if (!rvm_rtc_read(&ts)) {
		if (base < 0) panic(rvm_clock_bad);
		ts.tv_sec = base; ts.tv_nsec = 0;
	}
	tc_setclock(&ts);
	rvm_intr_resume(guard);
}

/* Busy delay, usable with interrupts masked. No scheduler or tick dependence.
 * Subtraction also tolerates a wrap in the raw 64-bit nanosecond counter. */
void
delay(unsigned int usec)
{
	uint64_t start, duration;
	if (usec == 0) return;
	start = rvm_clock_monotonic_ns();
	duration = (uint64_t)usec * 1000;
	while (rvm_clock_monotonic_ns() - start < duration)
		__asm volatile("" ::: "memory");
}

/* Absolute raw-monotonic deadline. Tick granularity may overshoot, but never
 * returns early. IRQ delivery remains restricted to the existing HALT probe. */
int
rvm_clock_wait_until(uint64_t deadline)
{
	if (!rvm_clock_active() || rvm_bootclock_busy() ||
	    rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() ||
	    curcpu()->ci_cpl != IPL_NONE) return 0;
	while (rvm_clock_monotonic_ns() < deadline)
		if (!rvm_clock_wait()) return 0;
	return 1;
}
