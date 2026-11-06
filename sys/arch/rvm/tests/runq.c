/* Real priority queues, equal-priority FIFO rotation and self selection. */
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/switch.h>
#include <machine/bootsched.h>
#include <machine/bootheap.h>
#define ROUNDS 16
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(runq_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
const char runq_bad[] = "MI run queue regression at line 0x";
const char runq_ok[] = "RVM MI priorities, FIFO rotation and queue cleanup verified.\n";
static struct lwp workers[3] __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static unsigned int trace[3 * ROUNDS] __attribute__((section(".data"))) = { 0 };
static unsigned int position __attribute__((section(".data"))) = 0;
static void
entry(void *arg)
{
	unsigned int id = (uintptr_t)arg, n;
	volatile uint64_t canary = 0x72756e7100000000UL + id;
	unsigned char *p = kmem_zalloc(93, KM_NOSLEEP);
	REQUIRE(p != NULL && p[0] == 0 && p[92] == 0);
	p[0] = id + 1; p[92] = id + 0x51;
	for (n = 0; n < ROUNDS; n++) {
		REQUIRE(curlwp == &workers[id] && curcpu()->ci_onproc == curlwp);
		REQUIRE(curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_mtx_count == 0);
		REQUIRE(curlwp->l_stat == LSONPROC && (curlwp->l_pflag & LP_RUNNING));
		REQUIRE(canary == 0x72756e7100000000UL + id && p[0] == id + 1 && p[92] == id + 0x51);
		REQUIRE(position < __arraycount(trace));
		trace[position++] = id;
		if (n == ROUNDS - 1) { kmem_free(p, 93); rvm_bootsched_finish(); }
		rvm_bootsched_yield();
	}
	REQUIRE(0);
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int i;
	vaddr_t stack;
	int64_t before, switches;
	long resident, wired;
	pri_t priority;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	switches = curcpu()->ci_counts[CPU_COUNT_NSWTCH];
	resident = pmap_resident_count(pmap_kernel()); wired = pmap_wired_count(pmap_kernel());
	priority = curlwp->l_priority; curlwp->l_priority = 0;
	REQUIRE(rvm_bootsched_init());
	/* The only runnable LWP is self: release both locks without a stack switch. */
	for (i = 0; i < 8; i++) rvm_bootsched_yield();
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_NSWTCH] == switches);
	for (i = 0; i < 3; i++) {
		stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
		REQUIRE(stack != 0);
		workers[i].l_addr = (void *)stack; workers[i].l_cpu = curcpu();
		workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		workers[i].l_proc = &process; workers[i].l_stat = LSIDL;
		workers[i].l_priority = i == 0 ? 32 : 96;
		cpu_lwp_fork(curlwp, &workers[i], NULL, 0, entry, (void *)(uintptr_t)i);
		workers[i].l_auxprio = PRI_COUNT;
		REQUIRE(!rvm_bootsched_ready(&workers[i]) && workers[i].l_stat == LSIDL);
		workers[i].l_auxprio = 0;
		REQUIRE(rvm_bootsched_ready(&workers[i]) && !rvm_bootsched_ready(&workers[i]));
		REQUIRE(!rvm_bootsched_reap(&workers[i]));
	}
	REQUIRE(curcpu()->ci_schedstate.spc_count == 3 && !rvm_bootsched_fini());
	/* The owner has lower priority: it resumes only after all workers finish. */
	rvm_bootsched_yield();
	REQUIRE(position == 3 * ROUNDS);
	for (i = 0; i < 2 * ROUNDS; i++) REQUIRE(trace[i] == 1 + (i & 1));
	for (; i < 3 * ROUNDS; i++) REQUIRE(trace[i] == 0);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_NSWTCH] == switches + 2 * ROUNDS + 2);
	REQUIRE(curcpu()->ci_schedstate.spc_count == 0 && curcpu()->ci_schedstate.spc_mcount == 0);
	for (i = 0; i < __arraycount(curcpu()->ci_schedstate.spc_bitmap); i++)
		REQUIRE(curcpu()->ci_schedstate.spc_bitmap[i] == 0);
	for (i = 0; i < 3; i++) {
		REQUIRE(workers[i].l_stat == LSZOMB && !(workers[i].l_pflag & LP_RUNNING));
		REQUIRE(!rvm_bootsched_ready(&workers[i]));
		REQUIRE(rvm_bootsched_reap(&workers[i]));
	}
	REQUIRE(rvm_bootsched_fini()); curlwp->l_priority = priority;
	REQUIRE(curcpu()->ci_schedstate.spc_queue == NULL);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident && pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(kernel_map->nentries == 0 && rvm_bootheap_live() == 0);
	rvm_early_puts(runq_ok);
}
