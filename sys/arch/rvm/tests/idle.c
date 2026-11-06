/* Idle stack, repeated sleep/wakeup, and exit with an empty run queue. */
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/switch.h>
#include <machine/bootsched.h>
#include <machine/bootheap.h>
#define ROUNDS 32
const char idle_bad[] = "MI idle regression at line 0x";
const char idle_ok[] = "RVM idle stack, sleeping workers and empty-queue exit verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(idle_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp child __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static struct lwp *parent __attribute__((section(".data"))) = NULL;
static unsigned int phase __attribute__((section(".data"))) = 0;
static unsigned int wakes __attribute__((section(".data"))) = 0;
static unsigned int progress __attribute__((section(".data"))) = 0;

static void
poll_event(void *arg)
{
	struct lwp *idle = curcpu()->ci_data.cpu_idlelwp;
	REQUIRE(arg == &child && curlwp == idle && curcpu()->ci_onproc == idle);
	REQUIRE(idle != parent && idle != &child && idle->l_stat == LSIDL);
	REQUIRE((idle->l_flag & LW_IDLE) && (idle->l_pflag & LP_RUNNING));
	REQUIRE(curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_mtx_count == 0);
	REQUIRE(curcpu()->ci_schedstate.spc_count == 0 && parent->l_stat == LSSLEEP);
	REQUIRE(!(parent->l_pflag & LP_RUNNING) && !(child.l_pflag & LP_RUNNING));
	REQUIRE(!rvm_bootsched_ready(idle));
	if (phase == 3) {
		REQUIRE(child.l_stat == LSZOMB && rvm_bootsched_ready(parent));
	} else {
		REQUIRE((phase == 1 || phase == 2) && child.l_stat == LSSLEEP);
		REQUIRE(rvm_bootsched_ready(&child) && !rvm_bootsched_ready(&child));
	}
	phase = 0; wakes++;
}

static void
worker(void *arg)
{
	unsigned int n;
	volatile uint64_t canary = 0x69646c6553544143UL;
	unsigned char *p = kmem_zalloc(73, KM_NOSLEEP);
	REQUIRE(arg == &child && p != NULL && p[0] == 0 && p[72] == 0);
	p[0] = 0x47; p[72] = 0x91;
	for (n = 0; n < ROUNDS; n++) {
		phase = 1;
		rvm_bootsched_sleep();
		REQUIRE(phase == 0 && curlwp == &child && child.l_stat == LSONPROC);
		REQUIRE(canary == 0x69646c6553544143UL && p[0] == 0x47 && p[72] == 0x91);
		progress++;
		REQUIRE(rvm_bootsched_ready(parent));
		if (n == ROUNDS - 1) { kmem_free(p, 73); rvm_bootsched_finish(); }
		phase = 2;
		rvm_bootsched_sleep();
		REQUIRE(phase == 0);
	}
	REQUIRE(0);
}
static void
finish_empty(void *arg)
{
	REQUIRE(arg == &child && parent->l_stat == LSSLEEP && phase == 3);
	rvm_bootsched_finish();
}
static void
prepare(void (*func)(void *))
{
	vaddr_t stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
	REQUIRE(stack != 0);
	memset(&child, 0, sizeof(child));
	child.l_addr = (void *)stack; child.l_cpu = curcpu();
	child.l_flag = LW_SYSTEM; child.l_pflag = LP_MPSAFE;
	child.l_stat = LSIDL; child.l_proc = &process;
	cpu_lwp_fork(parent, &child, NULL, 0, func, &child);
	REQUIRE(rvm_bootsched_ready(&child));
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int n;
	int64_t before;
	long resident, wired;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32);
	parent = curlwp;
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel()); wired = pmap_wired_count(pmap_kernel());
	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(poll_event, &child));
	REQUIRE(!rvm_bootsched_idle_init(NULL, NULL));
	REQUIRE(!rvm_bootsched_reap(curcpu()->ci_data.cpu_idlelwp));
	prepare(worker);
	for (n = 0; n < ROUNDS; n++) {
		rvm_bootsched_sleep();
		REQUIRE(curlwp == parent && progress == n + 1 && wakes == 2 * n + 1);
		REQUIRE(curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_mtx_count == 0);
		REQUIRE(!(curcpu()->ci_data.cpu_idlelwp->l_pflag & LP_RUNNING));
		REQUIRE(child.l_stat == (n == ROUNDS - 1 ? LSZOMB : LSSLEEP));
		if (n != ROUNDS - 1) REQUIRE(!rvm_bootsched_reap(&child));
	}
	REQUIRE(rvm_bootsched_reap(&child));
	phase = 3; prepare(finish_empty);
	rvm_bootsched_sleep();
	REQUIRE(phase == 0 && wakes == 2 * ROUNDS && child.l_stat == LSZOMB);
	REQUIRE(rvm_bootsched_reap(&child));
	REQUIRE(rvm_bootsched_fini() && curcpu()->ci_data.cpu_idlelwp == NULL);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident && pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(kernel_map->nentries == 0 && rvm_bootheap_live() == 0);
	rvm_early_puts(idle_ok);
}
