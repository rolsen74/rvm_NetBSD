/* Kernel VM entries own whole amap references; pages are populated NOWAIT. */
#include <sys/param.h>
#include <sys/systm.h>
#include <uvm/uvm.h>
#include <machine/bootalloc.h>
#include <machine/bootamap.h>
#include <machine/bootanon.h>
#include <machine/bootanonmap.h>
#include <machine/bootheap.h>
#include <machine/bootmap.h>
#include <machine/psl.h>
#include <machine/uvm_boot.h>

static struct {
	vaddr_t address;
	vsize_t requested;
	vsize_t size;
	struct vm_amap *amap;
} mappings[RVM_BOOTANONMAP_MAPPINGS] __attribute__((section(".data"))) = { 0 };
const char rvm_anonmap_bad[] = "Anonymous mapping ownership corrupted";

static int
available(void)
{
	return rvm_uvm_boot.initialized && kernel_map != NULL &&
	    rvm_boot_get_cr() == RVM_CR_MMU && curcpu()->ci_cpl == IPL_NONE &&
	    rvm_bootheap_ready();
}

static int
find(vaddr_t address)
{
	unsigned int i;
	if (!available() || address == 0)
		return -1;
	for (i = 0; i < RVM_BOOTANONMAP_MAPPINGS; i++) {
		if (mappings[i].address == address)
			return i;
	}
	return -1;
}

static int
free_slot(void)
{
	unsigned int i;
	for (i = 0; i < RVM_BOOTANONMAP_MAPPINGS; i++) {
		if (mappings[i].address == 0)
			return i;
	}
	return -1;
}

static int
unmapped(vaddr_t address, vsize_t size)
{
	paddr_t pa;
	vsize_t offset;
	for (offset = 0; offset < size; offset += PAGE_SIZE) {
		if (pmap_extract(pmap_kernel(), address + offset, &pa))
			return 0;
	}
	return 1;
}

static void
undo_reservation(vaddr_t address, vsize_t size)
{
	if (rvm_bootmap_release(address, size) != 0)
		panic(rvm_anonmap_bad);
}

vaddr_t
rvm_bootanonmap_alloc(vsize_t requested)
{
	int slot;
	vaddr_t address = RVM_BOOTALLOC_BASE;
	vsize_t size, offset;
	struct vm_amap *amap;
	struct vm_anon *anon;
	struct vm_page *pg;
	if (!available() || requested == 0 ||
	    requested > RVM_BOOTANON_ANONS * PAGE_SIZE || (slot = free_slot()) < 0)
		return 0;
	size = round_page(requested);
	if (rvm_bootmap_reserve(&address, size, PAGE_SIZE, 0) != 0)
		return 0;
	if (!unmapped(address, size)) {
		undo_reservation(address, size);
		return 0;
	}
	amap = rvm_bootamap_create(size);
	if (amap == NULL) {
		undo_reservation(address, size);
		return 0;
	}
	for (offset = 0; offset < size; offset += PAGE_SIZE) {
		anon = uvm_analloc();
		if (anon == NULL)
			goto fail;
		if (!rvm_bootamap_put(amap, offset, anon)) {
			if (!rvm_bootanon_drop(anon))
				panic(rvm_anonmap_bad);
			goto fail;
		}
		pg = rvm_bootamap_page(amap, offset);
		if (pg == NULL)
			goto fail;
		pmap_kenter_pa(address + offset, VM_PAGE_TO_PHYS(pg),
		    VM_PROT_READ | VM_PROT_WRITE, 0);
	}
	pmap_update(pmap_kernel());
	if (rvm_bootmap_attach_amap(address, size, amap) != 0)
		goto fail;
	mappings[slot].address = address;
	mappings[slot].requested = requested;
	mappings[slot].size = size;
	mappings[slot].amap = amap;
	return address;
fail:
	if (!rvm_bootamap_drop(amap))
		panic(rvm_anonmap_bad);
	undo_reservation(address, size);
	return 0;
}

vaddr_t
rvm_bootanonmap_share(vaddr_t source)
{
	int old = find(source), slot;
	vaddr_t address = RVM_BOOTALLOC_BASE;
	vsize_t size, offset;
	struct vm_amap *amap;
	struct vm_page *pg;
	if (old < 0 || (slot = free_slot()) < 0)
		return 0;
	size = mappings[old].size;
	amap = mappings[old].amap;
	if (rvm_bootmap_reserve(&address, size, PAGE_SIZE, 0) != 0)
		return 0;
	if (!unmapped(address, size) || !rvm_bootamap_hold(amap)) {
		undo_reservation(address, size);
		return 0;
	}
	for (offset = 0; offset < size; offset += PAGE_SIZE) {
		pg = rvm_bootamap_page(amap, offset);
		if (pg == NULL) {
			pmap_kremove(address, offset);
			pmap_update(pmap_kernel());
			if (!rvm_bootamap_drop(amap))
				panic(rvm_anonmap_bad);
			undo_reservation(address, size);
			return 0;
		}
		pmap_kenter_pa(address + offset, VM_PAGE_TO_PHYS(pg),
		    VM_PROT_READ | VM_PROT_WRITE, 0);
	}
	pmap_update(pmap_kernel());
	if (rvm_bootmap_attach_amap(address, size, amap) != 0) {
		pmap_kremove(address, size);
		pmap_update(pmap_kernel());
		if (!rvm_bootamap_drop(amap))
			panic(rvm_anonmap_bad);
		undo_reservation(address, size);
		return 0;
	}
	mappings[slot].address = address;
	mappings[slot].requested = mappings[old].requested;
	mappings[slot].size = size;
	mappings[slot].amap = amap;
	return address;
}

int
rvm_bootanonmap_free(vaddr_t address, vsize_t requested)
{
	int i = find(address);
	if (i < 0 || requested != mappings[i].requested)
		return 0;
	if (rvm_bootmap_release_amap(address, mappings[i].size, mappings[i].amap) != 0)
		panic(rvm_anonmap_bad);
	mappings[i].address = 0;
	mappings[i].requested = 0;
	mappings[i].size = 0;
	mappings[i].amap = NULL;
	return 1;
}

unsigned int
rvm_bootanonmap_live(void)
{
	unsigned int i, live = 0;
	for (i = 0; i < RVM_BOOTANONMAP_MAPPINGS; i++)
		live += mappings[i].address != 0;
	return live;
}

int
rvm_bootanonmap_verify(void)
{
	vaddr_t first, second;
	paddr_t a, b;
	volatile uint64_t *data;
	struct vm_map_entry *entry;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	int64_t dirty = curcpu()->ci_counts[CPU_COUNT_ANONDIRTY];
	long resident = pmap_resident_count(pmap_kernel());
	long wired = pmap_wired_count(pmap_kernel());
	first = rvm_bootanonmap_alloc(PAGE_SIZE + 1);
	if (first == 0)
		return 0;
	vm_map_lock_read(kernel_map);
	int found = uvm_map_lookup_entry(kernel_map, first, &entry) &&
	    entry->aref.ar_amap != NULL && entry->aref.ar_pageoff == 0;
	vm_map_unlock_read(kernel_map);
	if (!found)
		return 0;
	data = (volatile uint64_t *)first;
	if (data[0] != 0 || data[2 * PAGE_SIZE / sizeof(*data) - 1] != 0)
		return 0;
	data[0] = 0x52564d414e4d4150UL;
	second = rvm_bootanonmap_share(first);
	if (second == 0 || !pmap_extract(pmap_kernel(), first, &a) ||
	    !pmap_extract(pmap_kernel(), second, &b) || a != b ||
	    *(volatile uint64_t *)second != data[0] ||
	    !rvm_bootanonmap_free(first, PAGE_SIZE + 1) ||
	    pmap_extract(pmap_kernel(), first, &a) ||
	    *(volatile uint64_t *)second != 0x52564d414e4d4150UL ||
	    !rvm_bootanonmap_free(second, PAGE_SIZE + 1) ||
	    !rvm_bootamap_fini() || !rvm_bootanon_fini())
		return 0;
	return rvm_bootanonmap_live() == 0 && rvm_bootheap_live() == 0 &&
	    curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before &&
	    curcpu()->ci_counts[CPU_COUNT_ANONDIRTY] == dirty &&
	    pmap_resident_count(pmap_kernel()) == resident &&
	    pmap_wired_count(pmap_kernel()) == wired;
}
