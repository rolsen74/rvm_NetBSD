/* Real MI resume and zombie unlocks in a three-worker directed ring. */
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/mutex.h>
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/switch.h>
#include <machine/bootsched.h>
#include <machine/bootheap.h>
const char mi_ok[] = "RVM MI switch ring, zombie release and reaping verified.\n";
const char mi_bad[] = "MI switch regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(mi_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
#define NWORKERS 3
#define ROUNDS 32
static struct lwp workers[NWORKERS] __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static struct lwp *parent __attribute__((section(".data"))) = NULL;
static unsigned int progress[NWORKERS] __attribute__((section(".data"))) = { 0 };
static void
entry(void *arg)
{
	unsigned int id = (uintptr_t)arg, i, n;
	struct lwp *previous = id == 0 ? parent : &workers[id - 1];
	struct lwp *next = id == NWORKERS - 1 ? parent : &workers[id + 1];
	volatile uint64_t local[12];
	unsigned char *p = kmem_zalloc(93, KM_NOSLEEP);
	REQUIRE(p != NULL && p[0] == 0 && p[92] == 0);
	p[0] = id + 1; p[92] = 0x81 + id;
	for (i = 0; i < 12; i++) local[i] = 0x2345678900000000UL + 32 * id + i;
	for (n = 0; n < ROUNDS; n++) {
		REQUIRE(curlwp == &workers[id] && curcpu()->ci_onproc == curlwp);
		REQUIRE(curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_mtx_count == 0);
		REQUIRE(!mutex_owned(previous->l_mutex) && !(previous->l_pflag & LP_RUNNING));
		REQUIRE(curlwp->l_pflag & LP_RUNNING);
		REQUIRE(previous->l_stat == (n == ROUNDS - 1 && id != 0 ? LSZOMB : LSSLEEP));
		REQUIRE(p[0] == id + 1 && p[92] == 0x81 + id);
		for (i = 0; i < 12; i++) REQUIRE(local[i] == 0x2345678900000000UL + 32 * id + i);
		progress[id]++;
		if (n == ROUNDS - 1) {
			kmem_free(p, 93);
			rvm_bootsched_exit(next);
		}
		rvm_bootsched_switch(next);
	}
	REQUIRE(0);
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int i, n;
	vaddr_t stack;
	int64_t before, switches;
	long resident, wired;
	int flags;
	kmutex_t *lock;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32);
	parent = curlwp; flags = parent->l_pflag; lock = parent->l_mutex;
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	switches = curcpu()->ci_counts[CPU_COUNT_NSWTCH];
	resident = pmap_resident_count(pmap_kernel()); wired = pmap_wired_count(pmap_kernel());
	REQUIRE(rvm_bootsched_init() && !rvm_bootsched_init());
	REQUIRE(!rvm_bootsched_reap(parent));
	for (i = 0; i < NWORKERS; i++) {
		stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
		REQUIRE(stack != 0);
		workers[i].l_addr = (void *)stack; workers[i].l_cpu = curcpu();
		workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		workers[i].l_stat = LSIDL; workers[i].l_proc = &process;
		cpu_lwp_fork(parent, &workers[i], NULL, 0, entry, (void *)(uintptr_t)i);
		REQUIRE(!rvm_bootsched_reap(&workers[i]));
	}
	for (n = 0; n < ROUNDS; n++) {
		rvm_bootsched_switch(&workers[0]);
		REQUIRE(curlwp == parent && curcpu()->ci_onproc == parent);
		REQUIRE(curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_mtx_count == 0);
		REQUIRE(!rvm_bootsched_fini());
		for (i = 0; i < NWORKERS; i++) {
			REQUIRE(progress[i] == n + 1 && !(workers[i].l_pflag & LP_RUNNING));
			REQUIRE(!mutex_owned(workers[i].l_mutex));
			REQUIRE(workers[i].l_stat == (n == ROUNDS - 1 ? LSZOMB : LSSLEEP));
			if (n != ROUNDS - 1) REQUIRE(!rvm_bootsched_reap(&workers[i]));
		}
	}
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_NSWTCH] == switches + ROUNDS * (NWORKERS + 1));
	for (i = 0; i < NWORKERS; i++) {
		REQUIRE(rvm_bootsched_reap(&workers[i]) && !rvm_bootsched_reap(&workers[i]));
		REQUIRE(workers[i].l_addr == NULL && workers[i].l_md.md_pcb == NULL);
	}
	REQUIRE(rvm_bootsched_fini() && !rvm_bootsched_fini());
	REQUIRE(parent->l_mutex == lock && parent->l_pflag == flags);
	REQUIRE(curcpu()->ci_schedstate.spc_mutex == NULL && curcpu()->ci_schedstate.spc_lwplock == NULL);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident && pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(kernel_map->nentries == 0 && rvm_bootheap_live() == 0 && parent->l_nopreempt == 0);
	rvm_early_puts(mi_ok);
}
