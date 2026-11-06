/* Force the second physical page to fail, then check complete rollback. */
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <uvm/uvm.h>
#include <machine/bootobject.h>
const char low_bad[] = "Guarded u-area rollback regression at line 0x";
const char low_ok[] = "RVM guarded u-area partial allocation rollback verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(low_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct vm_page *held[512] __attribute__((section(".data"))) = { 0 };
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int count = 0, n, radix;
	int64_t before;
	long resident, wired;
	struct vm_page *one;
	rvm_normal_early_init(bi);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel()); wired = pmap_wired_count(pmap_kernel());
	radix = rvm_bootradix_used();
	while (curcpu()->ci_counts[CPU_COUNT_FREEPAGES] > 2) {
		REQUIRE(count < __arraycount(held));
		held[count] = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_USERESERVE);
		REQUIRE(held[count] != NULL); count++;
	}
	/* One page is available; the other is reserved for the page daemon. */
	one = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_USERESERVE);
	REQUIRE(one != NULL && curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == 1);
	uvm_pagefree(one);
	for (n = 0; n < 20; n++) {
		REQUIRE(cpu_uarea_alloc(true) == NULL);
		REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == 2 && kernel_map->nentries == 0);
		REQUIRE(rvm_bootradix_used() == radix);
		REQUIRE(pmap_resident_count(pmap_kernel()) == resident && pmap_wired_count(pmap_kernel()) == wired);
	}
	while (count) uvm_pagefree(held[--count]);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	rvm_early_puts(low_ok);
}
