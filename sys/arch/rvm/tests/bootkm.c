/* Exercise actual MI km allocation, batched removal and NOWAIT rollback. */
#include <uvm/uvm.h>
#include <machine/bootobject.h>

const char km_ok[] = "MI wired kernel allocation and rollback verified.\n";
const char km_bad[] = "Kernel allocation regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(km_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	const uvm_flag_t flags = UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO;
	const uvm_flag_t vaonly = UVM_KMF_VAONLY | UVM_KMF_NOWAIT;
	vaddr_t a, b, buffer, slots[RVM_BOOTALLOC_SLOTS];
	paddr_t pa;
	volatile uint64_t *data;
	struct vm_page *page;
	struct vm_map_entry *entry;
	int64_t before;
	long resident, wired;
	int page_wired;
	unsigned int i;

	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 15);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel());
	wired = pmap_wired_count(pmap_kernel());
	page_wired = uvmexp.wired;
	REQUIRE(!uvm_km_alloc(NULL, PAGE_SIZE, 0, flags));
	REQUIRE(!uvm_km_alloc(kernel_map, 0, 0, flags));
	REQUIRE(!uvm_km_alloc(kernel_map, (vsize_t)-1, 0, flags));
	REQUIRE(!uvm_km_alloc(kernel_map, PAGE_SIZE, 3 * PAGE_SIZE, flags));
	REQUIRE(!uvm_km_alloc(kernel_map, PAGE_SIZE, 0, flags | UVM_KMF_EXEC));
	REQUIRE(!uvm_km_alloc(kernel_map, PAGE_SIZE, 0, UVM_KMF_WIRED));
	REQUIRE(!uvm_km_alloc(kernel_map, PAGE_SIZE, 0,
	    UVM_KMF_PAGEABLE | UVM_KMF_NOWAIT));
	REQUIRE(!uvm_km_alloc(kernel_map, PAGE_SIZE, 0, flags | UVM_KMF_WAITVA));
	REQUIRE(!uvm_km_alloc(kernel_map, PAGE_SIZE, 0, vaonly | UVM_KMF_ZERO));
	vm_map_lock(kernel_map);
	REQUIRE(!uvm_km_alloc(kernel_map, PAGE_SIZE, 0, flags | UVM_KMF_TRYLOCK));
	vm_map_unlock(kernel_map);
	REQUIRE(kernel_map->nentries == 0);

	/* Share the map with an object-owned buffer: MI km uses raw pages at
	 * a nonzero color offset and must leave the other owner intact.
	 */
	buffer = rvm_bootalloc(PAGE_SIZE);
	REQUIRE(buffer != 0);
	*(volatile uint64_t *)buffer = 0x52564d4d49584544UL;
	a = uvm_km_alloc(kernel_map, PAGE_SIZE + 1, 4 * PAGE_SIZE, flags);
	REQUIRE(a != 0 && a != buffer && (a & (4 * PAGE_SIZE - 1)) == 0);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before - 3);
	data = (volatile uint64_t *)a;
	for (i = 0; i < 2 * PAGE_SIZE / sizeof(*data); i++) {
		REQUIRE(data[i] == 0);
		data[i] = 0x55564d4b4d000000UL ^ i;
	}
	REQUIRE(pmap_extract(pmap_kernel(), a, &pa));
	page = PHYS_TO_VM_PAGE(pa);
	REQUIRE(page != NULL && page->uobject == NULL && page->uanon == NULL);
	REQUIRE(page->wire_count == 0 && !(page->flags & (PG_BUSY | PG_STAT)));
	REQUIRE(uvmexp.wired == page_wired + 1); /* Only the resident object. */
	vm_map_lock_read(kernel_map);
	REQUIRE(uvm_map_lookup_entry(kernel_map, a, &entry));
	REQUIRE(entry->start == a && entry->end == a + 2 * PAGE_SIZE);
	REQUIRE(entry->etype == 0 && entry->object.uvm_obj == NULL);
	REQUIRE(entry->flags & UVM_MAP_NOMERGE);
	vm_map_unlock_read(kernel_map);
	uvm_km_free(kernel_map, a, PAGE_SIZE + 1, UVM_KMF_WIRED);
	REQUIRE(!pmap_extract(pmap_kernel(), a, &pa));
	REQUIRE(!pmap_extract(pmap_kernel(), a + PAGE_SIZE, &pa));
	REQUIRE(*(volatile uint64_t *)buffer == 0x52564d4d49584544UL);
	REQUIRE(rvm_bootfree(buffer));
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);

	/* Zero-on-reuse and >16-page removal exercise MI's free batches. */
	a = uvm_km_alloc(kernel_map, 17 * PAGE_SIZE, 0, flags);
	REQUIRE(a != 0);
	data = (volatile uint64_t *)a;
	for (i = 0; i < 17 * PAGE_SIZE / sizeof(*data); i++) {
		REQUIRE(data[i] == 0);
		data[i] = ~(uint64_t)i;
	}
	uvm_km_free(kernel_map, a, 17 * PAGE_SIZE, UVM_KMF_WIRED);
	a = uvm_km_alloc(kernel_map, 17 * PAGE_SIZE, 0, flags);
	REQUIRE(a != 0);
	data = (volatile uint64_t *)a;
	for (i = 0; i < 17 * PAGE_SIZE / sizeof(*data); i++)
		REQUIRE(data[i] == 0);
	uvm_km_free(kernel_map, a, 17 * PAGE_SIZE, UVM_KMF_WIRED);

	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		slots[i] = uvm_km_alloc(kernel_map, PAGE_SIZE, 0, vaonly);
		REQUIRE(slots[i] != 0);
		REQUIRE(!pmap_extract(pmap_kernel(), slots[i], &pa));
	}
	REQUIRE(!uvm_km_alloc(kernel_map, PAGE_SIZE, 0, vaonly));
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++)
		uvm_km_free(kernel_map, slots[i], PAGE_SIZE, UVM_KMF_VAONLY);

	a = uvm_km_alloc(kernel_map, RVM_BOOTALLOC_SIZE, PAGE_SIZE, flags);
	if (before < (int64_t)(RVM_BOOTALLOC_SIZE / PAGE_SIZE)) {
		REQUIRE(a == 0); /* Partial physical allocation must roll back. */
		for (i = 0; i < RVM_BOOTALLOC_SIZE / PAGE_SIZE; i++)
			REQUIRE(!pmap_extract(pmap_kernel(),
			    RVM_BOOTALLOC_BASE + i * PAGE_SIZE, &pa));
	} else {
		REQUIRE(a != 0);
		REQUIRE(!uvm_km_alloc(kernel_map, PAGE_SIZE, 0, flags));
		uvm_km_free(kernel_map, a, RVM_BOOTALLOC_SIZE, UVM_KMF_WIRED);
	}
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(kernel_map->nentries == 0 && kernel_map->size == 0);
	a = uvm_km_alloc(kernel_map, PAGE_SIZE, 0, flags & ~UVM_KMF_ZERO);
	b = uvm_km_alloc(kernel_map, PAGE_SIZE, 0, flags);
	REQUIRE(a && b && a != b && kernel_map->nentries == 2);
	uvm_km_free(kernel_map, a, PAGE_SIZE, UVM_KMF_WIRED);
	uvm_km_free(kernel_map, b, PAGE_SIZE, UVM_KMF_WIRED);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident);
	REQUIRE(pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(uvmexp.wired == page_wired && rvm_bootradix_used() == 0);
	REQUIRE(curcpu()->ci_cpl == IPL_NONE && kernel_map->nentries == 0);
	rvm_early_puts(km_ok);
}
