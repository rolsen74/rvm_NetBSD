/* Exercise VA ownership and transactional page allocation in the real VM. */
#include <uvm/uvm.h>
#include <machine/bootalloc.h>
#include <machine/mmu.h>
#include <machine/bootobject.h>

const char wired_ok[] = "Wired buffer ownership and rollback verified.\n";
const char wired_bad[] = "Wired buffer regression at line 0x";
#define CHECK(c) do { if (!(c)) { rvm_early_puts(wired_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	vaddr_t a, b, c, slots[RVM_BOOTALLOC_SLOTS];
	paddr_t pa, other;
	unsigned long flags;
	int64_t before;
	long resident, wired;
	int page_wired;
	unsigned int radix;
	unsigned int i;
	volatile uint64_t *words;

	rvm_normal_early_init(bi);
	CHECK(rvm_boot_stage == 32 && !rvm_bootalloc_init());
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel());
	wired = pmap_wired_count(pmap_kernel());
	page_wired = uvmexp.wired;
	radix = rvm_bootradix_used();
	CHECK(!rvm_bootalloc(0) && !rvm_bootalloc((vsize_t)-1));
	CHECK(!rvm_bootalloc(RVM_BOOTALLOC_SIZE + 1));
	CHECK(!rvm_bootfree(0) && !rvm_bootfree(RVM_BOOTALLOC_BASE));
	a = rvm_bootalloc(PAGE_SIZE + 1);
	b = rvm_bootalloc(PAGE_SIZE);
	CHECK(a == RVM_BOOTALLOC_BASE && b == a + 2 * PAGE_SIZE);
	CHECK(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before - 3);
	for (i = 0; i < 2 * PAGE_SIZE / sizeof(*words); i++) {
		words = (volatile uint64_t *)a;
		CHECK(words[i] == 0);
		words[i] = 0x52564d0000000000UL + i;
	}
	CHECK(pmap_extract(pmap_kernel(), a, &pa));
	CHECK(pmap_extract(pmap_kernel(), a + PAGE_SIZE, &other) && pa != other);
	CHECK(rvm_boot_pmap_lookup(a, &pa, &flags));
	CHECK((flags & (RVM_PTE_READ | RVM_PTE_WRITE | RVM_PTE_EXEC)) ==
	    (RVM_PTE_READ | RVM_PTE_WRITE));
	CHECK(!rvm_bootfree(a + 1) && !rvm_bootfree(a + PAGE_SIZE));
	CHECK(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before - 3);
	CHECK(rvm_bootfree(a) && !rvm_bootfree(a));
	CHECK(!pmap_extract(pmap_kernel(), a, &pa));
	CHECK(!pmap_extract(pmap_kernel(), a + PAGE_SIZE, &pa));
	c = rvm_bootalloc(2 * PAGE_SIZE);
	CHECK(c == a); /* First-fit reuses the hole without disturbing b. */
	words = (volatile uint64_t *)c;
	for (i = 0; i < 2 * PAGE_SIZE / sizeof(*words); i++)
		CHECK(words[i] == 0);
	CHECK(rvm_bootfree(b) && rvm_bootfree(c));
	CHECK(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);

	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		slots[i] = rvm_bootalloc(1);
		CHECK(slots[i] == RVM_BOOTALLOC_BASE + i * PAGE_SIZE);
	}
	CHECK(!rvm_bootalloc(1));
	/* Fragment the fixed descriptor set, then refill every hole. */
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i += 2)
		CHECK(rvm_bootfree(slots[i]));
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i += 2) {
		a = rvm_bootalloc(1);
		CHECK(a == slots[i]);
	}
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++)
		CHECK(rvm_bootfree(slots[i]));
	CHECK(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);

	a = rvm_bootalloc(RVM_BOOTALLOC_SIZE);
	if (before < (int64_t)(RVM_BOOTALLOC_SIZE / PAGE_SIZE)) {
		/* Physical exhaustion after partial mapping: all pages, aliases,
		 * descriptors and the VA reservation must have been returned.
		 */
		CHECK(a == 0);
		CHECK(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
		for (i = 0; i < RVM_BOOTALLOC_SIZE / PAGE_SIZE; i++)
			CHECK(!pmap_extract(pmap_kernel(),
			    RVM_BOOTALLOC_BASE + i * PAGE_SIZE, &pa));
	} else {
		CHECK(a == RVM_BOOTALLOC_BASE);
		CHECK(!rvm_bootalloc(1)); /* Virtual arena exhausted. */
		CHECK(rvm_bootfree(a));
	}
	a = rvm_bootalloc(PAGE_SIZE);
	CHECK(a == RVM_BOOTALLOC_BASE && rvm_bootfree(a));
	CHECK(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	CHECK(pmap_resident_count(pmap_kernel()) == resident);
	CHECK(pmap_wired_count(pmap_kernel()) == wired);
	CHECK(uvmexp.wired == page_wired && rvm_bootradix_used() == radix);
	CHECK(kernel_map->nentries == 0 && kernel_map->size == 0);
	CHECK(curcpu()->ci_cpl == IPL_NONE);
	rvm_early_puts(wired_ok);
}
