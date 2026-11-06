/* Checked front end for real MI pool_get/put/destroy, UP and IRQ-off only. */
#include <sys/param.h>
#include <sys/pool.h>
#include <sys/systm.h>
#include <uvm/uvm.h>
#include <machine/bootalloc.h>
#include <machine/bootpool.h>
#include <machine/psl.h>
#include <machine/uvm_boot.h>

/* A power-of-two, cache-aligned stride avoids general immediate multiply
 * codegen for indexing pool storage during bootstrap.
 */
union pool_slot {
	struct pool pool;
	unsigned char storage[512];
};
__CTASSERT(sizeof(struct pool) <= 512);
static union pool_slot pools[RVM_BOOTPOOL_POOLS] __aligned(64)
    __attribute__((section(".data"))) = { 0 };
static unsigned char active[RVM_BOOTPOOL_POOLS]
    __attribute__((section(".data"))) = { 0 };
static struct pool_allocator allocator __attribute__((section(".data"))) = { 0 };
static int initialized __attribute__((section(".data"))) = 0;
static struct {
	struct pool *pool;
	void *item;
} outstanding[RVM_BOOTPOOL_ITEMS] __attribute__((section(".data"))) = { 0 };
const char rvm_pool_name[] = "rvm-pool";
const char rvm_pool_bad[] = "Invalid bootstrap pool operation";

void rvm_mi_pool_globals(void);
void rvm_mi_pool_setup(struct pool *, size_t, unsigned int, struct pool_allocator *);
void *rvm_mi_pool_get(struct pool *, int);
void rvm_mi_pool_put(struct pool *, void *);
void rvm_mi_pool_destroy(struct pool *);

static int
available(void)
{
	return rvm_uvm_boot.initialized && kernel_map != NULL &&
	    rvm_boot_get_cr() == RVM_CR_MMU && curcpu()->ci_cpl == IPL_NONE;
}

static int
valid(struct pool *pool)
{
	unsigned int i;
	for (i = 0; i < RVM_BOOTPOOL_POOLS; i++) {
		if (pool == &pools[i].pool && active[i])
			return available();
	}
	return 0;
}

static void *
page_alloc(struct pool *pool, int flags)
{
	if (!valid(pool) || !(flags & PR_NOWAIT) || (flags & PR_WAITOK))
		panic(rvm_pool_bad);
	return (void *)uvm_km_alloc(kernel_map, PAGE_SIZE, 0,
	    UVM_KMF_WIRED | UVM_KMF_NOWAIT);
}

static void
page_free(struct pool *pool, void *page)
{
	if (!valid(pool))
		panic(rvm_pool_bad);
	uvm_km_free(kernel_map, (vaddr_t)page, PAGE_SIZE, UVM_KMF_WIRED);
}

struct pool *
rvm_bootpool_create(size_t size, unsigned int alignment)
{
	unsigned int i;
	if (!available() || size == 0 || size > PAGE_SIZE / 2 ||
	    (alignment != 0 && (alignment < 8 || alignment > 256 || !powerof2(alignment))))
		return NULL;
	if (!initialized) {
		rvm_mi_pool_globals();
		allocator.pa_alloc = page_alloc;
		allocator.pa_free = page_free;
		allocator.pa_pagesz = PAGE_SIZE;
		initialized = 1;
	}
	for (i = 0; i < RVM_BOOTPOOL_POOLS; i++) {
		if (!active[i]) {
			rvm_mi_pool_setup(&pools[i].pool, size, alignment == 0 ? 8 : alignment,
			    &allocator);
			active[i] = 1;
			return &pools[i].pool;
		}
	}
	return NULL;
}

void *
pool_get(struct pool *pool, int flags)
{
	unsigned int i;
	void *item;
	if (!valid(pool) || !(flags & PR_NOWAIT) ||
	    (flags & ~(PR_NOWAIT | PR_ZERO)) != 0)
		panic(rvm_pool_bad);
	for (i = 0; i < RVM_BOOTPOOL_ITEMS; i++) {
		if (outstanding[i].item == NULL)
			break;
	}
	if (i == RVM_BOOTPOOL_ITEMS)
		return NULL;
	item = rvm_mi_pool_get(pool, flags);
	if (item != NULL) {
		outstanding[i].pool = pool;
		outstanding[i].item = item;
	}
	return item;
}

void
pool_put(struct pool *pool, void *item)
{
	unsigned int i;
	if (!valid(pool) || item == NULL)
		panic(rvm_pool_bad);
	for (i = 0; i < RVM_BOOTPOOL_ITEMS; i++) {
		if (outstanding[i].item == item && outstanding[i].pool == pool)
			break;
	}
	if (i == RVM_BOOTPOOL_ITEMS)
		panic(rvm_pool_bad);
	rvm_mi_pool_put(pool, item);
	outstanding[i].item = NULL;
	outstanding[i].pool = NULL;
}

int
rvm_bootpool_destroy(struct pool *pool)
{
	unsigned int i;
	if (!valid(pool) || pool->pr_nout != 0)
		return 0;
	rvm_mi_pool_destroy(pool);
	for (i = 0; i < RVM_BOOTPOOL_POOLS; i++) {
		if (pool == &pools[i].pool) {
			active[i] = 0;
			return 1;
		}
	}
	return 0;
}

int
rvm_bootpool_verify(void)
{
	struct pool *pool;
	unsigned char *first, *second;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	unsigned int i;

	pool = rvm_bootpool_create(37, 16);
	if (pool == NULL)
		return 0;
	first = pool_get(pool, PR_NOWAIT | PR_ZERO);
	second = pool_get(pool, PR_NOWAIT | PR_ZERO);
	if (first == NULL || second == NULL || first == second ||
	    (((uintptr_t)first | (uintptr_t)second) & 15) != 0)
		return 0;
	for (i = 0; i < 37; i++) {
		if (first[i] != 0 || second[i] != 0)
			return 0;
		first[i] = i;
		second[i] = ~i;
	}
	pool_put(pool, first);
	pool_put(pool, second);
	if (!rvm_bootpool_destroy(pool))
		return 0;
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before;
}
