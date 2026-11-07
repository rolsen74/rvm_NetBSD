/* Actual RTE-based recovery from unmapped and permission-denied accesses. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <uvm/uvm.h>
#include <machine/bootcopy.h>
#include <machine/pcb.h>
#include <machine/asyncirq.h>
#include <machine/bootsched.h>
#include <machine/clock.h>
const char copy_bad[] = "Kernel copy regression at line 0x";
const char copy_ok[] = "RVM kcopy fault return, overlap, PCB restoration and worker copies verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(copy_bad); rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static unsigned char data[128] __attribute__((section(".data"))) = { 0 };
static unsigned char output[128] __attribute__((section(".data"))) = { 0 };
static struct lwp workers[4] __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static unsigned int completed __attribute__((section(".data"))) = 0;
extern int rvm_copy_registers(const void *, void *, size_t);
static void worker(void *arg)
{
	unsigned int id = (uintptr_t)arg, i;
	unsigned char local[64];
	void *old = (void *)(uintptr_t)(0x12345000 + id);
	struct pcb *pcb = lwp_getpcb(curlwp);
	pcb->pcb_onfault = old;
	for (i = 0; i < 8; i++) {
		REQUIRE(kcopy(data, local, sizeof(local)) == 0 && local[23] == data[23]);
		REQUIRE(kcopy(NULL, local, 1) == EFAULT && pcb->pcb_onfault == old && curlwp->l_md.md_irq_hold == 0);
		rvm_bootsched_yield();
	}
	pcb->pcb_onfault = NULL; completed++; rvm_bootsched_finish();
}
void rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int i;
	vaddr_t va;
	struct vm_page *a, *b;
	struct pcb *pcb;
	void *old;
	int64_t pages;
	unsigned long faults;
	rvm_normal_early_init(bi); REQUIRE(rvm_boot_stage == 32 && rvm_kcopy_ready());
	pcb = lwp_getpcb(curlwp); old = (void *)0x12345678;
	pcb->pcb_onfault = old;
	for (i = 0; i < sizeof(data); i++) data[i] = i;
	REQUIRE(kcopy(NULL, NULL, 0) == 0);
	REQUIRE(kcopy(data, output, sizeof(data)) == 0 && pcb->pcb_onfault == old);
	for (i = 0; i < sizeof(data); i++) REQUIRE(output[i] == i);
	REQUIRE(kcopy(output, output + 3, 100) == 0);
	for (i = 0; i < 100; i++) REQUIRE(output[i + 3] == i);
	REQUIRE(kcopy(output + 3, output, 100) == 0);
	for (i = 0; i < 100; i++) REQUIRE(output[i] == i);
	faults = rvm_kcopy_faults();
	REQUIRE(kcopy(NULL, output, 1) == EFAULT && rvm_kcopy_faults() == faults + 1);
	REQUIRE(kcopy(data, NULL, 1) == EFAULT && pcb->pcb_onfault == old);
	REQUIRE(kcopy(data, (void *)(uintptr_t)rvm_early_init, 1) == EFAULT);
	REQUIRE(kcopy((void *)UINTPTR_MAX, output, 2) == EFAULT);
	REQUIRE(kcopy(data, (void *)UINTPTR_MAX, 2) == EFAULT);
	REQUIRE(rvm_copy_registers(NULL, output, 1) == 1 && pcb->pcb_onfault == old);
	pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	a = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO | UVM_PGA_USERESERVE);
	b = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO | UVM_PGA_USERESERVE); REQUIRE(a && b);
	va = uvm_km_alloc(kernel_map, 4 * PAGE_SIZE, 0, UVM_KMF_VAONLY | UVM_KMF_NOWAIT); REQUIRE(va);
	pmap_kenter_pa(va, VM_PAGE_TO_PHYS(a), VM_PROT_READ | VM_PROT_WRITE, 0);
	pmap_kenter_pa(va + 3 * PAGE_SIZE, VM_PAGE_TO_PHYS(b), VM_PROT_READ | VM_PROT_WRITE, 0);
	unsigned char *src = (void *)(va + 3 * PAGE_SIZE);
	for (i = 0; i < 8; i++) src[i] = i + 23;
	REQUIRE(kcopy(src, (void *)(va + PAGE_SIZE - 4), 8) == EFAULT);
	for (i = 0; i < 4; i++) REQUIRE(*(unsigned char *)(va + PAGE_SIZE - 4 + i) == i + 23);
	REQUIRE(kcopy((void *)(va + PAGE_SIZE - 4), output, 8) == EFAULT);
	for (i = 0; i < 4; i++) REQUIRE(output[i] == i + 23);
	pmap_page_protect(b, VM_PROT_NONE);
	pmap_kenter_pa(va + 3 * PAGE_SIZE, VM_PAGE_TO_PHYS(b), VM_PROT_WRITE, 0);
	REQUIRE(kcopy(src, output, 1) == EFAULT);
	pmap_page_protect(a, VM_PROT_NONE); pmap_page_protect(b, VM_PROT_NONE);
	uvm_km_free(kernel_map, va, 4 * PAGE_SIZE, UVM_KMF_VAONLY);
	uvm_pagefree(a); uvm_pagefree(b);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && pcb->pcb_onfault == old);
	pcb->pcb_onfault = NULL;
	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(NULL, NULL));
	for (i = 0; i < 4; i++) {
		vaddr_t stack = uvm_uarea_system_alloc(NULL); REQUIRE(stack);
		workers[i].l_cpu = curcpu(); workers[i].l_proc = &process;
		workers[i].l_stat = LSIDL; workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		uvm_lwp_setuarea(&workers[i], stack);
		uvm_lwp_fork(curlwp, &workers[i], NULL, 0, worker, (void *)(uintptr_t)i);
		REQUIRE(rvm_bootsched_ready(&workers[i]));
	}
	REQUIRE(rvm_async_clock_start()); rvm_async_preemption(1);
	while (completed != 4) rvm_bootsched_yield();
	REQUIRE(rvm_async_clock_stop());
	for (i = 0; i < 4; i++) REQUIRE(rvm_bootsched_reap(&workers[i]));
	REQUIRE(rvm_bootsched_fini() && curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages);
	rvm_early_puts(copy_ok); rvm_boot_stop();
}
