/* Only the constrained pool path is retained from the unchanged MI source. */
#include <sys/param.h>
#include <sys/pool.h>

/* Inline initialization so the non-NULL custom allocator eliminates default
 * allocator tables. PHINPAGE selects the self-contained header path.
 */
inline __attribute__((always_inline)) void pool_init(struct pool *, size_t,
    u_int, u_int, int, const char *, struct pool_allocator *, int);

#include "../../../kern/subr_pool.c"

extern const char rvm_pool_name[];

void
rvm_mi_pool_globals(void)
{
	/* Static pointer initializers are not supported by FHR GCC yet. */
	TAILQ_INIT(&pool_head);
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
