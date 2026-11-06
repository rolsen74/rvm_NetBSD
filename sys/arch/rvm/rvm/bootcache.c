/* Checked ownership around real MI magazines, constructors and destruction.
 * One CPU with hardware interrupts disabled; no sleeping or callback reentry.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <uvm/uvm.h>
#include <machine/bootcache.h>
#include <machine/bootpool.h>
#include <machine/psl.h>
#include <machine/uvm_boot.h>

union cache_slot {
	struct pool_cache cache;
	unsigned char storage[1024];
};
__CTASSERT(sizeof(struct pool_cache) <= 1024);
static union cache_slot caches[RVM_BOOTCACHE_CACHES] __aligned(64)
    __attribute__((section(".data"))) = { 0 };
static unsigned char active[RVM_BOOTCACHE_CACHES]
    __attribute__((section(".data"))) = { 0 };
static struct {
	pool_cache_t cache;
	void *item;
} outstanding[RVM_BOOTCACHE_ITEMS] __attribute__((section(".data"))) = { 0 };
static struct pool_allocator allocator __attribute__((section(".data"))) = { 0 };
static int initialized __attribute__((section(".data"))) = 0;
static int in_operation __attribute__((section(".data"))) = 0;
const char rvm_cache_bad[] = "Invalid bootstrap pool cache operation";

int rvm_mi_cache_groups_init(struct pool_allocator *);
int rvm_mi_cache_group_pool(struct pool *);
void rvm_mi_cache_groups_fini(void);
void rvm_mi_cache_setup(pool_cache_t, size_t, unsigned int, unsigned int,
    struct pool_allocator *, int (*)(void *, void *, int),
    void (*)(void *, void *), void *);
void *rvm_mi_cache_get_paddr(pool_cache_t, int, paddr_t *);
void rvm_mi_cache_put_paddr(pool_cache_t, void *, paddr_t);
void rvm_mi_cache_invalidate(pool_cache_t);
void rvm_mi_cache_destroy(pool_cache_t);

static int
available(void)
{
	return rvm_uvm_boot.initialized && kernel_map != NULL &&
	    rvm_boot_get_cr() == RVM_CR_MMU && curcpu()->ci_cpl == IPL_NONE;
}

static int
valid(pool_cache_t pc)
{
	unsigned int i;
	for (i = 0; i < RVM_BOOTCACHE_CACHES; i++) {
		if (pc == &caches[i].cache && active[i])
			return 1;
	}
	return 0;
}

static int
valid_pool(struct pool *pool)
{
	unsigned int i;
	if (initialized && rvm_mi_cache_group_pool(pool))
		return 1;
	for (i = 0; i < RVM_BOOTCACHE_CACHES; i++) {
		if (pool == &caches[i].cache.pc_pool && active[i])
			return 1;
	}
	return 0;
}

static void *
page_alloc(struct pool *pool, int flags)
{
	if (!valid_pool(pool) || flags != PR_NOWAIT)
		panic(rvm_cache_bad);
	/* Magazine misses at IPL_VM may fall back to immediate destruction.
	 * Their metadata pages were preallocated at IPL_NONE; do not grow here.
	 */
	if (curcpu()->ci_cpl != IPL_NONE)
		return NULL;
	return (void *)uvm_km_alloc(kernel_map, PAGE_SIZE, 0,
	    UVM_KMF_WIRED | UVM_KMF_NOWAIT);
}

static void
page_free(struct pool *pool, void *page)
{
	if (!valid_pool(pool) || !available())
		panic(rvm_cache_bad);
	uvm_km_free(kernel_map, (vaddr_t)page, PAGE_SIZE, UVM_KMF_WIRED);
}

static int
no_ctor(void *arg __unused, void *item __unused, int flags __unused)
{
	return 0;
}

static void
no_dtor(void *arg __unused, void *item __unused)
{
}

static void
enter(pool_cache_t pc)
{
	if (!available() || !valid(pc) || in_operation)
		panic(rvm_cache_bad);
	in_operation = 1;
}

pool_cache_t
rvm_bootcache_create(size_t size, unsigned int alignment, unsigned int flags,
    int (*ctor)(void *, void *, int), void (*dtor)(void *, void *), void *arg)
{
	unsigned int i;
	if (in_operation)
		panic(rvm_cache_bad);
	if (!available() || size == 0 || size > PAGE_SIZE / 2 ||
	    (flags & ~PR_LARGECACHE) != 0 ||
	    (alignment != 0 && (alignment < 8 || alignment > 256 || !powerof2(alignment))))
		return NULL;
	for (i = 0; i < RVM_BOOTCACHE_CACHES; i++) {
		if (!active[i])
			break;
	}
	if (i == RVM_BOOTCACHE_CACHES || !rvm_bootpool_init())
		return NULL;
	in_operation = 1;
	if (!initialized) {
		allocator.pa_alloc = page_alloc;
		allocator.pa_free = page_free;
		allocator.pa_pagesz = PAGE_SIZE;
		initialized = 1;
		if (!rvm_mi_cache_groups_init(&allocator)) {
			initialized = 0;
			in_operation = 0;
			return NULL;
		}
	}
	active[i] = 1;
	rvm_mi_cache_setup(&caches[i].cache, size, alignment == 0 ? 8 : alignment,
	    flags, &allocator, ctor == NULL ? no_ctor : ctor,
	    dtor == NULL ? no_dtor : dtor, arg);
	in_operation = 0;
	return &caches[i].cache;
}

void *
pool_cache_get_paddr(pool_cache_t pc, int flags, paddr_t *pap)
{
	unsigned int i;
	void *item;
	enter(pc);
	if (flags != PR_NOWAIT)
		panic(rvm_cache_bad);
	for (i = 0; i < RVM_BOOTCACHE_ITEMS; i++) {
		if (outstanding[i].item == NULL)
			break;
	}
	item = i == RVM_BOOTCACHE_ITEMS ? NULL : rvm_mi_cache_get_paddr(pc, flags, pap);
	if (item != NULL) {
		outstanding[i].item = item;
		outstanding[i].cache = pc;
	}
	in_operation = 0;
	return item;
}

void
pool_cache_put_paddr(pool_cache_t pc, void *item, paddr_t pa)
{
	unsigned int i;
	enter(pc);
	if (item == NULL || pa != POOL_PADDR_INVALID)
		panic(rvm_cache_bad);
	for (i = 0; i < RVM_BOOTCACHE_ITEMS; i++) {
		if (outstanding[i].item == item && outstanding[i].cache == pc)
			break;
	}
	if (i == RVM_BOOTCACHE_ITEMS)
		panic(rvm_cache_bad);
	rvm_mi_cache_put_paddr(pc, item, pa);
	outstanding[i].item = NULL;
	outstanding[i].cache = NULL;
	in_operation = 0;
}

void
pool_cache_invalidate(pool_cache_t pc)
{
	enter(pc);
	rvm_mi_cache_invalidate(pc);
	in_operation = 0;
}

int
rvm_bootcache_destroy(pool_cache_t pc)
{
	unsigned int i;
	if (!available() || !valid(pc))
		return 0;
	enter(pc);
	for (i = 0; i < RVM_BOOTCACHE_ITEMS; i++) {
		if (outstanding[i].cache == pc) {
			in_operation = 0;
			return 0;
		}
	}
	rvm_mi_cache_destroy(pc);
	for (i = 0; i < RVM_BOOTCACHE_CACHES; i++) {
		if (pc == &caches[i].cache)
			active[i] = 0;
	}
	for (i = 0; i < RVM_BOOTCACHE_CACHES; i++) {
		if (active[i])
			break;
	}
	if (i == RVM_BOOTCACHE_CACHES) {
		rvm_mi_cache_groups_fini();
		initialized = 0;
	}
	in_operation = 0;
	return 1;
}

static int
verify_ctor(void *arg, void *item, int flags)
{
	unsigned int *counts = arg;
	if (flags != PR_NOWAIT)
		return 1;
	counts[0]++;
	memset(item, 0x5a, 37);
	return 0;
}

static void
verify_dtor(void *arg, void *item __unused)
{
	unsigned int *counts = arg;
	counts[1]++;
}

int
rvm_bootcache_verify(void)
{
	pool_cache_t pc;
	unsigned char *a, *b, *again;
	unsigned int counts[2] = { 0, 0 };
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	pc = rvm_bootcache_create(37, 16, 0, verify_ctor, verify_dtor, counts);
	if (pc == NULL)
		return 0;
	a = pool_cache_get(pc, PR_NOWAIT);
	b = pool_cache_get(pc, PR_NOWAIT);
	if (a == NULL || b == NULL || a == b || a[0] != 0x5a || b[36] != 0x5a ||
	    (((uintptr_t)a | (uintptr_t)b) & 15) != 0 || counts[0] != 2 ||
	    rvm_bootcache_destroy(pc))
		return 0;
	a[0] = 0x39;
	pool_cache_put(pc, a);
	again = pool_cache_get(pc, PR_NOWAIT);
	if (again != a || again[0] != 0x39 || counts[0] != 2 || counts[1] != 0 ||
	    pc->pc_cpu0.cc_hits < 2)
		return 0;
	pool_cache_put(pc, again);
	pool_cache_put(pc, b);
	pool_cache_invalidate(pc);
	if (counts[1] != 2)
		return 0;
	a = pool_cache_get(pc, PR_NOWAIT);
	if (a == NULL || counts[0] != 3)
		return 0;
	pool_cache_put(pc, a);
	if (!rvm_bootcache_destroy(pc) || counts[1] != 3)
		return 0;
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before;
}
