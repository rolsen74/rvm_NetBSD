/* Only the constrained pool path is retained from the unchanged MI source. */
#include <sys/param.h>
#include <sys/pool.h>

/* Inline initialization so the non-NULL custom allocator eliminates default
 * allocator tables. PHINPAGE selects the self-contained header path.
 */
inline __attribute__((always_inline)) void pool_init(struct pool *, size_t,
    u_int, u_int, int, const char *, struct pool_allocator *, int);

/* The bootstrap ABI currently supports eight register arguments. */
inline __attribute__((always_inline)) void pool_cache_bootstrap(pool_cache_t,
    size_t, u_int, u_int, u_int, const char *, struct pool_allocator *, int,
    int (*)(void *, void *, int), void (*)(void *, void *), void *);

#include "../../../kern/subr_pool.c"

extern const char rvm_pool_name[];

void
rvm_mi_pool_globals(void)
{
	/* Static pointer initializers are not supported by FHR GCC yet. */
	TAILQ_INIT(&pool_head);
	TAILQ_INIT(&pool_cache_head);
	mutex_init(&pool_head_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&pool_allocator_lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&pool_busy, rvm_pool_name);
}

void
rvm_mi_pool_setup(struct pool *pool, size_t size, unsigned int alignment,
    struct pool_allocator *allocator)
{
	if (allocator == NULL)
		panic(rvm_pool_name);
	pool_init(pool, size, alignment, 0, PR_PHINPAGE, rvm_pool_name,
	    allocator, IPL_NONE);
}

/* Cache group pools use the same real MI page/item machinery, with explicit
 * NOWAIT preallocation before magazine operations raise the logical IPL.
 */
int
rvm_mi_cache_groups_init(struct pool_allocator *allocator)
{
	void *item;
	if (allocator == NULL)
		panic(rvm_pool_name);
	pcg_normal_cache = NULL;
	pcg_large_cache = NULL;
	pool_init(&pcg_normal_pool, sizeof(pcg_t) +
	    (PCG_NOBJECTS_NORMAL - 1) * sizeof(pcgpair_t), 64, 0,
	    PR_PHINPAGE, rvm_pool_name, allocator, IPL_NONE);
	pool_init(&pcg_large_pool, sizeof(pcg_t) +
	    (PCG_NOBJECTS_LARGE - 1) * sizeof(pcgpair_t), 64, 0,
	    PR_PHINPAGE, rvm_pool_name, allocator, IPL_NONE);
	item = pool_get(&pcg_normal_pool, PR_NOWAIT);
	if (item == NULL)
		goto fail;
	pool_put(&pcg_normal_pool, item);
	item = pool_get(&pcg_large_pool, PR_NOWAIT);
	if (item == NULL)
		goto fail;
	pool_put(&pcg_large_pool, item);
	return 1;
fail:
	pool_destroy(&pcg_normal_pool);
	pool_destroy(&pcg_large_pool);
	return 0;
}

int
rvm_mi_cache_group_pool(struct pool *pool)
{
	return pool == &pcg_normal_pool || pool == &pcg_large_pool;
}

void
rvm_mi_cache_groups_fini(void)
{
	pool_destroy(&pcg_normal_pool);
	pool_destroy(&pcg_large_pool);
}

void
rvm_mi_cache_setup(pool_cache_t pc, size_t size, unsigned int alignment,
    unsigned int flags, struct pool_allocator *allocator,
    int (*ctor)(void *, void *, int), void (*dtor)(void *, void *), void *arg)
{
	if (allocator == NULL)
		panic(rvm_pool_name);
	pool_cache_bootstrap(pc, size, alignment, 0, flags | PR_PHINPAGE,
	    rvm_pool_name, allocator, IPL_NONE, ctor, dtor, arg);
}
