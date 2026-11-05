/* Included by bootstrap.py after machdep.c with its entry point renamed. */
#include <sys/null.h>
#include <uvm/uvm.h>

const char rvm_test_pmap_ok[] = "Kernel pmap operations verified.\n";
const char rvm_test_pmap_bad[] = "Kernel pmap regression failed at line 0x";

#define REQUIRE(condition) do { \
	if (!(condition)) { \
		rvm_early_puts(rvm_test_pmap_bad); \
		rvm_early_puthex(__LINE__); \
		rvm_boot_stop(); \
	} \
} while (0)

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	vaddr_t start, end, va;
	paddr_t pa, free_start, next, testpa;
	unsigned long flags, tables;
	volatile uint64_t *src, *dst, *alias;
	long resident, wired;
	unsigned int i;
	struct vm_page *testpage;

	rvm_normal_early_init(bi);
	pmap_virtual_space(&start, &end);
	REQUIRE(start == VM_KERNEL_VM_BASE && end - start == VM_KERNEL_VM_SIZE);
	REQUIRE(pmap_kernel()->pm_root == rvm_boot_pmap.root);
	REQUIRE(!pmap_extract(pmap_kernel(), 0, NULL));
	REQUIRE(!pmap_extract(pmap_kernel(), start, &pa));
	REQUIRE(!pmap_extract(pmap_kernel(), end - 1, &pa));
	REQUIRE(!pmap_extract(pmap_kernel(), end, &pa));
	resident = pmap_resident_count(pmap_kernel());
	wired = pmap_wired_count(pmap_kernel());
	REQUIRE(resident == wired && resident ==
	    (long)(rvm_bootmem.ram_end >> RVM_MMU_PAGE_SHIFT) + 1);
	free_start = rvm_bootmem.free_start;
	next = rvm_bootmem.next;
	tables = rvm_boot_pmap.tables;
	REQUIRE(rvm_bootmem.sealed && rvm_bootmem_seal());
	REQUIRE(rvm_bootmem_alloc(RVM_MMU_PAGE_SIZE, RVM_MMU_PAGE_SIZE) == 0);
	REQUIRE(!rvm_bootmem_init(&rvm_bootinfo));
	/* Allocate a managed page after the MI page allocator has initialized. */
	REQUIRE(free_start < rvm_bootmem.free_end);
	testpage = uvm_pagealloc_strat(NULL, 0, NULL, UVM_PGA_USERESERVE,
	    UVM_PGA_STRAT_NORMAL, VM_FREELIST_DEFAULT);
	REQUIRE(testpage != NULL);
	testpa = VM_PAGE_TO_PHYS(testpage);
	REQUIRE(testpa > rvm_uvm_boot.probe);
	src = (volatile uint64_t *)(uintptr_t)rvm_boot_pmap.test_page;
	dst = (volatile uint64_t *)(uintptr_t)testpa;
	for (i = 0; i < RVM_MMU_PAGE_SIZE / sizeof(*src); i++) {
		src[i] = 0x52564d434f505900UL ^ i;
		dst[i] = ~src[i];
	}
	pmap_zero_page(testpa);
	for (i = 0; i < RVM_MMU_PAGE_SIZE / sizeof(*src); i++)
		REQUIRE(dst[i] == 0);
	pmap_copy_page(rvm_boot_pmap.test_page, testpa);
	for (i = 0; i < RVM_MMU_PAGE_SIZE / sizeof(*src); i++)
		REQUIRE(dst[i] == src[i]);

	/* Cross a leaf-table boundary, then exercise the final preallocated leaf. */
	va = start + RVM_MMU_ENTRIES * RVM_MMU_PAGE_SIZE - RVM_MMU_PAGE_SIZE;
	pmap_kenter_pa(va, rvm_boot_pmap.test_page,
	    VM_PROT_READ | VM_PROT_WRITE, PMAP_NOCACHE);
	pmap_kenter_pa(va + RVM_MMU_PAGE_SIZE, testpa,
	    VM_PROT_READ | VM_PROT_WRITE, 0);
	pmap_kenter_pa(end - RVM_MMU_PAGE_SIZE, rvm_boot_pmap.test_page,
	    VM_PROT_READ, 0);
	pmap_update(pmap_kernel());
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident + 3);
	REQUIRE(pmap_wired_count(pmap_kernel()) == wired + 3);
	REQUIRE(pmap_extract(pmap_kernel(), va + 7, &pa) &&
	    pa == rvm_boot_pmap.test_page + 7);
	REQUIRE(pmap_extract(pmap_kernel(), va, NULL));
	REQUIRE(pmap_extract(pmap_kernel(), end - 1, &pa) &&
	    pa == rvm_boot_pmap.test_page + RVM_MMU_PAGE_MASK);
	alias = (volatile uint64_t *)va;
	alias[0] = 0x123456789abcdef0UL;
	REQUIRE(src[0] == alias[0] && dst[0] != alias[0]);

	/* Replacement must take effect even after the old VA was accessed. */
	pmap_kenter_pa(va, testpa, VM_PROT_READ | VM_PROT_WRITE, 0);
	pmap_update(pmap_kernel());
	REQUIRE(alias[0] == dst[0] && alias[0] != src[0]);
	alias[RVM_MMU_PAGE_SIZE / sizeof(*alias) - 1] = 0x8877665544332211UL;
	REQUIRE(dst[RVM_MMU_PAGE_SIZE / sizeof(*dst) - 1] ==
	    0x8877665544332211UL);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident + 3);
	REQUIRE(rvm_boot_pmap_lookup(va, &pa, &flags) && pa == testpa &&
	    (flags & (RVM_PTE_SUPERVISOR | RVM_PTE_USED_READ | RVM_PTE_USED_WRITE)) ==
	    (RVM_PTE_SUPERVISOR | RVM_PTE_USED_READ | RVM_PTE_USED_WRITE));

	pmap_protect(pmap_kernel(), va, va + 2 * RVM_MMU_PAGE_SIZE, VM_PROT_READ);
	/* A subsequent request for more permissions must not restore writes. */
	pmap_protect(pmap_kernel(), va, va + RVM_MMU_PAGE_SIZE, VM_PROT_ALL);
	pmap_update(pmap_kernel());
	REQUIRE(rvm_boot_pmap_lookup(va, &pa, &flags) &&
	    (flags & (RVM_PTE_READ | RVM_PTE_WRITE | RVM_PTE_EXEC)) == RVM_PTE_READ);
	REQUIRE(alias[0] == dst[0]);
	pmap_protect(pmap_kernel(), va, va + RVM_MMU_PAGE_SIZE, VM_PROT_NONE);
	pmap_remove(pmap_kernel(), va, va + 2 * RVM_MMU_PAGE_SIZE);
	pmap_kremove(end - RVM_MMU_PAGE_SIZE, RVM_MMU_PAGE_SIZE);
	pmap_update(pmap_kernel());
	REQUIRE(!pmap_extract(pmap_kernel(), va, &pa));
	REQUIRE(!pmap_extract(pmap_kernel(), va + RVM_MMU_PAGE_SIZE, &pa));
	REQUIRE(!pmap_extract(pmap_kernel(), end - 1, &pa));
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident);
	REQUIRE(pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(rvm_bootmem.free_start == free_start && rvm_bootmem.next == next &&
	    rvm_boot_pmap.tables == tables);
	uvm_pagefree(testpage);
	rvm_early_puts(rvm_test_pmap_ok);
}
