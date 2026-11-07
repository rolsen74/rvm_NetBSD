/* MI sparse slot arrays, whole-map references, owner locks and rollback. */
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/bootamap.h>
#include <machine/bootanon.h>
#include <machine/bootcache.h>
#include <machine/bootheap.h>

const char amap_ok[] = "MI anonymous maps, sharing and exhaustion verified.\n";
const char amap_bad[] = "Anonymous map regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(amap_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)

static void
restored(int64_t before, long resident, long wired, int64_t dirty)
{
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_ANONDIRTY] == dirty);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_ANONCLEAN] == 0);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_ANONUNKNOWN] == 0);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident);
	REQUIRE(pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(rvm_bootheap_live() == 0 && kernel_map->nentries == 0);
	REQUIRE(curcpu()->ci_cpl == IPL_NONE && curlwp->l_nopreempt == 0);
}

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	struct vm_amap *map, *other, *registry[RVM_BOOTAMAP_MAPS];
	struct vm_anon *anons[36], *spare;
	struct vm_page *pg, *moved, *held[1024];
	unsigned char *blocks[RVM_BOOTHEAP_RECORDS];
	vaddr_t reservations[RVM_BOOTALLOC_SLOTS];
	pool_cache_t caches[RVM_BOOTCACHE_CACHES];
	unsigned int i, j, n, slot;
	int64_t before, dirty, snapshot;
	long resident, wired;
	paddr_t pa;

	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32 && !rvm_bootamap_fini());
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	dirty = curcpu()->ci_counts[CPU_COUNT_ANONDIRTY];
	resident = pmap_resident_count(pmap_kernel());
	wired = pmap_wired_count(pmap_kernel());
	REQUIRE(!rvm_bootamap_create(0) && !rvm_bootamap_create(PAGE_SIZE + 1));
	REQUIRE(!rvm_bootamap_create((RVM_BOOTAMAP_SLOTS + 1UL) * PAGE_SIZE));
	REQUIRE(!rvm_bootamap_drop((struct vm_amap *)1));
	REQUIRE(!rvm_bootamap_hold(NULL));
	REQUIRE(!rvm_bootamap_lookup((struct vm_amap *)1, 0));

	map = rvm_bootamap_create(65 * PAGE_SIZE);
	other = rvm_bootamap_create(4 * PAGE_SIZE);
	REQUIRE(map && other && map->am_lock != other->am_lock);
	REQUIRE(map->am_nslot == 65 && map->am_maxslot >= 65 && map->am_nused == 0);
	for (i = 0; i < __arraycount(anons); i++) {
		slot = (i * 7) % 65;
		anons[i] = uvm_analloc();
		REQUIRE(anons[i] != NULL && rvm_bootamap_put(map, slot * PAGE_SIZE, anons[i]));
		REQUIRE(anons[i]->an_lock == map->am_lock && anons[i]->an_ref == 1);
		REQUIRE(!rvm_bootamap_put(other, 0, anons[i]));
		REQUIRE(!rvm_bootanon_drop(anons[i]) && !rvm_bootanon_hold(anons[i]));
		REQUIRE(rvm_bootanon_alloc(anons[i]) == NULL);
		REQUIRE(rvm_bootamap_lookup(map, slot * PAGE_SIZE) == anons[i]);
		if (i < 4) {
			pg = rvm_bootamap_page(map, slot * PAGE_SIZE);
			REQUIRE(pg && pg->uanon == anons[i] && pg->uobject == NULL);
			volatile uint64_t *data = (volatile uint64_t *)(uintptr_t)VM_PAGE_TO_PHYS(pg);
			REQUIRE(data[0] == 0 && data[PAGE_SIZE / sizeof(*data) - 1] == 0);
			data[0] = 0x52564d414d415000UL + i;
		}
	}
	REQUIRE(map->am_nused == __arraycount(anons));
	REQUIRE(!rvm_bootamap_fini() && !rvm_bootanon_fini());
	REQUIRE(!rvm_bootamap_lookup(map, 65 * PAGE_SIZE));
	REQUIRE(!rvm_bootamap_lookup(map, 1) && !rvm_bootamap_take(map, 2 * PAGE_SIZE));
	spare = uvm_analloc();
	REQUIRE(spare != NULL && !rvm_bootamap_put(map, 0, spare));
	REQUIRE(rvm_bootanon_drop(spare));
	for (i = 0; i < 3; i++)
		REQUIRE(rvm_bootamap_hold(map));
	REQUIRE(map->am_ref == 4 && (map->am_flags & AMAP_SHARED));
	REQUIRE(!rvm_bootamap_take(map, 0));
	REQUIRE(!rvm_bootamap_put(map, PAGE_SIZE, anons[0]));
	REQUIRE(rvm_bootamap_page(map, 0) == anons[0]->an_page);
	for (i = 0; i < 3; i++)
		REQUIRE(rvm_bootamap_drop(map));
	REQUIRE(map->am_ref == 1 && !(map->am_flags & AMAP_SHARED));
	for (i = 1; i < 1024; i++)
		REQUIRE(rvm_bootamap_hold(map));
	REQUIRE(map->am_ref == 1024 && !rvm_bootamap_hold(map));
	for (i = 1; i < 1024; i++)
		REQUIRE(rvm_bootamap_drop(map));
	REQUIRE(map->am_ref == 1 && !(map->am_flags & AMAP_SHARED));
	for (i = 0; i < __arraycount(anons); i++)
		REQUIRE(anons[i]->an_ref == 1);

	/* MI unadd compacts the used-slot array and fixes the moved back pointer. */
	moved = anons[1]->an_page;
	REQUIRE(rvm_bootamap_take(map, 7 * PAGE_SIZE) == anons[1]);
	REQUIRE(map->am_nused == __arraycount(anons) - 1);
	for (i = 0; i < (unsigned int)map->am_nused; i++)
		REQUIRE(map->am_bckptr[map->am_slots[i]] == (int)i);
	REQUIRE(rvm_bootamap_put(other, 0, anons[1]));
	REQUIRE(rvm_bootamap_page(other, 0) == moved);
	REQUIRE(*(volatile uint64_t *)(uintptr_t)VM_PAGE_TO_PHYS(moved) ==
	    0x52564d414d415001UL);
	REQUIRE(rvm_bootamap_take(other, 0) == anons[1] && rvm_bootanon_drop(anons[1]));
	pg = anons[0]->an_page;
	pmap_kenter_pa(VM_KERNEL_VM_BASE, VM_PAGE_TO_PHYS(pg), VM_PROT_READ | VM_PROT_WRITE, 0);
	pmap_update(pmap_kernel());
	REQUIRE(rvm_bootamap_drop(map) && !rvm_bootamap_drop(map));
	REQUIRE(!pmap_extract(pmap_kernel(), VM_KERNEL_VM_BASE, &pa));
	REQUIRE(rvm_bootamap_drop(other));
	REQUIRE(rvm_bootamap_fini() && rvm_bootanon_fini());
	restored(before, resident, wired, dirty);

	for (j = 0; j < 3; j++) {
		for (i = 0; i < RVM_BOOTAMAP_MAPS; i++) {
			registry[i] = rvm_bootamap_create(RVM_BOOTAMAP_SLOTS * PAGE_SIZE);
			REQUIRE(registry[i] != NULL);
		}
		REQUIRE(rvm_bootamap_create(PAGE_SIZE) == NULL);
		for (i = 0; i < RVM_BOOTAMAP_MAPS; i++)
			REQUIRE(rvm_bootamap_drop(registry[i]));
		REQUIRE(rvm_bootamap_fini());
		restored(before, resident, wired, dirty);
	}

	/* Fail each metadata page, the descriptor backing, and heap import. */
	for (j = 0; j < 4; j++) {
		n = RVM_BOOTALLOC_SLOTS - j;
		for (i = 0; i < n; i++) {
			reservations[i] = uvm_km_alloc(kernel_map, PAGE_SIZE, 0,
			    UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
			REQUIRE(reservations[i]);
		}
		REQUIRE(!rvm_bootamap_create(PAGE_SIZE));
		REQUIRE(rvm_bootamap_fini() == (j >= 2));
		for (i = 0; i < n; i++)
			uvm_km_free(kernel_map, reservations[i], PAGE_SIZE, UVM_KMF_VAONLY);
		restored(before, resident, wired, dirty);
	}

	/* Exhaust heap tags after warming one cached descriptor. Freeing tail
	 * blocks permits one, two, then all three MI slot-array allocations.
	 */
	map = rvm_bootamap_create(8 * PAGE_SIZE);
	REQUIRE(map && rvm_bootamap_drop(map));
	for (n = 0; n < __arraycount(blocks); n++) {
		blocks[n] = kmem_alloc(1, KM_NOSLEEP);
		if (!blocks[n])
			break;
	}
	REQUIRE(n > 128 && n < __arraycount(blocks));
	snapshot = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	for (j = 0; j < 3; j++) {
		REQUIRE(!rvm_bootamap_create(8 * PAGE_SIZE));
		REQUIRE(rvm_bootheap_live() == n);
		REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == snapshot);
		kmem_free(blocks[--n], 1);
	}
	map = rvm_bootamap_create(8 * PAGE_SIZE);
	REQUIRE(map != NULL && rvm_bootamap_drop(map));
	while (n != 0)
		kmem_free(blocks[--n], 1);
	REQUIRE(rvm_bootamap_fini());
	restored(before, resident, wired, dirty);

	for (i = 0; i < RVM_BOOTCACHE_CACHES; i++) {
		caches[i] = rvm_bootcache_create(37, 8, 0, NULL, NULL, NULL);
		REQUIRE(caches[i] != NULL);
	}
	REQUIRE(!rvm_bootamap_create(PAGE_SIZE) && !rvm_bootamap_fini());
	for (i = 0; i < RVM_BOOTCACHE_CACHES; i++)
		REQUIRE(rvm_bootcache_destroy(caches[i]));
	restored(before, resident, wired, dirty);

	map = rvm_bootamap_create(PAGE_SIZE);
	REQUIRE(map && rvm_bootamap_drop(map));
	for (n = 0; n < __arraycount(held); n++) {
		held[n] = uvm_pagealloc_strat(NULL, 0, NULL, UVM_PGA_USERESERVE, 0, 0);
		if (!held[n])
			break;
	}
	REQUIRE(n > 0 && n < __arraycount(held));
	REQUIRE(!rvm_bootamap_create(PAGE_SIZE));
	uvm_pagefree(held[--n]);
	map = rvm_bootamap_create(PAGE_SIZE);
	REQUIRE(map && rvm_bootamap_drop(map) && rvm_bootamap_fini());
	while (n != 0)
		uvm_pagefree(held[--n]);
	restored(before, resident, wired, dirty);
	rvm_early_puts(amap_ok);
}
