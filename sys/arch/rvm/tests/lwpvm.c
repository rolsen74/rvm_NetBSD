/* Selected MI uvm_glue bodies: attach/fork/teardown with guarded and plain areas. */
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/bootsched.h>
#include <machine/irq.h>
#include <machine/trap.h>
const char vm_ok[] = "RVM MI u-area attachment, fork, exit and fallback verified.\n";
const char vm_bad[] = "MI LWP VM regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(vm_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp child __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static void
setup(vaddr_t base)
{
	memset(&child, 0, sizeof(child));
	child.l_cpu = curcpu(); child.l_proc = &process;
	child.l_stat = LSIDL; child.l_flag = LW_SYSTEM; child.l_pflag = LP_MPSAFE;
	REQUIRE(uvm_lwp_getuarea(&child) == 0);
	uvm_lwp_setuarea(&child, base);
	REQUIRE(uvm_lwp_getuarea(&child) == base && child.l_addr == (void *)base);
}
static void
entry(void *arg)
{
	volatile uint64_t canary = 0x55564d4c5750564dUL;
	unsigned char *p = kmem_zalloc(41, KM_NOSLEEP);
	REQUIRE(arg == &child && p != NULL && p[0] == 0 && p[40] == 0);
	p[0] = 0x61; p[40] = 0x82;
	REQUIRE(uvm_lwp_getuarea(curlwp) == (vaddr_t)curlwp->l_addr);
	REQUIRE(rvm_clock_wait() && rvm_svc_probe());
	REQUIRE(canary == 0x55564d4c5750564dUL && p[0] == 0x61 && p[40] == 0x82);
	kmem_free(p, 41); rvm_bootsched_finish();
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	vaddr_t base;
	unsigned int n;
	paddr_t pa;
	int64_t before;
	long resident, wired;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel()); wired = pmap_wired_count(pmap_kernel());
	REQUIRE(uvm_uarea_system_alloc((struct cpu_info *)1) == 0);
	base = uvm_uarea_system_alloc(NULL); REQUIRE(base != 0);
	uvm_uarea_system_free(base); /* Unattached allocation. */
	base = uvm_uarea_system_alloc(NULL); REQUIRE(base != 0); setup(base);
	uvm_lwp_exit(&child); /* Attached but never forked. */
	REQUIRE(uvm_lwp_getuarea(&child) == 0 && !pmap_extract(pmap_kernel(), base, &pa));
	base = uvm_uarea_system_alloc(NULL); REQUIRE(base != 0); setup(base);
	uvm_lwp_fork(curlwp, &child, NULL, 0, entry, &child);
	cpu_lwp_free2(&child); uvm_lwp_exit(&child); /* Forked, never scheduled. */
	REQUIRE(uvm_lwp_getuarea(&child) == 0 && !pmap_extract(pmap_kernel(), base, &pa));
	REQUIRE(rvm_bootsched_init() && rvm_clock_start());
	for (n = 0; n < 8; n++) {
		base = (n & 1) ? uvm_km_alloc(kernel_map, USPACE, 0,
		    UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO) : uvm_uarea_system_alloc(curcpu());
		REQUIRE(base != 0); setup(base);
		uvm_lwp_fork(curlwp, &child, NULL, 0, entry, &child);
		REQUIRE(child.l_md.md_pcb == child.l_addr && rvm_bootsched_ready(&child));
		rvm_bootsched_yield();
		REQUIRE(child.l_stat == LSZOMB && rvm_bootsched_reap(&child));
		REQUIRE(uvm_lwp_getuarea(&child) == 0 && child.l_md.md_pcb == NULL);
		REQUIRE(!pmap_extract(pmap_kernel(), base, &pa));
	}
	REQUIRE(rvm_clock_stop() && rvm_bootsched_fini());
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before && kernel_map->nentries == 0);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident && pmap_wired_count(pmap_kernel()) == wired);
	rvm_early_puts(vm_ok);
}
