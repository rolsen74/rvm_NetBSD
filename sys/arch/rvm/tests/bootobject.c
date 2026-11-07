/* Actual MI page ownership, radix tags, wiring and metadata exhaustion. */
#include <uvm/uvm.h>
#include <machine/bootmap.h>
#include <machine/bootobject.h>

const char object_ok[] = "Resident UVM objects and radix exhaustion verified.\n";
const char object_bad[] = "Resident object regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(object_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	struct uvm_object *obj, *others[RVM_BOOTALLOC_SLOTS];
	struct vm_page *pages[3], *page;
	const voff_t offsets[3] = { 0, 17 * PAGE_SIZE, 1UL << 36 };
	const voff_t far = 1UL << 60;
	volatile uint64_t *words;
	int64_t before, remaining;
	int wired;
	unsigned int n, i, j;
	vaddr_t va;
	struct vm_map_entry *entry;
	paddr_t pa;

	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32 && rvm_bootradix_used() == 0);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	wired = uvmexp.wired;
	REQUIRE(!rvm_bootobj_create(0) && !rvm_bootobj_create(1));
	REQUIRE(!rvm_bootobj_create((vsize_t)-PAGE_SIZE));
	REQUIRE(!rvm_bootobj_destroy(NULL));
	obj = rvm_bootobj_create((1UL << 36) + PAGE_SIZE);
	REQUIRE(obj != NULL && obj->uo_npages == 0 && obj->uo_refs == 1);
	REQUIRE(!rvm_bootobj_alloc(obj, -PAGE_SIZE));
	REQUIRE(!rvm_bootobj_alloc(obj, 1));
	REQUIRE(!rvm_bootobj_alloc(obj, (1UL << 36) + PAGE_SIZE));
	for (i = 0; i < 3; i++) {
		pages[i] = rvm_bootobj_alloc(obj, offsets[i]);
		REQUIRE(pages[i] != NULL);
		REQUIRE(pages[i]->uobject == obj && pages[i]->offset == offsets[i]);
		REQUIRE(pages[i]->wire_count == 1 && (pages[i]->flags & PG_TABLED));
		REQUIRE(!(pages[i]->flags & (PG_BUSY | PG_FAKE | PG_STAT)));
		REQUIRE(!rvm_bootobj_alloc(obj, offsets[i]));
		words = (volatile uint64_t *)(uintptr_t)VM_PAGE_TO_PHYS(pages[i]);
		for (j = 0; j < PAGE_SIZE / sizeof(*words); j++)
			REQUIRE(words[j] == 0);
	}
	REQUIRE(obj->uo_npages == 3 && uvmexp.wired == wired + 3);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before - 3);
	REQUIRE(!rvm_bootobj_destroy(obj));
	rw_enter(obj->vmobjlock, RW_WRITER);
	REQUIRE(!uvm_obj_clean_p(obj) && uvm_obj_nowriteback_p(obj));
	for (i = 0; i < 3; i++) {
		REQUIRE(uvm_pagelookup(obj, offsets[i]) == pages[i]);
		REQUIRE(uvm_obj_page_dirty_p(pages[i]));
		REQUIRE(uvm_pagegetdirty(pages[i]) == UVM_PAGE_STATUS_DIRTY);
		uvm_pagemarkdirty(pages[i], UVM_PAGE_STATUS_CLEAN);
		REQUIRE(!uvm_obj_page_dirty_p(pages[i]));
	}
	REQUIRE(uvm_obj_clean_p(obj));
	uvm_pagemarkdirty(pages[1], UVM_PAGE_STATUS_DIRTY);
	REQUIRE(!uvm_obj_clean_p(obj) && uvm_obj_page_dirty_p(pages[1]));
	REQUIRE(uvm_pagelookup(obj, PAGE_SIZE) == NULL);
	rw_exit(obj->vmobjlock);
	for (i = 0; i < 3; i++) {
		REQUIRE(rvm_bootobj_free(obj, offsets[i]));
		REQUIRE(!rvm_bootobj_free(obj, offsets[i]));
	}
	REQUIRE(obj->uo_npages == 0 && rvm_bootradix_used() == 0);
	REQUIRE(rvm_bootobj_destroy(obj));
	REQUIRE(!rvm_bootobj_destroy(obj));
	REQUIRE(uvmexp.wired == wired);

	obj = rvm_bootobj_create(2 * PAGE_SIZE);
	va = RVM_BOOTALLOC_BASE;
	REQUIRE(obj && !rvm_bootmap_reserve_object(&va, 2 * PAGE_SIZE, obj));
	REQUIRE(obj->uo_refs == 2 && !rvm_bootobj_destroy(obj));
	vm_map_lock_read(kernel_map);
	REQUIRE(uvm_map_lookup_entry(kernel_map, va, &entry));
	REQUIRE(entry->object.uvm_obj == obj && entry->etype == UVM_ET_OBJ);
	REQUIRE(entry->offset == 0 && entry->protection ==
	    (VM_PROT_READ | VM_PROT_WRITE));
	vm_map_unlock_read(kernel_map);
	REQUIRE(!pmap_extract(pmap_kernel(), va, &pa));
	REQUIRE(!rvm_bootmap_release(va, 2 * PAGE_SIZE));
	REQUIRE(obj->uo_refs == 1 && rvm_bootobj_destroy(obj));

	/* Sparse objects consume radix nodes while leaving plenty of free RAM.
	 * Failed insertions must preserve existing pages and physical counts.
	 */
	n = 0;
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		obj = rvm_bootobj_create(far + PAGE_SIZE);
		REQUIRE(obj != NULL);
		others[n++] = obj;
		REQUIRE(rvm_bootobj_alloc(obj, 0) != NULL);
		remaining = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
		page = rvm_bootobj_alloc(obj, far);
		if (page == NULL) {
			REQUIRE(obj->uo_npages == 1);
			REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == remaining);
			break;
		}
		REQUIRE(obj->uo_npages == 2);
	}
	REQUIRE(i < RVM_BOOTALLOC_SLOTS && rvm_bootradix_used() > 0);
	for (i = 0; i < n; i++) {
		obj = others[i];
		if (obj->uo_npages == 2)
			REQUIRE(rvm_bootobj_free(obj, far));
		REQUIRE(rvm_bootobj_free(obj, 0));
		REQUIRE(rvm_bootobj_destroy(obj));
	}
	REQUIRE(rvm_bootradix_used() == 0 && uvmexp.wired == wired);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		others[i] = rvm_bootobj_create(PAGE_SIZE);
		REQUIRE(others[i] != NULL);
	}
	REQUIRE(rvm_bootobj_create(PAGE_SIZE) == NULL);
	REQUIRE(rvm_bootalloc(1) == 0); /* Object-slot exhaustion unwinds cleanly. */
	REQUIRE(kernel_map->nentries == 0);
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++)
		REQUIRE(rvm_bootobj_destroy(others[i]));
	va = rvm_bootalloc(PAGE_SIZE + 1);
	REQUIRE(va != 0 && uvmexp.wired == wired + 2);
	REQUIRE(rvm_bootfree(va));
	REQUIRE(rvm_bootradix_used() == 0 && uvmexp.wired == wired);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	rvm_early_puts(object_ok);
}
