/* EARLY-only integration check, after MI threads/clocks start, before rootconf. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/mutex.h>
#include <sys/cpu.h>
#include <sys/atomic.h>
#include <sys/pool.h>
#include <machine/switch.h>
#include <machine/cache.h>
#ifdef RVM_KERNEL_ICACHE
#include <machine/generic.h>
#include <machine/pmap.h>
#include <machine/asyncirq.h>
#include <machine/psl.h>
#endif

void rvm_kernel_smoke(void);
void rvm_user_smoke(void);
void rvm_process_smoke(void);
void rvm_disk_smoke(void);
#define CHECK(c) do { if (!(c)) panic("RVM normal kernel check: " #c); } while (0)

struct smoke_state {
	kmutex_t lock;
	kcondvar_t cv;
	callout_t timer;
	bool done;
};

#ifdef RVM_KERNEL_ICACHE
/* Same instructions and input, with only CR.I changed between runs. */
static volatile uint64_t instruction_seed = 1234567;
static uint64_t __noinline
instruction_work(uint64_t value, unsigned count)
{
	while (count--) {
		value ^= value << 7;
		value ^= value >> 9;
		value += 0x12345;
	}
	return value;
}

static void
instruction_cache_check(void)
{
	volatile struct rvm_general_driver *gd = (void *)RVM_GENERIC_ADDR;
	uint64_t saved_cr = rvm_intr_disable(), start, uncached, cached, a, b;
	uint64_t cr = saved_cr & ~RVM_CR_INTERRUPT;
	if (!(cr & RVM_CR_INSTCACHE)) { rvm_irq_set_cr(saved_cr); return; }
	/* IRQs remain masked during each short pair to avoid scheduler noise.
	 * Both modes use the same data path; only instruction fetch changes. */
	for (unsigned run = 0; run < 3; run++) {
#ifdef RVM_MMU_TLB
		uint64_t walk, reference;
		rvm_irq_set_cr(cr & ~(RVM_CR_INSTCACHE | RVM_CR_TLB));
		start = gd->monotonic_ns;
		reference = instruction_work(instruction_seed, 20000);
		walk = gd->monotonic_ns - start;
#endif
		rvm_irq_set_cr(cr & ~RVM_CR_INSTCACHE);
		start = gd->monotonic_ns;
		a = instruction_work(instruction_seed, 20000);
		uncached = gd->monotonic_ns - start;
		rvm_irq_set_cr(cr | RVM_CR_INSTCACHE);
		CHECK(rvm_boot_get_cr() == (cr | RVM_CR_INSTCACHE));
		start = gd->monotonic_ns;
		b = instruction_work(instruction_seed, 20000);
		cached = gd->monotonic_ns - start;
		CHECK(a == b && !(rvm_boot_get_cr() & RVM_CR_DATACACHE));
#ifdef RVM_MMU_TLB
		CHECK(reference == a);
		printf("RVM TLB comparison %u: walk=%llu us tlb=%llu us tlb+icache=%llu us\n",
		    run + 1, (unsigned long long)(walk / 1000),
		    (unsigned long long)(uncached / 1000), (unsigned long long)(cached / 1000));
#endif
		printf("RVM I-cache comparison %u: uncached=%llu us cached=%llu us\n",
		    run + 1, (unsigned long long)(uncached / 1000),
		    (unsigned long long)(cached / 1000));
	}
	rvm_irq_set_cr(saved_cr);
}
#endif

static void
smoke_timeout(void *arg)
{
	struct smoke_state *state = arg;
	CHECK(cpu_softintr_p());
	CHECK(rvm_get_user_sp() == 0);
	mutex_enter(&state->lock);
	state->done = true;
	cv_signal(&state->cv);
	mutex_exit(&state->lock);
}

void
rvm_kernel_smoke(void)
{
	struct smoke_state state;
	unsigned char *small, *large;
	uint64_t *abi_aligned;
	struct pool byte_pool;
	void *item;
	int begin;
	uint64_t oldusp;

#ifdef RVM_KERNEL_ICACHE
	instruction_cache_check();
#endif

	small = kmem_zalloc(8, KM_SLEEP);
	large = kmem_alloc(PAGE_SIZE + 37, KM_SLEEP);
	for (size_t i = 0; i < 8; i++) CHECK(small[i] == 0);
	memset(large, 0x5a, PAGE_SIZE + 37);
	/* Exercise the ABI's four-byte alignment of 64-bit scalar objects. */
	abi_aligned = (void *)(large + 4);
	CHECK(((uintptr_t)abi_aligned & 7) == 4);
	atomic_store_release(abi_aligned, UINT64_C(0x123456789abcdef0));
	CHECK(atomic_load_acquire(abi_aligned) == UINT64_C(0x123456789abcdef0));
	CHECK(atomic_cas_64(abi_aligned, UINT64_C(0x123456789abcdef0), 7) ==
	    UINT64_C(0x123456789abcdef0));
	CHECK(atomic_add_64_nv(abi_aligned, -1) == 6);
	CHECK(kcopy(large + PAGE_SIZE, small, 8) == 0);
	for (size_t i = 0; i < 8; i++) CHECK(small[i] == 0x5a);
	CHECK(kcopy(NULL, small, 1) == EFAULT);
	kmem_free(large, PAGE_SIZE + 37);
	kmem_free(small, 8);
	/* An off-page bitmap must cover all 65536 one-byte items in a page. */
	pool_init(&byte_pool, 1, 1, 0, PR_NOTOUCH, "rvm-byte", NULL, IPL_NONE);
	CHECK(byte_pool.pr_itemsperpage == PAGE_SIZE);
	item = pool_get(&byte_pool, PR_WAITOK);
	CHECK(item != NULL);
	*(unsigned char *)item = 0x3c;
	pool_put(&byte_pool, item);
	pool_destroy(&byte_pool);
	/* Clock/CV checks afterwards also verify the probe restored IRQ delivery. */
	rvm_user_smoke();
	memset(&state, 0, sizeof(state));
	mutex_init(&state.lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&state.cv, "rvmsmoke");
	callout_init(&state.timer, CALLOUT_MPSAFE);
	mutex_enter(&state.lock);
	oldusp = rvm_get_user_sp();
	rvm_set_user_sp(VM_MAXUSER_ADDRESS - 64);
	begin = getticks();
	callout_reset(&state.timer, 2, smoke_timeout, &state);
	while (!state.done) {
		if (cv_timedwait(&state.cv, &state.lock, 2 * hz) != 0)
			panic("RVM normal kernel: softclock/CV timeout");
	}
	CHECK((unsigned int)(getticks() - begin) >= 2);
	CHECK(rvm_get_user_sp() == VM_MAXUSER_ADDRESS - 64);
	rvm_set_user_sp(oldusp);
	mutex_exit(&state.lock);
	callout_halt(&state.timer, NULL);
	callout_destroy(&state.timer);
	cv_destroy(&state.cv);
	mutex_destroy(&state.lock);
	printf("RVM normal kernel: heap, kcopy, hardclock, softclock and CV wakeup verified\n");
	printf("RVM normal kernel: four-byte-aligned 64-bit atomic operations verified\n");
	printf("RVM normal kernel: 65536-item pool bitmap verified\n");
	printf("RVM normal kernel: per-LWP user stack pointer switching verified\n");
	rvm_process_smoke();
	rvm_disk_smoke();
}
