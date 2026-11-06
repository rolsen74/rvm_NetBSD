/* Account only on-CPU serviced time; exclude sleeping and idle LWPs. */
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/kmem.h>
#include <sys/callout.h>
#include <uvm/uvm.h>
#include <machine/bootsched.h>
#include <machine/irq.h>
const char runtime_ok[] = "RVM MI LWP runtime, sleeping exclusion and idle accounting verified.\n";
const char runtime_bad[] = "LWP runtime regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(runtime_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp workers[2] __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static struct lwp *parent __attribute__((section(".data"))) = NULL;
static callout_t wakeups[2] __attribute__((section(".data"))) = { 0 };
static unsigned int resumed __attribute__((section(".data"))) = 0;
static int initial __attribute__((section(".data"))) = 0;
static void
runtime_wakeup(void *arg)
{
	REQUIRE(!cpu_intr_p() && curcpu()->ci_cpl == IPL_NONE && CURCPU_IDLE_P());
	REQUIRE(getticks() == initial + (arg == parent ? 8 : 5));
	REQUIRE(rvm_bootsched_ready(arg));
}
static void
worker(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	volatile uint64_t canary = 0x52554e54494d4500UL + id;
	unsigned char *p = kmem_zalloc(57, KM_NOSLEEP);
	REQUIRE(p != NULL && p[0] == 0 && p[56] == 0);
	p[0] = id + 1; p[56] = id + 0x51;
	REQUIRE(rvm_clock_wait() && rvm_clock_wait());
	if (id == 0) {
		callout_reset(&wakeups[0], 3, runtime_wakeup, &workers[0]);
		rvm_bootsched_sleep(); resumed++;
		REQUIRE(getticks() == initial + 5 && rvm_clock_wait());
	}
	REQUIRE(canary == 0x52554e54494d4500UL + id && p[0] == id + 1 && p[56] == id + 0x51);
	kmem_free(p, 57); rvm_bootsched_finish();
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int i;
	vaddr_t stack;
	int64_t before;
	struct bintime parent_before;
	uint64_t sys_before, idle_before;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 29 && rvm_bootsched_init());
	parent = curlwp;
	rvm_bootsched_yield(); parent_before = parent->l_rtime;
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES]; initial = getticks();
	sys_before = curcpu()->ci_schedstate.spc_cp_time[CP_SYS];
	idle_before = curcpu()->ci_schedstate.spc_cp_time[CP_IDLE];
	REQUIRE(rvm_bootsched_idle_init(NULL, NULL) && rvm_clock_start());
	for (i = 0; i < 2; i++) {
		callout_init(&wakeups[i], CALLOUT_MPSAFE);
		stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
		REQUIRE(stack != 0);
		workers[i].l_addr = (void *)stack; workers[i].l_cpu = curcpu();
		workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		workers[i].l_proc = &process; workers[i].l_stat = LSIDL;
		cpu_lwp_fork(parent, &workers[i], NULL, 0, worker, (void *)(uintptr_t)i);
		REQUIRE(rvm_bootsched_ready(&workers[i]));
	}
	callout_reset(&wakeups[1], 8, runtime_wakeup, parent);
	rvm_bootsched_sleep();
	REQUIRE(resumed == 1 && getticks() == initial + 8);
	REQUIRE(bintimecmp(&parent->l_rtime, &parent_before, ==));
	REQUIRE(workers[0].l_rtime.sec == 0 && workers[0].l_rtime.frac == 6 * ((1UL << 63) / RVM_CLOCK_HZ));
	REQUIRE(workers[1].l_rtime.sec == 0 && workers[1].l_rtime.frac == 4 * ((1UL << 63) / RVM_CLOCK_HZ));
	REQUIRE(workers[0].l_rticksum == 3 && workers[1].l_rticksum == 2);
	REQUIRE(curcpu()->ci_data.cpu_idlelwp->l_rtime.sec == 0 && curcpu()->ci_data.cpu_idlelwp->l_rtime.frac == 0);
	REQUIRE(curcpu()->ci_schedstate.spc_cp_time[CP_SYS] == sys_before + 5);
	REQUIRE(curcpu()->ci_schedstate.spc_cp_time[CP_IDLE] == idle_before + 3);
	for (i = 0; i < 2; i++) {
		REQUIRE(workers[i].l_stat == LSZOMB && rvm_bootsched_reap(&workers[i]));
		callout_stop(&wakeups[i]); callout_destroy(&wakeups[i]);
	}
	REQUIRE(rvm_clock_stop() && rvm_bootsched_fini());
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before && kernel_map->nentries == 0);
	rvm_early_puts(runtime_ok);
}
