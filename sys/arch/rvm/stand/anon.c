/* Keep the real MI descriptor constructor/allocation/free bodies. Adapt only
 * their private cache to the bounded custom-backed bootstrap cache facility.
 */
#include <sys/param.h>
#include <sys/pool.h>
#include <machine/bootcache.h>

#undef pool_cache_get_paddr
#undef pool_cache_put_paddr
void *pool_cache_get_paddr(pool_cache_t, int, paddr_t *);
void pool_cache_put_paddr(pool_cache_t, void *, paddr_t);
static void *
rvm_bootanon_cache_get_real(pool_cache_t pc, int flags, paddr_t *pa)
{
	return pool_cache_get_paddr(pc, flags, pa);
}
static void
rvm_bootanon_cache_put_real(pool_cache_t pc, void *item, paddr_t pa)
{
	pool_cache_put_paddr(pc, item, pa);
}
#define pool_cache_get_paddr rvm_anon_cache_get
#define pool_cache_put_paddr rvm_anon_cache_put

#include "../../../uvm/uvm_anon.c"

static pool_cache_t cache;
extern const char rvm_anon_bad[];

int
rvm_mi_anon_start(void)
{
	if (cache != NULL)
		panic(rvm_anon_bad);
	cache = rvm_bootcache_create(sizeof(struct vm_anon), 8, PR_LARGECACHE,
	    uvm_anon_ctor, NULL, NULL);
	return cache != NULL;
}

int
rvm_mi_anon_stop(void)
{
	if (cache == NULL || !rvm_bootcache_destroy(cache))
		return 0;
	cache = NULL;
	return 1;
}

void *
rvm_anon_cache_get(pool_cache_t pc, int flags, paddr_t *pa)
{
	if (pc != &uvm_anon_cache || cache == NULL)
		panic(rvm_anon_bad);
	return rvm_bootanon_cache_get_real(cache, flags, pa);
}

void
rvm_anon_cache_put(pool_cache_t pc, void *item, paddr_t pa)
{
	struct vm_anon *anon = item;
	if (pc != &uvm_anon_cache || cache == NULL || anon->an_lock != NULL ||
	    anon->an_ref != 0 || anon->an_page != NULL || anon->an_swslot != 0)
		panic(rvm_anon_bad);
	rvm_bootanon_cache_put_real(cache, item, pa);
}
