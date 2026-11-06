/* Real VM entries and MI detach, shared aliases and partial failure rollback. */
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/bootalloc.h>
#include <machine/bootamap.h>
#include <machine/bootanon.h>
#include <machine/bootanonmap.h>
#include <machine/bootheap.h>
#include <machine/bootmap.h>

const char anonmap_ok[] = "MI anonymous VM entries, aliases and rollback verified.\n";
const char anonmap_bad[] = "Anonymous mapping regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(anonmap_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)

static struct vm_amap *
owner(vaddr_t address, vsize_t size)
{
	struct vm_map_entry *entry;
	struct vm_amap *amap;
	vm_map_lock_read(kernel_map);
	REQUIRE(uvm_map_lookup_entry(kernel_map, address, &entry));
	REQUIRE(entry->start == address && entry->end == address + size);
	REQUIRE(entry->etype == 0 && entry->object.uvm_obj == NULL);
	REQUIRE(entry->aref.ar_amap != NULL && entry->aref.ar_pageoff == 0);
	REQUIRE(entry->protection == (VM_PROT_READ | VM_PROT_WRITE));
	REQUIRE(entry->wired_count == 0);
	amap = entry->aref.ar_amap;
	vm_map_unlock_read(kernel_map);
	return amap;
}

static void
restored(int64_t before, long resident, long wired, int64_t dirty)
{
	(void)rvm_bootamap_fini();
	(void)rvm_bootanon_fini();
	REQUIRE(rvm_bootanonmap_live() == 0 && rvm_bootheap_live() == 0);
	REQUIRE(kernel_map->nentries == 0 && curcpu()->ci_cpl == IPL_NONE);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_ANONDIRTY] == dirty);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident);
	REQUIRE(pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(curlwp->l_nopreempt == 0);
}

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	vaddr_t first, second, third, views[RVM_BOOTANONMAP_MAPPINGS];
	vaddr_t reservations[RVM_BOOTALLOC_SLOTS];
	struct vm_anon *anons[RVM_BOOTANON_ANONS];
	struct vm_page *held[1024], *pg;
	unsigned char *blocks[RVM_BOOTHEAP_RECORDS];
	struct vm_amap *amap;
	paddr_t a, b;
	unsigned int i, j, n;
	int64_t before, dirty, snapshot;
	long resident, wired;
	int entries;

	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 20);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	dirty = curcpu()->ci_counts[CPU_COUNT_ANONDIRTY];
	resident = pmap_resident_count(pmap_kernel());
	wired = pmap_wired_count(pmap_kernel());
	REQUIRE(!rvm_bootanonmap_alloc(0));
	REQUIRE(!rvm_bootanonmap_alloc(RVM_BOOTANON_ANONS * PAGE_SIZE + 1));
	REQUIRE(!rvm_bootanonmap_alloc((vsize_t)-1));
	REQUIRE(!rvm_bootanonmap_share(0) && !rvm_bootanonmap_share(1));
	REQUIRE(!rvm_bootanonmap_free(0, 1));

	first = rvm_bootanonmap_alloc(2 * PAGE_SIZE + 17);
	REQUIRE(first != 0);
	amap = owner(first, 3 * PAGE_SIZE);
	REQUIRE(amap->am_ref == 1 && !rvm_bootamap_drop(amap));
	REQUIRE(!rvm_bootamap_take(amap, 0));
	REQUIRE(!rvm_bootanon_drop(rvm_bootamap_lookup(amap, 0)));
	REQUIRE(rvm_bootmap_release(first, 3 * PAGE_SIZE) != 0);
	REQUIRE(!rvm_bootanonmap_free(first, 3 * PAGE_SIZE));
	REQUIRE(!rvm_bootanonmap_free(first + PAGE_SIZE, 2 * PAGE_SIZE + 17));
	for (i = 0; i < 3; i++) {
		volatile uint64_t *data = (volatile uint64_t *)(first + i * PAGE_SIZE);
		for (j = 0; j < PAGE_SIZE / sizeof(*data); j++)
			REQUIRE(data[j] == 0);
		data[0] = 0x52564d5649455700UL + i;
	}
	snapshot = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	second = rvm_bootanonmap_share(first);
	third = rvm_bootanonmap_share(first);
	REQUIRE(second && third && second != first && third != second);
	REQUIRE(owner(second, 3 * PAGE_SIZE) == amap && owner(third, 3 * PAGE_SIZE) == amap);
	REQUIRE(amap->am_ref == 3 && curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == snapshot);
	for (i = 0; i < 3; i++) {
		REQUIRE(pmap_extract(pmap_kernel(), first + i * PAGE_SIZE, &a));
		REQUIRE(pmap_extract(pmap_kernel(), second + i * PAGE_SIZE, &b) && a == b);
		REQUIRE(*(volatile uint64_t *)(third + i * PAGE_SIZE) == 0x52564d5649455700UL + i);
	}
	*(volatile uint64_t *)second = 0x123456789abcdef0UL;
	REQUIRE(*(volatile uint64_t *)third == 0x123456789abcdef0UL);
	REQUIRE(rvm_bootanonmap_free(first, 2 * PAGE_SIZE + 17));
	REQUIRE(!pmap_extract(pmap_kernel(), first, &a) && amap->am_ref == 2);
	REQUIRE(*(volatile uint64_t *)second == 0x123456789abcdef0UL);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_ANONDIRTY] == dirty + 3);
	REQUIRE(rvm_bootanonmap_free(third, 2 * PAGE_SIZE + 17));
	REQUIRE(amap->am_ref == 1 && !rvm_bootamap_take(amap, 0));
	/* A separately held caller reference survives the last VM entry. */
	REQUIRE(rvm_bootamap_hold(amap));
	REQUIRE(rvm_bootanonmap_free(second, 2 * PAGE_SIZE + 17));
	REQUIRE(amap->am_ref == 1 && rvm_bootamap_page(amap, 0) != NULL);
	REQUIRE(rvm_bootamap_drop(amap));
	restored(before, resident, wired, dirty);

	/* Mapping-record capacity can be reached using aliases of one page. */
	first = rvm_bootanonmap_alloc(1);
	REQUIRE(first);
	views[0] = first;
	for (i = 1; i < RVM_BOOTANONMAP_MAPPINGS; i++) {
		views[i] = rvm_bootanonmap_share(first);
		REQUIRE(views[i] != 0);
	}
	REQUIRE(!rvm_bootanonmap_share(first) && !rvm_bootanonmap_alloc(1));
	REQUIRE(rvm_bootanonmap_live() == RVM_BOOTANONMAP_MAPPINGS);
	*(volatile uint64_t *)first = 0xabcdef;
	REQUIRE(rvm_bootanonmap_free(first, 1));
	views[0] = rvm_bootanonmap_share(views[7]);
	REQUIRE(views[0] && *(volatile uint64_t *)views[0] == 0xabcdef);
	for (i = 0; i < RVM_BOOTANONMAP_MAPPINGS; i++)
		REQUIRE(rvm_bootanonmap_free(views[i], 1));
	restored(before, resident, wired, dirty);

	for (i = 0; i < RVM_BOOTANONMAP_MAPPINGS; i++) {
		views[i] = rvm_bootanonmap_alloc(i + 1);
		REQUIRE(views[i]);
		*(volatile uint64_t *)views[i] = i;
	}
	REQUIRE(!rvm_bootanonmap_alloc(1));
	for (i = RVM_BOOTANONMAP_MAPPINGS; i != 0; i--) {
		REQUIRE(*(volatile uint64_t *)views[i - 1] == i - 1);
		REQUIRE(rvm_bootanonmap_free(views[i - 1], i));
	}
	restored(before, resident, wired, dirty);

	/* A pre-existing pmap alias must not be silently overwritten. */
	pg = uvm_pagealloc_strat(NULL, 0, NULL, UVM_PGA_USERESERVE, 0, 0);
	REQUIRE(pg);
	pmap_kenter_pa(RVM_BOOTALLOC_BASE, VM_PAGE_TO_PHYS(pg), VM_PROT_READ | VM_PROT_WRITE, 0);
	pmap_update(pmap_kernel());
	REQUIRE(!rvm_bootanonmap_alloc(1));
	REQUIRE(pmap_extract(pmap_kernel(), RVM_BOOTALLOC_BASE, &a) && a == VM_PAGE_TO_PHYS(pg));
	pmap_kremove(RVM_BOOTALLOC_BASE, PAGE_SIZE); pmap_update(pmap_kernel());
	uvm_pagefree(pg);
	restored(before, resident, wired, dirty);

	for (j = 0; j < 6; j++) {
		n = RVM_BOOTALLOC_SLOTS - j;
		for (i = 0; i < n; i++) {
			reservations[i] = uvm_km_alloc(kernel_map, PAGE_SIZE, 0,
			    UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
			REQUIRE(reservations[i]);
		}
		REQUIRE(!rvm_bootanonmap_alloc(PAGE_SIZE));
		(void)rvm_bootamap_fini(); (void)rvm_bootanon_fini();
		REQUIRE(kernel_map->nentries == (int)n && rvm_bootanonmap_live() == 0);
		for (i = 0; i < n; i++)
			uvm_km_free(kernel_map, reservations[i], PAGE_SIZE, UVM_KMF_VAONLY);
		restored(before, resident, wired, dirty);
	}

	first = rvm_bootanonmap_alloc(1);
	REQUIRE(first && rvm_bootanonmap_free(first, 1));
	for (n = 0; n < __arraycount(blocks); n++) {
		blocks[n] = kmem_alloc(1, KM_NOSLEEP);
		if (!blocks[n]) break;
	}
	REQUIRE(n > 128 && n < __arraycount(blocks));
	snapshot = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	entries = kernel_map->nentries;
	for (j = 0; j < 3; j++) {
		REQUIRE(!rvm_bootanonmap_alloc(1));
		REQUIRE(rvm_bootheap_live() == n && kernel_map->nentries == entries);
		REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == snapshot);
		kmem_free(blocks[--n], 1);
	}
	first = rvm_bootanonmap_alloc(1);
	REQUIRE(first && rvm_bootanonmap_free(first, 1));
	while (n != 0) kmem_free(blocks[--n], 1);
	restored(before, resident, wired, dirty);

	/* Run out of descriptors after three successfully mapped pages. */
	first = rvm_bootanonmap_alloc(1);
	REQUIRE(first && rvm_bootanonmap_free(first, 1));
	for (i = 0; i < RVM_BOOTANON_ANONS - 3; i++) {
		anons[i] = uvm_analloc(); REQUIRE(anons[i]);
	}
	snapshot = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	entries = kernel_map->nentries;
	REQUIRE(!rvm_bootanonmap_alloc(4 * PAGE_SIZE));
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == snapshot);
	REQUIRE(kernel_map->nentries == entries && rvm_bootanonmap_live() == 0);
	for (i = 0; i < RVM_BOOTANON_ANONS - 3; i++)
		REQUIRE(rvm_bootanon_drop(anons[i]));
	restored(before, resident, wired, dirty);

	/* Fail eager backing after two pages, then recover using the same caches. */
	first = rvm_bootanonmap_alloc(1);
	REQUIRE(first && rvm_bootanonmap_free(first, 1));
	for (n = 0; n < __arraycount(held); n++) {
		held[n] = uvm_pagealloc_strat(NULL, 0, NULL, UVM_PGA_USERESERVE, 0, 0);
		if (!held[n]) break;
	}
	REQUIRE(n > 5 && n < __arraycount(held));
	snapshot = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	for (i = 0; i < 3; i++) uvm_pagefree(held[--n]);
	REQUIRE(!rvm_bootanonmap_alloc(4 * PAGE_SIZE));
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == snapshot + 3);
	for (i = 0; i < 2; i++) uvm_pagefree(held[--n]);
	first = rvm_bootanonmap_alloc(4 * PAGE_SIZE);
	REQUIRE(first && rvm_bootanonmap_free(first, 4 * PAGE_SIZE));
	while (n != 0) uvm_pagefree(held[--n]);
	restored(before, resident, wired, dirty);

	first = rvm_bootanonmap_alloc(1);
	REQUIRE(first);
	amap = owner(first, PAGE_SIZE);
	for (n = 0; n < RVM_BOOTALLOC_SLOTS; n++) {
		reservations[n] = uvm_km_alloc(kernel_map, PAGE_SIZE, 0,
		    UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
		if (!reservations[n]) break;
	}
	REQUIRE(n > 0 && !rvm_bootanonmap_share(first) && amap->am_ref == 1);
	while (n != 0) uvm_km_free(kernel_map, reservations[--n], PAGE_SIZE, UVM_KMF_VAONLY);
	/* Full-range sharing remains valid if optional ppref allocation fails. */
	for (n = 0; n < __arraycount(blocks); n++) {
		blocks[n] = kmem_alloc(1, KM_NOSLEEP);
		if (!blocks[n]) break;
	}
	REQUIRE(n > 128 && n < __arraycount(blocks));
	second = rvm_bootanonmap_share(first);
	REQUIRE(second && amap->am_ref == 2);
	REQUIRE(rvm_bootanonmap_free(first, 1) && rvm_bootanonmap_free(second, 1));
	while (n != 0) kmem_free(blocks[--n], 1);
	restored(before, resident, wired, dirty);
	rvm_early_puts(anonmap_ok);
}
