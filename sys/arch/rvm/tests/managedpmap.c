/* Managed aliases, sticky R/M state, wiring and page-wide protections. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <uvm/uvm.h>
#include <machine/boot.h>
#include <machine/mmu.h>
#include <machine/asyncirq.h>
#include <machine/bootsched.h>
#include <machine/clock.h>
const char pv_bad[] = "Managed pmap regression at line 0x";
const char pv_ok[] = "RVM managed pmap aliases, access bits, wiring and protections verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(pv_bad); rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static void clear(struct vm_page *pg)
{
	pmap_clear_modify(pg); pmap_clear_reference(pg);
	REQUIRE(!pmap_is_modified(pg) && !pmap_is_referenced(pg));
}
void rvm_early_init(const struct rvm_bootinfo *bi)
{
	struct vm_page *a, *b;
	vaddr_t base; paddr_t pa, pb, out;
	volatile uint64_t *v0, *v1, *v3, value;
	unsigned long flags;
	long resident, wired;
	int64_t pages;
	unsigned int i, round;
	rvm_normal_early_init(bi); REQUIRE(rvm_boot_stage == 32);
	pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel()); wired = pmap_wired_count(pmap_kernel());
	for (round = 0; round < 3; round++) {
		a = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO | UVM_PGA_USERESERVE);
		b = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO | UVM_PGA_USERESERVE);
		REQUIRE(a && b && a->mdpage.pv_head == NULL && b->mdpage.pv_head == NULL);
		pa = VM_PAGE_TO_PHYS(a); pb = VM_PAGE_TO_PHYS(b);
		base = uvm_km_alloc(kernel_map, 64 * PAGE_SIZE, 0, UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
		REQUIRE(base != 0); clear(a); clear(b);
		REQUIRE(pmap_enter(pmap_kernel(), base, pa, VM_PROT_READ | VM_PROT_WRITE, 0) == 0);
		REQUIRE(pmap_enter(pmap_kernel(), base + PAGE_SIZE, pa, VM_PROT_READ | VM_PROT_WRITE, PMAP_WIRED) == 0);
		REQUIRE(pmap_enter(pmap_kernel(), base + 2 * PAGE_SIZE, pb, VM_PROT_READ | VM_PROT_WRITE, 0) == 0);
		REQUIRE(pmap_resident_count(pmap_kernel()) == resident + 3 && pmap_wired_count(pmap_kernel()) == wired + 1);
		REQUIRE(!pmap_is_referenced(a) && !pmap_is_modified(a));
		v0 = (void *)base; v1 = (void *)(base + PAGE_SIZE); v3 = (void *)(base + 3 * PAGE_SIZE);
		value = *v0; REQUIRE(value == 0 && pmap_is_referenced(a) && !pmap_is_modified(a));
		REQUIRE(pmap_clear_reference(a) && !pmap_is_referenced(a));
		*v1 = 0x123456789abcdefUL;
		REQUIRE(pmap_is_referenced(a) && pmap_is_modified(a));
		REQUIRE(pmap_clear_reference(a) && !pmap_is_referenced(a) && pmap_is_modified(a));
		REQUIRE(pmap_clear_modify(a) && !pmap_is_modified(a) && !pmap_is_referenced(a));
		value = *(volatile uint64_t *)(uintptr_t)pa;
		REQUIRE(value == 0x123456789abcdefUL && pmap_is_referenced(a)); clear(a);
		*(volatile uint64_t *)(uintptr_t)pa = 17;
		REQUIRE(pmap_is_modified(a) && pmap_is_referenced(a)); clear(a);
		/* Detach from the middle and replace an existing VA with another page. */
		*v0 = 99;
		REQUIRE(pmap_enter(pmap_kernel(), base, pb, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ) == 0);
		REQUIRE(pmap_is_modified(a) && pmap_is_referenced(b)); clear(a); clear(b);
		*v0 = 100; REQUIRE(pmap_is_modified(b) && !pmap_is_modified(a));
		pmap_unwire(pmap_kernel(), base + PAGE_SIZE);
		REQUIRE(pmap_wired_count(pmap_kernel()) == wired);
		pmap_kenter_pa(base + 3 * PAGE_SIZE, pa, VM_PROT_READ | VM_PROT_WRITE, PMAP_NOCACHE);
		*v3 = 123; pmap_kremove(base + 3 * PAGE_SIZE, PAGE_SIZE);
		REQUIRE(pmap_is_modified(a) && pmap_wired_count(pmap_kernel()) == wired);
		pmap_page_protect(b, VM_PROT_READ);
		REQUIRE(rvm_boot_pmap_lookup(base, &out, &flags) && out == pb && (flags & RVM_PTE_WRITE) == 0);
		pmap_page_protect(b, VM_PROT_ALL);
		REQUIRE(rvm_boot_pmap_lookup(base + 2 * PAGE_SIZE, &out, &flags) && (flags & RVM_PTE_WRITE) == 0);
		pmap_page_protect(b, VM_PROT_NONE); REQUIRE(b->mdpage.pv_head == NULL);
		REQUIRE(!pmap_extract(pmap_kernel(), base, &out) && !pmap_extract(pmap_kernel(), base + 2 * PAGE_SIZE, &out));
		pmap_page_protect(a, VM_PROT_NONE); REQUIRE(a->mdpage.pv_head == NULL);
		/* Exercise list removal at head, middle and tail across many aliases. */
		for (i = 0; i < 64; i++) REQUIRE(pmap_enter(pmap_kernel(), base + i * PAGE_SIZE, pa, VM_PROT_READ | VM_PROT_WRITE, i & 1 ? PMAP_WIRED : 0) == 0);
		pmap_kremove(base + 20 * PAGE_SIZE, 20 * PAGE_SIZE);
		REQUIRE(pmap_resident_count(pmap_kernel()) == resident + 44);
		pmap_page_protect(a, VM_PROT_NONE); REQUIRE(a->mdpage.pv_head == NULL);
		vaddr_t last = VM_KERNEL_VM_BASE + VM_KERNEL_VM_SIZE - PAGE_SIZE;
		REQUIRE(pmap_enter(pmap_kernel(), last, pa, VM_PROT_READ | VM_PROT_WRITE, PMAP_WIRED) == 0);
		REQUIRE(*(volatile uint64_t *)last == 123);
		pmap_page_protect(a, VM_PROT_NONE); REQUIRE(!pmap_extract(pmap_kernel(), last, &out));
		REQUIRE(pmap_resident_count(pmap_kernel()) == resident && pmap_wired_count(pmap_kernel()) == wired);
		uvm_km_free(kernel_map, base, 64 * PAGE_SIZE, UVM_KMF_VAONLY);
		uvm_pagefree(a); uvm_pagefree(b);
		REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && kernel_map->nentries == 0);
	}
	/* The same public PTE/PV operations guard themselves with real IRQs on. */
	a = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO | UVM_PGA_USERESERVE); REQUIRE(a);
	pa = VM_PAGE_TO_PHYS(a);
	base = uvm_km_alloc(kernel_map, 2 * PAGE_SIZE, 0, UVM_KMF_VAONLY | UVM_KMF_NOWAIT); REQUIRE(base);
	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(NULL, NULL));
	REQUIRE(rvm_async_clock_start());
	uint64_t events = rvm_async_clock_events(), stop = rvm_clock_monotonic_ns() + 250000000;
	i = 0;
	do {
		REQUIRE(pmap_enter(pmap_kernel(), base, pa, VM_PROT_READ | VM_PROT_WRITE, PMAP_WIRED) == 0);
		pmap_update(pmap_kernel()); *(volatile uint64_t *)base = i;
		REQUIRE(pmap_is_modified(a)); pmap_clear_reference(a); pmap_clear_modify(a);
		pmap_unwire(pmap_kernel(), base); pmap_remove(pmap_kernel(), base, base + PAGE_SIZE);
		REQUIRE(pmap_enter(pmap_kernel(), base + PAGE_SIZE, pa, VM_PROT_READ | VM_PROT_WRITE, 0) == 0);
		pmap_page_protect(a, VM_PROT_NONE); i++;
	} while (rvm_clock_monotonic_ns() < stop);
	REQUIRE(i > 5 && rvm_async_clock_events() > events);
	REQUIRE(rvm_async_clock_stop() && rvm_bootsched_fini());
	uvm_km_free(kernel_map, base, 2 * PAGE_SIZE, UVM_KMF_VAONLY); uvm_pagefree(a);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && kernel_map->nentries == 0);
	rvm_early_puts(pv_ok); rvm_boot_stop();
}
