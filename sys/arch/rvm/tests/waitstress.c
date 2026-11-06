/* Eight LWPs exchange heap-backed messages through real MI CV queues. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/kmem.h>
#include <sys/errno.h>
#include <uvm/uvm.h>
#include <machine/clock.h>
#include <machine/asyncirq.h>
#include <machine/bootwait.h>
#include <machine/bootsched.h>
const char ws_bad[] = "MI CV stress regression at line 0x";
const char ws_ok[] = "RVM sustained MI CV producer/consumer stress and cleanup verified.\n";
const char ws_name[] = "cv-stress";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(ws_bad); rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp workers[8] __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static kmutex_t lock __attribute__((section(".data"))) = { 0 };
static kcondvar_t readable __attribute__((section(".data"))) = { 0 };
static kcondvar_t writable __attribute__((section(".data"))) = { 0 };
static struct message { uint64_t number; unsigned char *data; size_t size; }
    ring[8] __attribute__((section(".data"))) = { 0 };
static uint64_t stop_ns __attribute__((section(".data"))) = 0;
static uint64_t sent __attribute__((section(".data"))) = 0;
static uint64_t received __attribute__((section(".data"))) = 0;
static uint64_t sum_sent __attribute__((section(".data"))) = 0;
static uint64_t sum_received __attribute__((section(".data"))) = 0;
static unsigned int live_producers __attribute__((section(".data"))) = 4;
static unsigned int head __attribute__((section(".data"))) = 0;
static unsigned int tail __attribute__((section(".data"))) = 0;
static unsigned int count __attribute__((section(".data"))) = 0;
static unsigned int per_worker[8] __attribute__((section(".data"))) = { 0 };
static void producer(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	while (rvm_clock_monotonic_ns() < stop_ns) {
		size_t size = 33 + ((per_worker[id] * 37 + id * 23) & 4095);
		unsigned char *p = kmem_zalloc(size, KM_NOSLEEP);
		uint64_t number;
		REQUIRE(p != NULL && p[0] == 0 && p[size - 1] == 0);
		mutex_enter(&lock);
		while (count == __arraycount(ring)) cv_wait(&writable, &lock);
		number = ++sent;
		p[0] = number & 255; p[size - 1] = (number >> 8) & 255;
		ring[tail].number = number; ring[tail].data = p; ring[tail].size = size;
		tail = (tail + 1) & 7; count++; sum_sent += number;
		cv_signal(&readable); mutex_exit(&lock);
		per_worker[id]++;
		if ((per_worker[id] & 3) == 0) REQUIRE(kpause(ws_name, false, 1, NULL) == EWOULDBLOCK);
	}
	mutex_enter(&lock); live_producers--;
	if (live_producers == 0) cv_broadcast(&readable);
	mutex_exit(&lock); rvm_bootsched_finish();
}
static void consumer(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	for (;;) {
		struct message m;
		mutex_enter(&lock);
		while (count == 0 && live_producers != 0) {
			int result = cv_timedwait(&readable, &lock, 2);
			REQUIRE(result == 0 || result == EWOULDBLOCK);
		}
		if (count == 0) { mutex_exit(&lock); break; }
		m = ring[head]; head = (head + 1) & 7; count--;
		cv_signal(&writable); mutex_exit(&lock);
		REQUIRE(m.data[0] == (m.number & 255) && m.data[m.size - 1] == ((m.number >> 8) & 255));
		kmem_free(m.data, m.size);
		mutex_enter(&lock); received++; sum_received += m.number; mutex_exit(&lock);
		per_worker[id]++;
		if ((per_worker[id] & 15) == 0) REQUIRE(kpause(ws_name, false, 1, NULL) == EWOULDBLOCK);
	}
	rvm_bootsched_finish();
}
void rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int i, finished, guard;
	int64_t pages;
	rvm_normal_early_init(bi); REQUIRE(rvm_boot_stage == 32);
	pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(NULL, NULL) && rvm_wait_init());
	mutex_init(&lock, MUTEX_DEFAULT, IPL_NONE); cv_init(&readable, ws_name); cv_init(&writable, ws_name);
	for (i = 0; i < 8; i++) {
		vaddr_t stack = uvm_uarea_system_alloc(curcpu()); REQUIRE(stack != 0);
		workers[i].l_cpu = curcpu(); workers[i].l_proc = &process;
		workers[i].l_stat = LSIDL; workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		uvm_lwp_setuarea(&workers[i], stack);
		uvm_lwp_fork(curlwp, &workers[i], NULL, 0, i < 4 ? producer : consumer, (void *)(uintptr_t)i);
		REQUIRE(rvm_bootsched_ready(&workers[i]));
	}
	stop_ns = rvm_clock_monotonic_ns() + RVM_WAIT_STRESS_NS;
	REQUIRE(rvm_async_clock_start()); rvm_async_preemption(1);
	do {
		REQUIRE(rvm_bootsched_sleep_until(rvm_clock_monotonic_ns() + 31000000) == 1);
		finished = 0; for (i = 0; i < 8; i++) finished += workers[i].l_stat == LSZOMB;
	} while (finished != 8);
	guard = rvm_intr_suspend(); REQUIRE(rvm_async_clock_stop());
	REQUIRE(sent > 100 && sent == received && sum_sent == sum_received && count == 0);
	REQUIRE(!cv_has_waiters(&readable) && !cv_has_waiters(&writable));
	for (i = 0; i < 8; i++) REQUIRE(per_worker[i] != 0 && rvm_bootsched_reap(&workers[i]));
	cv_destroy(&readable); cv_destroy(&writable); mutex_destroy(&lock);
	REQUIRE(rvm_bootsched_fini()); rvm_intr_resume(guard);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && kernel_map->nentries == 0);
	REQUIRE(curcpu()->ci_irq_locks == 0 && curlwp->l_md.md_irq_hold == 0);
	rvm_early_puts(ws_ok);
}
