/* Boot-level proof of managed kernel aliases and page-wide revocation. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <uvm/uvm.h>
#include <machine/mmu.h>
#include <machine/pmap.h>
int rvm_pmap_managed_verify(void)
{
	struct vm_page *pg;
	vaddr_t va;
	paddr_t pa, out;
	unsigned long flags;
	int64_t pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	long resident = pmap_resident_count(pmap_kernel());
	long wired = pmap_wired_count(pmap_kernel());
	pg = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO | UVM_PGA_USERESERVE);
	if (pg == NULL) return 0;
	pa = VM_PAGE_TO_PHYS(pg);
	va = uvm_km_alloc(kernel_map, 2 * PAGE_SIZE, 0, UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
	if (va == 0) { uvm_pagefree(pg); return 0; }
	pmap_clear_modify(pg); pmap_clear_reference(pg);
	if (pmap_is_modified(pg) || pmap_is_referenced(pg) ||
	    pmap_enter(pmap_kernel(), va, pa, VM_PROT_READ | VM_PROT_WRITE, PMAP_WIRED) ||
	    pmap_enter(pmap_kernel(), va + PAGE_SIZE, pa, VM_PROT_READ | VM_PROT_WRITE, 0)) return 0;
	*(volatile uint64_t *)va = 0x52564d4d414e4147UL;
	if (*(volatile uint64_t *)(va + PAGE_SIZE) != 0x52564d4d414e4147UL ||
	    !pmap_is_modified(pg) || !pmap_is_referenced(pg) ||
	    pmap_wired_count(pmap_kernel()) != wired + 1) return 0;
	pmap_unwire(pmap_kernel(), va); pmap_page_protect(pg, VM_PROT_READ);
	if (!rvm_boot_pmap_lookup(va + PAGE_SIZE, &out, &flags) || out != pa ||
	    (flags & RVM_PTE_WRITE) != 0) return 0;
	pmap_page_protect(pg, VM_PROT_NONE);
	if (pg->mdpage.pv_head != NULL || pmap_extract(pmap_kernel(), va, &out) ||
	    pmap_extract(pmap_kernel(), va + PAGE_SIZE, &out) ||
	    !pmap_clear_reference(pg) || pmap_is_referenced(pg) ||
	    !pmap_clear_modify(pg) || pmap_is_modified(pg)) return 0;
	uvm_km_free(kernel_map, va, 2 * PAGE_SIZE, UVM_KMF_VAONLY); uvm_pagefree(pg);
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages &&
	    pmap_resident_count(pmap_kernel()) == resident &&
	    pmap_wired_count(pmap_kernel()) == wired;
}
