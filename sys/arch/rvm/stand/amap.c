/* Real MI amap arrays and slot/reference algorithms with a private cache. */
#include <sys/param.h>
#include <sys/pool.h>
#include <sys/systm.h>
#include <machine/bootcache.h>

#undef pool_cache_get_paddr
#undef pool_cache_put_paddr
void *pool_cache_get_paddr(pool_cache_t, int, paddr_t *);
void pool_cache_put_paddr(pool_cache_t, void *, paddr_t);
static void *
cache_get(pool_cache_t pc, int flags, paddr_t *pa)
{
	return pool_cache_get_paddr(pc, flags, pa);
}
static void
cache_put(pool_cache_t pc, void *item, paddr_t pa)
{
	pool_cache_put_paddr(pc, item, pa);
}
#define pool_cache_get_paddr rvm_amap_cache_get
#define pool_cache_put_paddr rvm_amap_cache_put

#include "../../../uvm/uvm_amap.c"

extern const char rvm_amap_bad[];
static pool_cache_t cache;
/* Private managed locks have exactly one owner: their cached MI amap. */
static union {
	struct { krwlock_t lock; unsigned int used; } state;
	unsigned char storage[32];
} locks[32];

static unsigned int
lock_index(krwlock_t *lock)
{
	unsigned int i;
	for (i = 0; i < __arraycount(locks); i++) {
		if (lock == &locks[i].state.lock && locks[i].state.used)
			return i;
	}
	panic(rvm_amap_bad);
}

krwlock_t *
rvm_amap_lock_alloc(void)
{
	unsigned int i;
	for (i = 0; i < __arraycount(locks); i++) {
		if (!locks[i].state.used) {
			rw_init(&locks[i].state.lock);
			locks[i].state.used = 1;
			return &locks[i].state.lock;
		}
	}
	return NULL;
}

krwlock_t *rvm_amap_lock_wait(void) { panic(rvm_amap_bad); }
unsigned int rvm_amap_lock_refs(krwlock_t *lock) { (void)lock_index(lock); return 1; }

bool
rvm_amap_lock_free(krwlock_t *lock)
{
	unsigned int i = lock_index(lock);
	if (rw_lock_held(lock))
		panic(rvm_amap_bad);
	memset(lock, 0, sizeof(*lock));
	locks[i].state.used = 0;
	return true;
}

size_t
rvm_amap_roundup(size_t size)
{
	if (size > (size_t)-1 - 7)
		panic(rvm_amap_bad);
	return roundup(size, 8);
}

int
rvm_mi_amap_start(void)
{
	if (cache != NULL)
		panic(rvm_amap_bad);
	mutex_init(&amap_list_lock, MUTEX_DEFAULT, IPL_NONE);
	LIST_INIT(&amap_list);
	cache = rvm_bootcache_create(sizeof(struct vm_amap), 64, 0,
	    amap_ctor, amap_dtor, NULL);
	if (cache == NULL) {
		mutex_destroy(&amap_list_lock);
		return 0;
	}
	return 1;
}

int
rvm_mi_amap_stop(void)
{
	unsigned int i;
	if (cache == NULL || !rvm_bootcache_destroy(cache))
		return 0;
	cache = NULL;
	if (!LIST_EMPTY(&amap_list))
		panic(rvm_amap_bad);
	for (i = 0; i < __arraycount(locks); i++) {
		if (locks[i].state.used)
			panic(rvm_amap_bad);
	}
	mutex_destroy(&amap_list_lock);
	return 1;
}

void *
rvm_amap_cache_get(pool_cache_t pc, int flags, paddr_t *pa)
{
	if (pc != &uvm_amap_cache || cache == NULL)
		panic(rvm_amap_bad);
	return cache_get(cache, flags, pa);
}

void
rvm_amap_cache_put(pool_cache_t pc, void *item, paddr_t pa)
{
	if (pc != &uvm_amap_cache || cache == NULL)
		panic(rvm_amap_bad);
	cache_put(cache, item, pa);
}
