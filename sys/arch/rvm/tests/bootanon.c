/* Resident MI anons: references, cache reuse, page status and resource limits. */
#include <uvm/uvm.h>
#include <machine/bootanon.h>
#include <machine/bootcache.h>

const char anon_ok[] = "MI anonymous descriptors, pages and exhaustion verified.\n";
const char anon_bad[] = "Anonymous page regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(anon_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)

static void
restored(int64_t before, long resident, long wired, int64_t clean, int64_t dirty)
{
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_ANONCLEAN] == clean);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_ANONDIRTY] == dirty);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_ANONUNKNOWN] == 0);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident);
	REQUIRE(pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(kernel_map->nentries == 0 && curcpu()->ci_cpl == IPL_NONE);
	REQUIRE(curlwp->l_nopreempt == 0);
}

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	struct vm_anon *anons[RVM_BOOTANON_ANONS], *anon;
	struct vm_page *pages[8], *held[1024], *pg;
	pool_cache_t caches[RVM_BOOTCACHE_CACHES], cache;
	unsigned char *items[RVM_BOOTCACHE_ITEMS];
	vaddr_t reservations[RVM_BOOTALLOC_SLOTS];
	volatile uint64_t *data;
	krwlock_t *lock;
	paddr_t pa;
	unsigned int i, j, n;
	int64_t before, clean, dirty;
	long resident, wired;

	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 20 && !rvm_bootanon_fini());
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	clean = curcpu()->ci_counts[CPU_COUNT_ANONCLEAN];
	dirty = curcpu()->ci_counts[CPU_COUNT_ANONDIRTY];
	resident = pmap_resident_count(pmap_kernel());
	wired = pmap_wired_count(pmap_kernel());
	REQUIRE(!rvm_bootanon_hold(NULL) && !rvm_bootanon_drop(NULL));
	REQUIRE(rvm_bootanon_alloc((struct vm_anon *)1) == NULL);
	uvm_anon_init();
	REQUIRE(rvm_bootanon_init());
	for (i = 0; i < RVM_BOOTANON_ANONS; i++) {
		anons[i] = uvm_analloc();
		REQUIRE(anons[i] != NULL && anons[i]->an_ref == 1);
		REQUIRE(anons[i]->an_lock == NULL && anons[i]->an_page == NULL);
		REQUIRE(anons[i]->an_swslot == 0 && rvm_bootanon_valid(anons[i]));
		for (j = 0; j < i; j++)
			REQUIRE(anons[j] != anons[i]);
	}
	REQUIRE(uvm_analloc() == NULL && !rvm_bootanon_fini());
	for (i = 0; i < __arraycount(pages); i++) {
		pages[i] = rvm_bootanon_alloc(anons[i]);
		REQUIRE(pages[i] != NULL && pages[i]->uanon == anons[i]);
		REQUIRE(anons[i]->an_page == pages[i] && pages[i]->uobject == NULL);
		REQUIRE(pages[i]->wire_count == 0 && pages[i]->loan_count == 0);
		REQUIRE((pages[i]->flags & (PG_BUSY | PG_FAKE | PG_ANON)) == PG_ANON);
		REQUIRE(uvm_pagegetdirty(pages[i]) == UVM_PAGE_STATUS_DIRTY);
		data = (volatile uint64_t *)(uintptr_t)VM_PAGE_TO_PHYS(pages[i]);
		for (j = 0; j < PAGE_SIZE / sizeof(*data); j++) {
			REQUIRE(data[j] == 0);
			data[j] = 0x52564d414e4f4e00UL + i;
		}
		REQUIRE(rvm_bootanon_alloc(anons[i]) == NULL);
		REQUIRE(rvm_bootanon_hold(anons[i]) && anons[i]->an_ref == 2);
		REQUIRE(rvm_bootanon_drop(anons[i]) && anons[i]->an_ref == 1);
	}
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_ANONDIRTY] == dirty + 8);
	anon = anons[0];
	lock = anon->an_lock;
	rw_enter(lock, RW_WRITER);
	kpreempt_disable(); kpreempt_disable();
	uvm_pagemarkdirty(pages[0], UVM_PAGE_STATUS_CLEAN);
	REQUIRE(curlwp->l_nopreempt == 2);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_ANONCLEAN] == clean + 1);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_ANONDIRTY] == dirty + 7);
	uvm_pagemarkdirty(pages[0], UVM_PAGE_STATUS_DIRTY);
	kpreempt_enable(); kpreempt_enable();
	uvm_pagefree(pages[0]);
	REQUIRE(anon->an_page == NULL && anon->an_ref == 1);
	rw_exit(lock);
	pages[0] = rvm_bootanon_alloc(anon);
	REQUIRE(pages[0] != NULL);
	data = (volatile uint64_t *)(uintptr_t)VM_PAGE_TO_PHYS(pages[0]);
	for (j = 0; j < PAGE_SIZE / sizeof(*data); j++)
		REQUIRE(data[j] == 0);

	/* Aliases at separate leaf tables are both removed on final release. */
	pmap_kenter_pa(VM_KERNEL_VM_BASE, VM_PAGE_TO_PHYS(pages[0]),
	    VM_PROT_READ | VM_PROT_WRITE, 0);
	pmap_kenter_pa(VM_KERNEL_VM_BASE + (16UL << 20), VM_PAGE_TO_PHYS(pages[0]),
	    VM_PROT_READ | VM_PROT_WRITE, 0);
	pmap_update(pmap_kernel());
	*(volatile uint64_t *)VM_KERNEL_VM_BASE = 0x1122334455667788UL;
	REQUIRE(*(volatile uint64_t *)(VM_KERNEL_VM_BASE + (16UL << 20)) ==
	    0x1122334455667788UL);
	anon->an_ref = (uintptr_t)-1;
	REQUIRE(!rvm_bootanon_hold(anon) && anon->an_ref == (uintptr_t)-1);
	anon->an_ref = 1;
	for (i = 0; i < RVM_BOOTANON_ANONS; i++)
		REQUIRE(rvm_bootanon_drop(anons[i]));
	REQUIRE(!rvm_bootanon_valid(anon) && !rvm_bootanon_drop(anon));
	REQUIRE(!pmap_extract(pmap_kernel(), VM_KERNEL_VM_BASE, &pa));
	REQUIRE(!pmap_extract(pmap_kernel(), VM_KERNEL_VM_BASE + (16UL << 20), &pa));
	/* MI cache hits must expose reset descriptors without rerunning ctor. */
	for (i = 0; i < RVM_BOOTANON_ANONS; i++) {
		anons[i] = uvm_analloc();
		REQUIRE(anons[i] != NULL && anons[i]->an_ref == 1);
		REQUIRE(anons[i]->an_lock == NULL && anons[i]->an_page == NULL);
	}
	for (i = 0; i < RVM_BOOTANON_ANONS; i++)
		REQUIRE(rvm_bootanon_drop(anons[i]));
	REQUIRE(rvm_bootanon_fini());
	restored(before, resident, wired, clean, dirty);

	/* The low-level MI free contract still accepts an unbound, empty anon. */
	anon = uvm_analloc();
	REQUIRE(anon != NULL);
	anon->an_ref = 0;
	uvm_anfree(anon);
	REQUIRE(rvm_bootanon_fini());
	restored(before, resident, wired, clean, dirty);

	/* Fail cache metadata initialization at either page, or item backing. */
	for (j = 0; j < 3; j++) {
		n = RVM_BOOTALLOC_SLOTS - j;
		for (i = 0; i < n; i++) {
			reservations[i] = uvm_km_alloc(kernel_map, PAGE_SIZE, 0,
			    UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
			REQUIRE(reservations[i] != 0);
		}
		REQUIRE(uvm_analloc() == NULL);
		REQUIRE(rvm_bootanon_fini() == (j == 2));
		REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
		for (i = 0; i < n; i++)
			uvm_km_free(kernel_map, reservations[i], PAGE_SIZE, UVM_KMF_VAONLY);
		restored(before, resident, wired, clean, dirty);
	}

	for (i = 0; i < RVM_BOOTCACHE_CACHES; i++) {
		caches[i] = rvm_bootcache_create(37, 8, 0, NULL, NULL, NULL);
		REQUIRE(caches[i] != NULL);
	}
	REQUIRE(uvm_analloc() == NULL && !rvm_bootanon_fini());
	for (i = 0; i < RVM_BOOTCACHE_CACHES; i++)
		REQUIRE(rvm_bootcache_destroy(caches[i]));
	restored(before, resident, wired, clean, dirty);

	cache = rvm_bootcache_create(37, 8, 0, NULL, NULL, NULL);
	REQUIRE(cache != NULL);
	for (i = 0; i < RVM_BOOTCACHE_ITEMS; i++) {
		items[i] = pool_cache_get(cache, PR_NOWAIT);
		REQUIRE(items[i] != NULL);
	}
	REQUIRE(uvm_analloc() == NULL && rvm_bootanon_fini());
	for (i = 0; i < RVM_BOOTCACHE_ITEMS; i++)
		pool_cache_put(cache, items[i]);
	REQUIRE(rvm_bootcache_destroy(cache));
	restored(before, resident, wired, clean, dirty);

	anon = uvm_analloc();
	REQUIRE(anon != NULL);
	for (n = 0; n < __arraycount(held); n++) {
		held[n] = uvm_pagealloc_strat(NULL, 0, NULL, UVM_PGA_USERESERVE, 0, 0);
		if (held[n] == NULL)
			break;
	}
	REQUIRE(n > 0 && n < __arraycount(held));
	REQUIRE(rvm_bootanon_alloc(anon) == NULL);
	REQUIRE(anon->an_page == NULL && anon->an_ref == 1);
	uvm_pagefree(held[--n]);
	pg = rvm_bootanon_alloc(anon);
	REQUIRE(pg != NULL);
	data = (volatile uint64_t *)(uintptr_t)VM_PAGE_TO_PHYS(pg);
	REQUIRE(data[0] == 0 && data[PAGE_SIZE / sizeof(*data) - 1] == 0);
	REQUIRE(rvm_bootanon_drop(anon));
	while (n != 0)
		uvm_pagefree(held[--n]);
	REQUIRE(rvm_bootanon_fini());
	restored(before, resident, wired, clean, dirty);
	rvm_early_puts(anon_ok);
}
