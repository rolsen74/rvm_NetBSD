/* GeneralDriver timecounter/RTC; ICNT drives the scheduler clock. */
#include <sys/param.h>
#include <sys/atomic.h>
#include <machine/user.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/timetc.h>
#include <sys/intr.h>
#ifdef MULTIPROCESSOR
#include <sys/ipi.h>
#include <sys/xcall.h>
#endif
#include <dev/clock_subr.h>
#include <machine/boot.h>
#include <machine/generic.h>
#include <machine/irq.h>
#include <machine/asyncirq.h>
#include <machine/psl.h>
#include <machine/frame.h>
#include <machine/cache.h>
#include <machine/idle.h>

static volatile struct rvm_general_driver * const gd = (void *)RVM_GENERIC_ADDR;
static bool clock_started;
volatile uint64_t rvm_icnt_clock_ready;
void rvm_kernel_irq_entry(void);
void rvm_kernel_irq(uint64_t *);
void rvm_icnt_start(void);
void rvm_icnt_idle_ensure(void);
#ifdef MULTIPROCESSOR
void rvm_icnt_secondary_start(void);
#endif

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
	/* SMP reads of the device counter can regress; prefer the MI
	 * interrupt timecounter driven by the primary CPU's ICNT tick. */
	.tc_quality = -100,
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

void
cpu_initclocks(void)
{
	KASSERT(hz == RVM_CLOCK_HZ && !clock_started);
	rvm_intr_disable();
	stathz = 0;
	profhz = hz;
	rvm_boot_vectors[RVM_VECTOR_INSTCOUNT] = rvm_kernel_irq_entry;
	gd->timer[0].control = gd->timer[1].control = 0;
	gd->int_mask = 3;
	rvm_irq_set_inte(rvm_irq_get_inte() &
	    ~((UINT64_C(1) << RVM_TIMER_IRQ) | (UINT64_C(1) << RVM_TIMER2_IRQ)));
	tc_init(&rvm_timecounter);
	todr_attach(&rvm_todr);
	curcpu()->ci_last_tick = gd->monotonic_ns;
	clock_started = true;
	rvm_icnt_clock_ready = 1;
	rvm_icnt_start();
	rvm_intr_apply();
}

void
rvm_icnt_idle_ensure(void)
{
	struct cpu_info *ci = curcpu();

	/* A suppressed ICNT exception can leave an idle CPU without a tick.
	 * HALT's finite count still returns here, so restore the clock only
	 * after a missed interval. */
	if (clock_started && gd->monotonic_ns - ci->ci_last_tick >=
	    2 * (1000000000 / RVM_CLOCK_HZ))
		rvm_icnt_start();
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

	if (!clock_started || (frame[0] >> 48) != RVM_EF_VERSION_TEMP ||
	    vector != RVM_VECTOR_INSTCOUNT)
		rvm_boot_trap(saved + 1);
	rvm_idle_irq_return(frame);
#ifdef MULTIPROCESSOR
	/* ICNT is independent of CR's device interrupt mask.  Service IPIs
	 * at high IPL as a real interprocessor interrupt would, unless the
	 * interrupted CPU is inside the global atomic exchange gate. */
	if (mp_online && ci->ci_atomic_active == 0 && ci->ci_pmap_depth == 0) {
		ci->ci_cpl = IPL_HIGH;
		ci->ci_intr_depth++;
		ipi_cpu_handler();
		if (atomic_swap_uint(&ci->ci_flags, 0) & 1)
			xc_ipi_handler();
		ci->ci_intr_depth--;
		ci->ci_cpl = oldipl;
	}
#endif
	/* ICNT also fires with device interrupts masked. Do not run MI clock
	 * callbacks inside a critical section; elapsed time is caught up later. */
	if (!(saved[0] & RVM_CR_INTERRUPT) || oldipl >= IPL_SCHED ||
	    ci->ci_mtx_count != 0 || ci->ci_intr_depth != 0 ||
	    (curlwp != NULL && curlwp->l_md.md_irq_hold != 0))
		return;
	ci->ci_cpl = IPL_SCHED;
	ci->ci_intr_depth++;
	now = gd->monotonic_ns;
	elapsed = (now - ci->ci_last_tick) / (1000000000 / RVM_CLOCK_HZ);
	if (elapsed > 64) elapsed = 64;
	while (elapsed-- != 0) {
		ci->ci_last_tick += 1000000000 / RVM_CLOCK_HZ;
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

#ifdef MULTIPROCESSOR
void
rvm_icnt_secondary_start(void)
{
	curcpu()->ci_last_tick = gd->monotonic_ns;
	rvm_icnt_start();
}
#endif

void
delay(unsigned int usec)
{
	uint64_t begin = gd->monotonic_ns;
	uint64_t duration = (uint64_t)usec * 1000;
	while (gd->monotonic_ns - begin < duration)
		__asm volatile("" ::: "memory");
}
void setstatclockrate(int frequency __unused) { }
