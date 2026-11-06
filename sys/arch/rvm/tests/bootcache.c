/* Real MI magazine transitions, constructor failures and resource rollback. */
#include <uvm/uvm.h>
#include <machine/bootcache.h>
#include <machine/bootheap.h>

const char cache_ok[] = "MI pool cache magazines, callbacks and exhaustion verified.\n";
const char cache_bad[] = "Pool cache regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(cache_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
struct cache_counts { unsigned int ctor, dtor, fail; };

static int
construct(void *arg, void *item, int flags)
{
	struct cache_counts *c = arg;
	REQUIRE(flags == PR_NOWAIT);
	c->ctor++;
	if (c->fail)
		return 1;
	memset(item, 0x5a, 37);
	return 0;
}

static void
destruct(void *arg, void *item)
{
	struct cache_counts *c = arg;
	REQUIRE(((unsigned char *)item)[36] == 0x5a);
	c->dtor++;
}

static void
restored(int64_t before, long resident, long wired)
{
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(kernel_map->nentries == 0 && curcpu()->ci_cpl == IPL_NONE);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident);
	REQUIRE(pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(rvm_bootheap_live() == 0);
}

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	pool_cache_t pc, other, registry[RVM_BOOTCACHE_CACHES];
	unsigned char *items[RVM_BOOTCACHE_ITEMS], *item;
	vaddr_t reservations[RVM_BOOTALLOC_SLOTS];
	struct vm_page *held[1024];
	struct cache_counts counts;
	unsigned int i, j, n, large;
	paddr_t pa;
	int64_t before;
	long resident, wired;

	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 20);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel());
	wired = pmap_wired_count(pmap_kernel());
	REQUIRE(!rvm_bootcache_create(0, 0, 0, NULL, NULL, NULL));
	REQUIRE(!rvm_bootcache_create(PAGE_SIZE, 0, 0, NULL, NULL, NULL));
	REQUIRE(!rvm_bootcache_create(37, 3, 0, NULL, NULL, NULL));
	REQUIRE(!rvm_bootcache_create(37, 24, 0, NULL, NULL, NULL));
	REQUIRE(!rvm_bootcache_create(37, 512, 0, NULL, NULL, NULL));
	REQUIRE(!rvm_bootcache_create(37, 0, PR_PSERIALIZE, NULL, NULL, NULL));
	REQUIRE(!rvm_bootcache_destroy(NULL));

	for (large = 0; large < 2; large++) {
		memset(&counts, 0, sizeof(counts));
		pc = rvm_bootcache_create(37, 256, large ? PR_LARGECACHE : 0,
		    construct, destruct, &counts);
		REQUIRE(pc != NULL);
		for (i = 0; i < RVM_BOOTCACHE_ITEMS; i++) {
			items[i] = pool_cache_get_paddr(pc, PR_NOWAIT, &pa);
			REQUIRE(items[i] != NULL && ((uintptr_t)items[i] & 255) == 0);
			REQUIRE(pa == POOL_PADDR_INVALID && items[i][36] == 0x5a);
			for (j = 0; j < i; j++)
				REQUIRE(items[j] != items[i]);
			items[i][0] = i;
		}
		REQUIRE(counts.ctor == RVM_BOOTCACHE_ITEMS && counts.dtor == 0);
		REQUIRE(!rvm_bootcache_destroy(pc));
		REQUIRE(pool_cache_get(pc, PR_NOWAIT) == NULL);
		other = rvm_bootcache_create(37, 8, 0, NULL, NULL, NULL);
		REQUIRE(other != NULL && pool_cache_get(other, PR_NOWAIT) == NULL);
		for (i = 0; i < RVM_BOOTCACHE_ITEMS; i++) {
			REQUIRE(items[i][0] == (unsigned char)i);
			pool_cache_put(pc, items[i]);
		}
		REQUIRE(counts.dtor == 0 && pc->pc_fullgroups != NULL);
		REQUIRE(pc->pc_cpu0.cc_nfull > 0);
		for (i = 0; i < RVM_BOOTCACHE_ITEMS; i++) {
			items[i] = pool_cache_get(pc, PR_NOWAIT);
			REQUIRE(items[i] != NULL && items[i][36] == 0x5a);
		}
		REQUIRE(counts.ctor == RVM_BOOTCACHE_ITEMS && counts.dtor == 0);
		REQUIRE(pc->pc_cpu0.cc_hits >= 2 * RVM_BOOTCACHE_ITEMS);
		/* Invalidate cached objects while one client still owns its object. */
		for (i = 1; i < RVM_BOOTCACHE_ITEMS; i++)
			pool_cache_put(pc, items[i]);
		pool_cache_invalidate(pc);
		REQUIRE(counts.dtor == RVM_BOOTCACHE_ITEMS - 1);
		REQUIRE(items[0][36] == 0x5a && pc->pc_pool.pr_nout == 1);
		REQUIRE(!rvm_bootcache_destroy(pc));
		pool_cache_put(pc, items[0]);
		REQUIRE(rvm_bootcache_destroy(pc));
		REQUIRE(counts.dtor == counts.ctor);
		/* The other cache remains usable after shared magazine invalidation. */
		item = pool_cache_get(other, PR_NOWAIT);
		REQUIRE(item != NULL);
		pool_cache_put(other, item);
		REQUIRE(rvm_bootcache_destroy(other));
		restored(before, resident, wired);
	}

	memset(&counts, 0, sizeof(counts));
	pc = rvm_bootcache_create(37, 16, 0, construct, destruct, &counts);
	REQUIRE(pc != NULL);
	for (i = 0; i < 8; i++) {
		counts.fail = 1;
		REQUIRE(pool_cache_get(pc, PR_NOWAIT) == NULL);
		REQUIRE(pc->pc_pool.pr_nout == 0 && counts.dtor == i);
		counts.fail = 0;
		item = pool_cache_get(pc, PR_NOWAIT);
		REQUIRE(item != NULL);
		pool_cache_put(pc, item);
		pool_cache_invalidate(pc);
		REQUIRE(counts.ctor == 2 * (i + 1) && counts.dtor == i + 1);
	}
	REQUIRE(rvm_bootcache_destroy(pc));
	restored(before, resident, wired);

	/* Both normal and bitmap pool items are usable behind magazines. */
	pc = rvm_bootcache_create(PAGE_SIZE / 2, 64, 0, NULL, NULL, NULL);
	REQUIRE(pc != NULL && pc->pc_pool.pr_itemsperpage == 1);
	for (i = 0; i < RVM_BOOTALLOC_SLOTS - 2; i++) {
		items[i] = pool_cache_get(pc, PR_NOWAIT);
		REQUIRE(items[i] != NULL);
	}
	REQUIRE(pool_cache_get(pc, PR_NOWAIT) == NULL);
	REQUIRE(!(pc->pc_pool.pr_flags & PR_GROWING));
	for (i = 0; i < RVM_BOOTALLOC_SLOTS - 2; i++)
		pool_cache_put(pc, items[i]);
	REQUIRE(rvm_bootcache_destroy(pc));
	restored(before, resident, wired);

	/* Metadata failure must roll back even if its first page succeeded. */
	for (j = 0; j < 3; j++) {
		n = RVM_BOOTALLOC_SLOTS - j;
		for (i = 0; i < n; i++) {
			reservations[i] = uvm_km_alloc(kernel_map, PAGE_SIZE, 0,
			    UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
			REQUIRE(reservations[i] != 0);
		}
		pc = rvm_bootcache_create(37, 8, 0, NULL, NULL, NULL);
		if (j < 2) {
			REQUIRE(pc == NULL);
			REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
		} else {
			REQUIRE(pc != NULL && pool_cache_get(pc, PR_NOWAIT) == NULL);
			REQUIRE(rvm_bootcache_destroy(pc));
		}
		for (i = 0; i < n; i++)
			uvm_km_free(kernel_map, reservations[i], PAGE_SIZE, UVM_KMF_VAONLY);
		restored(before, resident, wired);
	}

	/* Exhaust physical pages independently of the virtual reservation limit. */
	for (n = 0; n < __arraycount(held); n++) {
		held[n] = uvm_pagealloc_strat(NULL, 0, NULL, UVM_PGA_USERESERVE, 0, 0);
		if (held[n] == NULL)
			break;
	}
	REQUIRE(n > 2 && n < __arraycount(held));
	REQUIRE(rvm_bootcache_create(37, 8, 0, NULL, NULL, NULL) == NULL);
	uvm_pagefree(held[--n]);
	REQUIRE(rvm_bootcache_create(37, 8, 0, NULL, NULL, NULL) == NULL);
	while (n != 0)
		uvm_pagefree(held[--n]);
	restored(before, resident, wired);

	for (j = 0; j < 3; j++) {
		for (i = 0; i < RVM_BOOTCACHE_CACHES; i++) {
			registry[i] = rvm_bootcache_create(1, 8, i & 1 ? PR_LARGECACHE : 0,
			    NULL, NULL, NULL);
			REQUIRE(registry[i] != NULL);
		}
		REQUIRE(rvm_bootcache_create(1, 8, 0, NULL, NULL, NULL) == NULL);
		for (i = 0; i < RVM_BOOTCACHE_CACHES; i++) {
			item = pool_cache_get(registry[i], PR_NOWAIT);
			REQUIRE(item != NULL);
			pool_cache_put(registry[i], item);
			REQUIRE(rvm_bootcache_destroy(registry[i]));
			REQUIRE(!rvm_bootcache_destroy(registry[i]));
		}
		restored(before, resident, wired);
	}
	rvm_early_puts(cache_ok);
}
