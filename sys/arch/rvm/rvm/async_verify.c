/* Asynchronous entry, masking, real elapsed tick catchup and MI preemption. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/callout.h>
#include <uvm/uvm.h>
#include <machine/boot.h>
#include <machine/psl.h>
#include <machine/asyncirq.h>
#include <machine/clock.h>
#include <machine/bootsched.h>
const char rvm_async_check_bad[] = "RVM asynchronous IRQ verification failed";
static volatile unsigned int calls __attribute__((section(".data"))) = 0;
static void
mark(void *arg)
{
	if (arg != &calls || cpu_intr_p() || rvm_boot_get_cr() != RVM_CR_MMU ||
	    curcpu()->ci_cpl != IPL_NONE) panic(rvm_async_check_bad);
	calls++;
}
int
rvm_async_irq_verify(void)
{
	unsigned int guard;
	uint64_t before, events, limit;
	callout_t callout;
	int oldipl, ticks;
	calls = 0;
	callout_init(&callout, CALLOUT_MPSAFE);
	callout_reset(&callout, 2, mark, (void *)&calls);
	before = rvm_async_clock_catchups();
	if (!rvm_async_clock_start()) return 0;
	limit = rvm_clock_monotonic_ns() + 2000000000;
	while (calls == 0 && rvm_clock_monotonic_ns() < limit) DELAY(1000);
	guard = rvm_intr_suspend();
	if (rvm_async_clock_events() == 0 || calls != 1 || curcpu()->ci_irq_locks != 0)
		return 0;
	oldipl = splhigh();
	rvm_intr_resume(guard);
	events = rvm_async_clock_events(); ticks = getticks();
	DELAY(65000);
	if (rvm_async_clock_events() != events || getticks() != ticks) return 0;
	splx(oldipl);
	limit = rvm_clock_monotonic_ns() + 2000000000;
	while (rvm_async_clock_events() == events && rvm_clock_monotonic_ns() < limit) DELAY(1000);
	guard = rvm_intr_suspend();
	if (rvm_async_clock_events() <= events || rvm_async_clock_catchups() <= before)
		return 0;
	callout_stop(&callout); callout_destroy(&callout);
	if (!rvm_async_clock_stop()) return 0;
	rvm_intr_resume(guard);
	return rvm_boot_get_cr() == RVM_CR_MMU && rvm_irq_get_inte() == 0 &&
	    curcpu()->ci_intr_depth == 0 && curcpu()->ci_irq_locks == 0;
}

static struct lwp workers[2] __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static volatile unsigned int progress[2] __attribute__((section(".data"))) = { 0 };
static void
busy_worker(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	uint64_t limit = rvm_clock_monotonic_ns() + 120000000;
	volatile uint64_t canary = 0x505245454d505400UL + id;
	/* No voluntary yield or wait in this interval. */
	while (rvm_clock_monotonic_ns() < limit) {
		if (canary != 0x505245454d505400UL + id || curlwp != &workers[id])
			panic(rvm_async_check_bad);
		progress[id]++;
	}
	rvm_bootsched_finish();
}
int
rvm_async_preempt_verify(void)
{
	unsigned int i, guard, held0, held1;
	vaddr_t stack;
	uint64_t before = rvm_bootsched_preemptions();
	int64_t pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_bootsched_init() || !rvm_bootsched_idle_init(NULL, NULL)) return 0;
	for (i = 0; i < 2; i++) {
		memset(&workers[i], 0, sizeof(workers[i])); progress[i] = 0;
		stack = uvm_uarea_system_alloc(curcpu());
		if (!stack) return 0;
		workers[i].l_cpu = curcpu(); workers[i].l_proc = &process;
		workers[i].l_stat = LSIDL; workers[i].l_flag = LW_SYSTEM;
		workers[i].l_pflag = LP_MPSAFE;
		uvm_lwp_setuarea(&workers[i], stack);
		uvm_lwp_fork(curlwp, &workers[i], NULL, 0, busy_worker, (void *)(uintptr_t)i);
		if (!rvm_bootsched_ready(&workers[i])) return 0;
	}
	if (!rvm_async_clock_start()) return 0;
	kpreempt_disable(); kpreempt_disable();
	rvm_async_preemption(1);
	held0 = progress[0]; held1 = progress[1];
	DELAY(60000);
	if (progress[0] != held0 || progress[1] != held1) return 0;
	kpreempt_enable(); DELAY(30000);
	if (progress[0] != held0 || progress[1] != held1) return 0;
	kpreempt_enable();
	/* The controller also waits without yielding: a timer must schedule the workers. */
	while (workers[0].l_stat != LSZOMB || workers[1].l_stat != LSZOMB)
		__asm volatile("" ::: "memory");
	guard = rvm_intr_suspend();
	if (progress[0] == 0 || progress[1] == 0 || rvm_bootsched_preemptions() <= before ||
	    workers[0].l_ru.ru_nivcsw == 0 || workers[1].l_ru.ru_nivcsw == 0) return 0;
	if (!rvm_async_clock_stop()) return 0;
	for (i = 0; i < 2; i++) if (!rvm_bootsched_reap(&workers[i])) return 0;
	if (!rvm_bootsched_fini()) return 0;
	rvm_intr_resume(guard);
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && kernel_map->nentries == 0;
}

volatile uint64_t rvm_async_probe_done __attribute__((section(".data"))) = 0;
static void
finish_probe(void *arg)
{
	if (arg != (void *)&rvm_async_probe_done || cpu_intr_p()) panic(rvm_async_check_bad);
	rvm_async_probe_done = 1;
}
int
rvm_async_register_verify(void)
{
	callout_t done;
	unsigned int guard;
	int result;
	rvm_async_probe_done = 0;
	callout_init(&done, CALLOUT_MPSAFE);
	callout_reset(&done, 3, finish_probe, (void *)&rvm_async_probe_done);
	if (!rvm_async_clock_start()) return 0;
	result = rvm_async_register_probe();
	guard = rvm_intr_suspend();
	if (!result || !rvm_async_probe_done || !rvm_async_clock_stop()) return 0;
	callout_stop(&done); callout_destroy(&done);
	rvm_intr_resume(guard);
	return 1;
}

#include <sys/kmem.h>
#include <sys/time.h>
static volatile unsigned int service_calls[2] __attribute__((section(".data"))) = { 0 };
static void
service_timeout(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	if (id > 1 || cpu_intr_p() || curcpu()->ci_cpl != IPL_NONE) panic(rvm_async_check_bad);
	service_calls[id]++;
}
static void
service_worker(void *arg)
{
	unsigned int id = (uintptr_t)arg, n;
	callout_t timeout;
	struct timespec ts;
	callout_init(&timeout, CALLOUT_MPSAFE);
	callout_reset(&timeout, 3, service_timeout, (void *)(uintptr_t)id);
	for (n = 0; n < 6; n++) {
		size_t size = 41 + id * 8 + n;
		unsigned char *p = kmem_zalloc(size, KM_NOSLEEP);
		uint64_t deadline = rvm_clock_monotonic_ns() + (id + 1) * 17000000;
		if (p == NULL || p[0] != 0 || p[size - 1] != 0) panic(rvm_async_check_bad);
		p[0] = 0x61; p[size - 1] = 0x92;
		if (rvm_bootsched_sleep_until(deadline) != 1 || rvm_clock_monotonic_ns() < deadline ||
		    p[0] != 0x61 || p[size - 1] != 0x92 || !rvm_rtc_read(&ts)) panic(rvm_async_check_bad);
		kmem_free(p, size);
		preempt_point();
	}
	if (service_calls[id] != 1) panic(rvm_async_check_bad);
	callout_stop(&timeout); callout_destroy(&timeout);
	rvm_bootsched_finish();
}
int
rvm_async_services_verify(void)
{
	unsigned int i, guard;
	vaddr_t stack;
	int64_t pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_bootsched_init() || !rvm_bootsched_idle_init(NULL, NULL)) return 0;
	for (i = 0; i < 2; i++) {
		memset(&workers[i], 0, sizeof(workers[i])); service_calls[i] = 0;
		stack = uvm_uarea_system_alloc(curcpu()); if (!stack) return 0;
		workers[i].l_cpu = curcpu(); workers[i].l_proc = &process;
		workers[i].l_stat = LSIDL; workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		uvm_lwp_setuarea(&workers[i], stack);
		uvm_lwp_fork(curlwp, &workers[i], NULL, 0, service_worker, (void *)(uintptr_t)i);
		if (!rvm_bootsched_ready(&workers[i])) return 0;
	}
	if (!rvm_async_clock_start()) return 0;
	rvm_async_preemption(1);
	while (workers[0].l_stat != LSZOMB || workers[1].l_stat != LSZOMB)
		if (rvm_bootsched_sleep_until(rvm_clock_monotonic_ns() + 27000000) != 1) return 0;
	guard = rvm_intr_suspend();
	if (!rvm_async_clock_stop()) return 0;
	for (i = 0; i < 2; i++) if (!rvm_bootsched_reap(&workers[i])) return 0;
	if (!rvm_bootsched_fini()) return 0;
	rvm_intr_resume(guard);
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && kernel_map->nentries == 0;
}
