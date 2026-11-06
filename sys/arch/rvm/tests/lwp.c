/* MI first entry: previous-LWP unlock, zombie handoff and u-area ownership. */
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/mutex.h>
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/switch.h>
#include <machine/bootheap.h>
const char lwp_ok[] = "RVM MI LWP startup and u-area lifecycle verified.\n";
const char lwp_bad[] = "Kernel LWP regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(lwp_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp target __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static kmutex_t lock __attribute__((section(".data"))) = { 0 };
static struct lwp *parent __attribute__((section(".data"))) = NULL;
static unsigned int calls __attribute__((section(".data"))) = 0;
static void
entry(void *arg)
{
	unsigned char *p;
	REQUIRE(curlwp == &target && arg == &target);
	REQUIRE(curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_mtx_count == 0);
	REQUIRE(!mutex_owned(&lock) && !(parent->l_pflag & LP_RUNNING));
	REQUIRE((parent->l_pflag & LP_MPSAFE) != 0);
	REQUIRE((target.l_pflag & LP_RUNNING) && target.l_md.md_startup == 0);
	p = kmem_zalloc(57, KM_NOSLEEP);
	REQUIRE(p != NULL && p[0] == 0 && p[56] == 0);
	p[0] = 0x76; p[56] = 0x39;
	kpreempt_disable(); kpreempt_enable();
	REQUIRE(p[0] == 0x76 && p[56] == 0x39);
	kmem_free(p, 57);
	calls++;
	splsched(); parent->l_pflag |= LP_RUNNING;
	curcpu()->ci_onproc = parent;
	cpu_switchto(&target, parent, false);
	REQUIRE(0);
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	vaddr_t stack;
	int64_t before;
	long resident, wired;
	unsigned int n, flags, stat;
	kmutex_t *saved_lock;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32);
	parent = curlwp; flags = parent->l_pflag; stat = parent->l_stat;
	saved_lock = parent->l_mutex;
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel()); wired = pmap_wired_count(pmap_kernel());
	mutex_init(&lock, MUTEX_SPIN, IPL_SCHED);
	for (n = 0; n < 12; n++) {
		stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
		REQUIRE(stack != 0);
		memset(&target, 0, sizeof(target));
		target.l_addr = (void *)stack; target.l_cpu = curcpu();
		target.l_flag = LW_SYSTEM; target.l_pflag = LP_MPSAFE;
		target.l_stat = LSIDL; target.l_proc = &process;
		cpu_lwp_fork(parent, &target, NULL, 0, entry, &target);
		REQUIRE(target.l_addr == (void *)stack && target.l_md.md_pcb == (void *)stack);
		REQUIRE(target.l_md.md_tf == NULL && target.l_md.md_startup == 1);
		REQUIRE(target.l_md.md_kstack_base > stack && target.l_md.md_kstack_size < USPACE);
		target.l_stat = LSONPROC; target.l_pflag |= LP_RUNNING;
		parent->l_pflag = LP_RUNNING | LP_MPSAFE;
		parent->l_stat = (n & 1) ? LSZOMB : LSONPROC;
		parent->l_mutex = &lock;
		splsched(); mutex_spin_enter(&lock);
		curcpu()->ci_mtx_count = -2; curcpu()->ci_onproc = &target;
		REQUIRE(cpu_switchto(parent, &target, false) == &target && calls == n + 1);
		REQUIRE(curcpu()->ci_cpl == IPL_SCHED && curcpu()->ci_mtx_count == 0);
		spl0(); target.l_stat = LSZOMB; target.l_pflag &= ~LP_RUNNING;
		cpu_lwp_free(&target, 0); cpu_lwp_free2(&target);
		REQUIRE(target.l_addr == (void *)stack && target.l_md.md_pcb == NULL);
		REQUIRE(target.l_md.md_kstack_base == 0 && target.l_md.md_startup == 0);
		uvm_km_free(kernel_map, (vaddr_t)target.l_addr, USPACE, UVM_KMF_WIRED);
		target.l_addr = NULL;
	}
	mutex_destroy(&lock);
	parent->l_pflag = flags; parent->l_stat = stat; parent->l_mutex = saved_lock;
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident && pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(kernel_map->nentries == 0 && rvm_bootheap_live() == 0);
	rvm_early_puts(lwp_ok);
}
