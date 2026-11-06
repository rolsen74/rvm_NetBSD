/* Bring-up checks for the platform clock services. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <sys/callout.h>
#include <machine/boot.h>
#include <machine/clock.h>
#include <machine/irq.h>
#include <machine/bootsched.h>

int
rvm_rtc_verify(void)
{
	struct timespec rtc, wall;
	if (!rvm_rtc_read(&rtc) || rtc.tv_sec < 1700000000) return 0;
	inittodr(0);
	nanotime(&wall);
	return wall.tv_sec >= rtc.tv_sec && wall.tv_sec <= rtc.tv_sec + 2;
}
int
rvm_delay_verify(void)
{
	uint64_t start = rvm_clock_monotonic_ns();
	struct timespec before, after;
	int ticks = getticks();
	nanouptime(&before);
	DELAY(0); DELAY(2000);
	nanouptime(&after);
	return rvm_clock_monotonic_ns() - start >= 2000000 && getticks() == ticks &&
	    timespeccmp(&after, &before, >);
}
int
rvm_wallclock_verify(void)
{
	struct timespec before, after, target, wall, delta;
	uint64_t elapsed;
	uint64_t raw = rvm_clock_monotonic_ns();
	nanouptime(&before);
	nanotime(&target); target.tv_sec += 86400;
	tc_setclock(&target); nanotime(&wall);
	if (wall.tv_sec < target.tv_sec || wall.tv_sec > target.tv_sec + 2) return 0;
	target.tv_sec -= 172800;
	tc_setclock(&target); nanotime(&wall); nanouptime(&after);
	if (wall.tv_sec < target.tv_sec || wall.tv_sec > target.tv_sec + 2 ||
	    !timespeccmp(&after, &before, >=) || rvm_clock_monotonic_ns() < raw) return 0;
	timespecsub(&after, &before, &delta);
	elapsed = (uint64_t)delta.tv_sec * 1000000000 + delta.tv_nsec;
	if (elapsed > rvm_clock_monotonic_ns() - raw + 2000) return 0;
	inittodr(0);
	return 1;
}
int
rvm_deadline_verify(void)
{
	uint64_t deadline;
	int ticks;
	if (!rvm_clock_start()) return 0;
	ticks = getticks(); deadline = rvm_clock_monotonic_ns();
	if (!rvm_clock_wait_until(deadline) || getticks() != ticks) return 0;
	deadline += 30000000;
	if (!rvm_clock_wait_until(deadline) || rvm_clock_monotonic_ns() < deadline) return 0;
	return rvm_clock_stop();
}
static void
early_wakeup(void *arg)
{
	if (!rvm_bootsched_ready(arg)) panic("Invalid RVM timed sleep wakeup");
}
int
rvm_tsleep_verify(void)
{
	callout_t early;
	uint64_t deadline;
	int result;
	if (!rvm_bootsched_init() || !rvm_bootsched_idle_init(NULL, NULL) ||
	    !rvm_clock_start()) return 0;
	deadline = rvm_clock_monotonic_ns() + 30000000;
	result = rvm_bootsched_sleep_until(deadline);
	if (result != 1 || rvm_clock_monotonic_ns() < deadline) return 0;
	callout_init(&early, CALLOUT_MPSAFE);
	callout_reset(&early, 1, early_wakeup, curlwp);
	result = rvm_bootsched_sleep_until(rvm_clock_monotonic_ns() + 1000000000);
	callout_stop(&early); callout_destroy(&early);
	if (result != 0) return 0;
	/* Reusing the timeout slot must not inherit the cancelled deadline. */
	deadline = rvm_clock_monotonic_ns() + 20000000;
	result = rvm_bootsched_sleep_until(deadline);
	return result == 1 && rvm_clock_monotonic_ns() >= deadline &&
	    rvm_clock_stop() && rvm_bootsched_fini();
}
