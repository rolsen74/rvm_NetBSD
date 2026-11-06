/* Guarded MD u-area lifecycle with actual scheduling and resource reuse. */
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/uarea.h>
#include <machine/switch.h>
#include <machine/bootsched.h>
#include <machine/irq.h>
#include <machine/mmu.h>
const char ua_bad[] = "Guarded u-area regression at line 0x";
const char ua_ok[] = "RVM guarded u-area zeroing, lifetime and reuse verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(ua_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp child __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static void *addresses[RVM_UAREA_SLOTS] __attribute__((section(".data"))) = { 0 };
static void
worker(void *arg)
{
	volatile uint64_t local[8];
	unsigned char *p = kmem_zalloc(39, KM_NOSLEEP);
	unsigned int i;
	REQUIRE(arg == &child && p != NULL && p[0] == 0 && p[38] == 0);
	p[0] = 0x31; p[38] = 0x94;
	for (i = 0; i < 8; i++) local[i] = 0x5541524541475541UL + i;
	REQUIRE(rvm_clock_wait());
	for (i = 0; i < 8; i++) REQUIRE(local[i] == 0x5541524541475541UL + i);
	REQUIRE(p[0] == 0x31 && p[38] == 0x94);
	kmem_free(p, 39); rvm_bootsched_finish();
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int i, n;
	vaddr_t base;
	paddr_t pa;
	unsigned long flags;
	int64_t before;
	long resident, wired;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel()); wired = pmap_wired_count(pmap_kernel());
	REQUIRE(cpu_uarea_alloc(false) == NULL && !cpu_uarea_free(NULL));
	for (i = 0; i < RVM_UAREA_SLOTS; i++) {
		addresses[i] = cpu_uarea_alloc(true);
		REQUIRE(addresses[i] != NULL);
		base = (vaddr_t)addresses[i];
		REQUIRE((base & PAGE_MASK) == 0 && !pmap_extract(pmap_kernel(), base - PAGE_SIZE, &pa));
		for (n = 0; n < USPACE / PAGE_SIZE; n++) {
			REQUIRE(rvm_boot_pmap_lookup(base + n * PAGE_SIZE, &pa, &flags));
			REQUIRE((flags & (RVM_PTE_READ | RVM_PTE_WRITE | RVM_PTE_SUPERVISOR)) ==
			    (RVM_PTE_READ | RVM_PTE_WRITE | RVM_PTE_SUPERVISOR) && !(flags & RVM_PTE_EXEC));
			REQUIRE(PHYS_TO_VM_PAGE(pa)->wire_count == 1);
			REQUIRE(*(volatile uint64_t *)(base + n * PAGE_SIZE) == 0);
			REQUIRE(*(volatile uint64_t *)(base + (n + 1) * PAGE_SIZE - 8) == 0);
		}
		*(volatile uint64_t *)base = 0x1234;
	}
	REQUIRE(cpu_uarea_alloc(true) == NULL);
	for (i = 0; i < RVM_UAREA_SLOTS; i++) {
		REQUIRE(!cpu_uarea_free((char *)addresses[i] + 8));
		REQUIRE(cpu_uarea_free(addresses[i]) && !cpu_uarea_free(addresses[i]));
	}
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before && kernel_map->nentries == 0);
	REQUIRE(rvm_bootsched_init() && rvm_clock_start());
	for (i = 0; i < 8; i++) {
		void *area = cpu_uarea_alloc(true);
		REQUIRE(area != NULL && *(volatile uint64_t *)area == 0);
		memset(&child, 0, sizeof(child));
		child.l_addr = area; child.l_cpu = curcpu(); child.l_proc = &process;
		child.l_flag = LW_SYSTEM; child.l_pflag = LP_MPSAFE; child.l_stat = LSIDL;
		cpu_lwp_fork(curlwp, &child, NULL, 0, worker, &child);
		REQUIRE(rvm_bootsched_ready(&child)); rvm_bootsched_yield();
		REQUIRE(child.l_stat == LSZOMB && rvm_bootsched_reap(&child));
		REQUIRE(child.l_addr == NULL && child.l_md.md_pcb == NULL);
		REQUIRE(!pmap_extract(pmap_kernel(), (vaddr_t)area, &pa));
	}
	REQUIRE(rvm_clock_stop() && rvm_bootsched_fini());
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before && kernel_map->nentries == 0);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident && pmap_wired_count(pmap_kernel()) == wired);
	rvm_early_puts(ua_ok);
}
