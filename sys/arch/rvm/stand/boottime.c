/* Included after MI bodies. Counter callbacks and hand links are set at runtime. */
#include <machine/irq.h>
static unsigned int initialized __attribute__((section(".data"))) = 0;
static struct timecounter dummy_counter __attribute__((section(".data"))) = { 0 };
static struct timecounter rvm_counter __attribute__((section(".data"))) = { 0 };
static struct timecounter *registered[4] __attribute__((section(".data"))) = { 0 };
static unsigned int nregistered __attribute__((section(".data"))) = 0;
const char rvm_tc_bad[] = "Invalid bootstrap MI timecounter operation";
const char rvm_tc_name[] = "rvm-serviced-ticks";
const char rvm_tc_dummy[] = "rvm-early-ticks";
static u_int
read_ticks(struct timecounter *tc __unused)
{
	return (u_int)getticks();
}
static int
context_ok(void)
{
	return rvm_boot_get_cr() == RVM_CR_MMU && !cpu_intr_p() &&
	    curcpu()->ci_cpl == IPL_NONE && curlwp != NULL && curlwp == curcpu()->ci_onproc;
}
int rvm_timecounter_ready(void) { return initialized; }
void
tc_init(struct timecounter *tc)
{
	unsigned int i;
	if (!initialized || !context_ok() || tc == NULL || tc->tc_get_timecount == NULL ||
	    tc->tc_frequency == 0 || tc->tc_frequency > 1000000000 ||
	    tc->tc_counter_mask == 0 || (tc->tc_counter_mask & (tc->tc_counter_mask + 1)) != 0 ||
	    tc->tc_poll_pps != NULL || tc->tc_next != NULL || nregistered == __arraycount(registered))
		panic(rvm_tc_bad);
	for (i = 0; i < nregistered; i++) if (registered[i] == tc) panic(rvm_tc_bad);
	registered[nregistered++] = tc;
	rvm_mi_tc_init(tc);
}
int
rvm_timecounter_init(void)
{
	unsigned int i;
	struct bintime previous;
	if (initialized) return 1;
	if (!context_ok() || !rvm_bootclock_ready() || rvm_clock_active()) return 0;
	/* Commit runtime in the early epoch before installing MI's uptime base. */
	rvm_bootclock_binuptime(&previous);
	updatertime(curlwp, &previous);
	mutex_init(&timecounter_lock, MUTEX_SPIN, IPL_HIGH);
	dummy_counter.tc_get_timecount = read_ticks;
	dummy_counter.tc_counter_mask = UINT_MAX;
	dummy_counter.tc_frequency = RVM_CLOCK_HZ;
	dummy_counter.tc_quality = -1000;
	dummy_counter.tc_name = rvm_tc_dummy;
	timecounter = timecounters = &dummy_counter;
	for (i = 0; i < __arraycount(hands); i++) {
		hands[i].th_counter = &dummy_counter;
		hands[i].th_scale = ((1UL << 63) / RVM_CLOCK_HZ) * 2;
		hands[i].th_offset_count = read_ticks(NULL);
		hands[i].th_offset.sec = 1;
		hands[i].th_next = &hands[(i + 1) % __arraycount(hands)];
	}
	hands[0].th_generation = 1;
	timehands = &hands[0];
	setrealuptime(1, 1);
	rvm_counter.tc_get_timecount = read_ticks;
	rvm_counter.tc_counter_mask = UINT_MAX;
	rvm_counter.tc_frequency = RVM_CLOCK_HZ;
	rvm_counter.tc_quality = 100;
	rvm_counter.tc_name = rvm_tc_name;
	initialized = 1;
	tc_init(&rvm_counter);
	binuptime(&curlwp->l_stime);
	return timecounter == &rvm_counter;
}
void
rvm_timecounter_tick(void)
{
	if (!initialized) return;
	if (rvm_boot_get_cr() != RVM_CR_MMU || curcpu()->ci_intr_depth != 1 ||
	    curcpu()->ci_cpl != IPL_HIGH) panic(rvm_tc_bad);
	tc_ticktock();
}
void
tc_setclock(const struct timespec *ts __unused)
{
	panic(rvm_tc_bad); /* No RTC, UTC setting, NTP or adjtime service yet. */
}
