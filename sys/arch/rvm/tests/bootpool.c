/* Exercise real MI page lists, item allocation, ownership and destruction. */
#include <sys/pool.h>
#include <uvm/uvm.h>
#include <machine/bootpool.h>
#include <machine/bootheap.h>

const char pool_ok[] = "MI pool ownership, page lists and exhaustion verified.\n";
const char pool_bad[] = "Pool regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(pool_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)

static void
restored(int64_t before)
{
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(kernel_map->nentries == 0 && curcpu()->ci_cpl == IPL_NONE);
	REQUIRE(rvm_bootheap_live() == 0);
}

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	struct pool *pool, *other, *registry[RVM_BOOTPOOL_POOLS];
	unsigned char *items[RVM_BOOTPOOL_ITEMS], *item;
	unsigned int i, j, count;
	int64_t before;
	long resident, wired;

	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 20);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel());
	wired = pmap_wired_count(pmap_kernel());
	REQUIRE(!rvm_bootpool_create(0, 0));
	REQUIRE(!rvm_bootpool_create(PAGE_SIZE, 0));
	REQUIRE(!rvm_bootpool_create(32, 3));
	REQUIRE(!rvm_bootpool_create(32, 24));
	REQUIRE(!rvm_bootpool_create(32, 512));
	REQUIRE(!rvm_bootpool_destroy(NULL));

	pool = rvm_bootpool_create(256, 64);
	REQUIRE(pool != NULL && (pool->pr_roflags & PR_PHINPAGE));
	REQUIRE(!(pool->pr_roflags & PR_USEBMAP));
	count = pool->pr_itemsperpage + 2;
	REQUIRE(count <= RVM_BOOTPOOL_ITEMS && count > 2);
	for (i = 0; i < count; i++) {
		items[i] = pool_get(pool, PR_NOWAIT | PR_ZERO);
		REQUIRE(items[i] != NULL && ((uintptr_t)items[i] & 63) == 0);
		for (j = 0; j < 256; j++) {
			REQUIRE(items[i][j] == 0);
			items[i][j] = (i + j) & 255;
		}
	}
	REQUIRE(pool->pr_npages == 2 && pool->pr_nout == count);
	REQUIRE(!rvm_bootpool_destroy(pool));
	for (i = 0; i < count; i++) {
		for (j = 0; j < 256; j++)
			REQUIRE(items[i][j] == ((i + j) & 255));
	}
	for (i = 0; i < count - 2; i++)
		pool_put(pool, items[i]);
	REQUIRE(pool->pr_nout == 2 && pool->pr_nidle == 1);
	REQUIRE(!rvm_bootpool_destroy(pool));
	pool_put(pool, items[count - 2]);
	pool_put(pool, items[count - 1]);
	REQUIRE(pool->pr_nout == 0 && pool->pr_nidle == 2);
	item = pool_get(pool, PR_NOWAIT | PR_ZERO);
	REQUIRE(item != NULL && pool->pr_npages == 2);
	for (i = 0; i < 256; i++)
		REQUIRE(item[i] == 0);
	pool_put(pool, item);
	REQUIRE(rvm_bootpool_destroy(pool));
	REQUIRE(!rvm_bootpool_destroy(pool));
	restored(before);

	/* Large items use MI's bitmap representation inside the page header. */
	pool = rvm_bootpool_create(PAGE_SIZE / 4, 64);
	REQUIRE(pool != NULL && (pool->pr_roflags & PR_USEBMAP));
	count = pool->pr_itemsperpage + 1;
	REQUIRE(count <= RVM_BOOTPOOL_ITEMS);
	for (i = 0; i < count; i++) {
		items[i] = pool_get(pool, PR_NOWAIT | PR_ZERO);
		REQUIRE(items[i] != NULL);
		for (j = 0; j < PAGE_SIZE / 4; j++)
			REQUIRE(items[i][j] == 0);
	}
	REQUIRE(pool->pr_npages == 2);
	for (i = 0; i < count; i++)
		pool_put(pool, items[i]);
	REQUIRE(rvm_bootpool_destroy(pool));
	restored(before);

	/* The ownership table is a separate limit, shared across all pools. */
	pool = rvm_bootpool_create(37, 16);
	other = rvm_bootpool_create(17, 8);
	REQUIRE(pool && other && pool->pr_poolid != other->pr_poolid);
	for (i = 0; i < RVM_BOOTPOOL_ITEMS; i++) {
		items[i] = pool_get(pool, PR_NOWAIT);
		REQUIRE(items[i] != NULL);
	}
	REQUIRE(pool_get(pool, PR_NOWAIT) == NULL);
	REQUIRE(pool_get(other, PR_NOWAIT) == NULL && other->pr_npages == 0);
	for (i = 0; i < RVM_BOOTPOOL_ITEMS; i++)
		pool_put(pool, items[i]);
	item = pool_get(other, PR_NOWAIT | PR_ZERO);
	REQUIRE(item != NULL && *item == 0);
	pool_put(other, item);
	REQUIRE(rvm_bootpool_destroy(pool) && rvm_bootpool_destroy(other));
	restored(before);

	/* One item per page hits the backing map limit before record limits. */
	pool = rvm_bootpool_create(PAGE_SIZE / 2, 8);
	REQUIRE(pool != NULL && pool->pr_itemsperpage == 1);
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		items[i] = pool_get(pool, PR_NOWAIT);
		REQUIRE(items[i] != NULL);
	}
	REQUIRE(pool_get(pool, PR_NOWAIT) == NULL);
	REQUIRE(pool->pr_nout == RVM_BOOTALLOC_SLOTS);
	REQUIRE(!(pool->pr_flags & PR_GROWING));
	REQUIRE(pool->pr_nfail > 0 && pool->pr_npages == RVM_BOOTALLOC_SLOTS);
	pool_put(pool, items[0]);
	items[0] = pool_get(pool, PR_NOWAIT | PR_ZERO);
	REQUIRE(items[0] != NULL && *items[0] == 0);
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++)
		pool_put(pool, items[i]);
	REQUIRE(rvm_bootpool_destroy(pool));
	restored(before);

	/* Destroying/recreating pools also exercises the shared allocator refcount. */
	for (j = 0; j < 3; j++) {
		for (i = 0; i < RVM_BOOTPOOL_POOLS; i++) {
			registry[i] = rvm_bootpool_create(32, 8);
			REQUIRE(registry[i] != NULL);
		}
		REQUIRE(rvm_bootpool_create(32, 8) == NULL);
		for (i = 0; i < RVM_BOOTPOOL_POOLS; i++) {
			item = pool_get(registry[i], PR_NOWAIT | PR_ZERO);
			REQUIRE(item != NULL && *item == 0);
			pool_put(registry[i], item);
			REQUIRE(rvm_bootpool_destroy(registry[i]));
		}
		restored(before);
	}
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident);
	REQUIRE(pmap_wired_count(pmap_kernel()) == wired);
	rvm_early_puts(pool_ok);
}
