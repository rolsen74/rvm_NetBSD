/* GeneralDriver timecounter, read-only host RTC, and alternating timer IRQs. */
#include <sys/param.h>
#include <machine/user.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/timetc.h>
#include <sys/intr.h>
#include <dev/clock_subr.h>
#include <machine/boot.h>
#include <machine/generic.h>
#include <machine/irq.h>
#include <machine/asyncirq.h>
#include <machine/psl.h>
#include <machine/cache.h>

static volatile struct rvm_general_driver * const gd = (void *)RVM_GENERIC_ADDR;
static uint64_t last_tick;
static unsigned int channel;
static bool clock_started;
void rvm_kernel_irq_entry(void);
void rvm_kernel_irq(uint64_t *);

static u_int
rvm_get_timecount(struct timecounter *tc __unused)
{
	return (u_int)(gd->monotonic_ns / 1000);
}
static struct timecounter rvm_timecounter = {
	.tc_get_timecount = rvm_get_timecount,
	.tc_counter_mask = UINT32_MAX,
	.tc_frequency = 1000000,
	.tc_name = "rvm-monotonic",
	.tc_quality = 100,
};

static int
rvm_rtc_get(todr_chip_handle_t handle __unused, struct timeval *tv)
{
	/* Reading seconds latches its fractional part in the next register. */
	tv->tv_sec = gd->rtc_seconds;
	tv->tv_usec = gd->rtc_nanoseconds / 1000;
	return 0;
}
static int
rvm_rtc_set(todr_chip_handle_t handle __unused, struct timeval *tv __unused)
{
	return EOPNOTSUPP;
}
static struct todr_chip_handle rvm_todr = {
	.todr_gettime = rvm_rtc_get,
	.todr_settime = rvm_rtc_set,
};

static void
rvm_timer_arm(unsigned int next)
{
	gd->timer[next].control = 0;
	gd->timer[next].counter = 0;
	gd->timer[next].reset = 1;
	rvm_irq_set_inte(UINT64_C(1) << (next ? RVM_TIMER2_IRQ : RVM_TIMER_IRQ));
	gd->timer[next].control = RVM_TIMER_ACTIVE | RVM_TIMER_INTERRUPT;
}

void
cpu_initclocks(void)
{
	KASSERT(hz == RVM_CLOCK_HZ && !clock_started);
	rvm_intr_disable();
	stathz = 0;
	profhz = hz;
	rvm_boot_vectors[RVM_TIMER_VECTOR] = rvm_kernel_irq_entry;
	rvm_boot_vectors[RVM_TIMER2_VECTOR] = rvm_kernel_irq_entry;
	gd->timer[0].control = gd->timer[1].control = 0;
	gd->int_mask = 3;
	tc_init(&rvm_timecounter);
	todr_attach(&rvm_todr);
	channel = 0;
	last_tick = gd->monotonic_ns;
	clock_started = true;
	rvm_timer_arm(channel);
	rvm_intr_apply();
}

void
rvm_kernel_irq(uint64_t *saved)
{
	struct cpu_info *ci = curcpu();
	uint64_t *frame = saved + 64; /* CR, 63 GPRs, hardware frame */
	unsigned int vector = (frame[0] >> 32) & 0xffff;
	uint64_t now, elapsed;
	int oldipl = ci->ci_cpl;
	struct clockframe cf = {
		.cf_pc = frame[1], .cf_usermode = USERMODE(frame[0]),
		.cf_intr_depth = ci->ci_intr_depth + 1,
	};
	KASSERT(!cf.cf_usermode ||
	    (saved[0] & (RVM_CR_INSTCACHE | RVM_CR_DATACACHE) & ~RVM_USER_CACHE_MASK) == 0);

	if (!clock_started || (frame[0] >> 48) != 0 ||
	    vector != (channel ? RVM_TIMER2_VECTOR : RVM_TIMER_VECTOR))
		rvm_boot_trap(saved + 1);
	ci->ci_cpl = IPL_SCHED;
	ci->ci_intr_depth++;
	gd->timer[channel].control = 0;
	gd->int_mask = 1UL << channel;
	channel ^= 1;
	rvm_timer_arm(channel);
	now = gd->monotonic_ns;
	elapsed = (now - last_tick) / (1000000000 / RVM_CLOCK_HZ);
	while (elapsed-- != 0) {
		last_tick += 1000000000 / RVM_CLOCK_HZ;
		hardclock(&cf);
	}
	ci->ci_intr_depth--;
	ci->ci_cpl = oldipl;
	if (cf.cf_usermode) rvm_user_irq_return((void *)(saved + 1));
	/* Slow softints are selected by MI mi_switch(). They remain pending
	 * until a scheduling point; this port does not advertise fast softints
	 * or in-kernel preemption in its normal-kernel feature flags yet.
	 */
}

void
delay(unsigned int usec)
{
	uint64_t begin = gd->monotonic_ns;
	uint64_t duration = (uint64_t)usec * 1000;
	while (gd->monotonic_ns - begin < duration)
		__asm volatile("" ::: "memory");
}
void setstatclockrate(int frequency __unused) { }
