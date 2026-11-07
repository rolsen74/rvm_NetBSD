/* UP normal kernels use the IRQ-serialized MD atomic implementation. */
#ifdef RVM_KERNEL
#ifndef _RVM_LOCK_H_
#define _RVM_LOCK_H_
#include <sys/atomic.h>

static inline int
__SIMPLELOCK_LOCKED_P(const __cpu_simple_lock_t *p)
{
	return *p == __SIMPLELOCK_LOCKED;
}
static inline int
__SIMPLELOCK_UNLOCKED_P(const __cpu_simple_lock_t *p)
{
	return *p == __SIMPLELOCK_UNLOCKED;
}
static inline void
__cpu_simple_lock_init(__cpu_simple_lock_t *p)
{
	*p = __SIMPLELOCK_UNLOCKED;
}
static inline void
__cpu_simple_lock_clear(__cpu_simple_lock_t *p)
{
	*p = __SIMPLELOCK_UNLOCKED;
}
static inline void
__cpu_simple_lock_set(__cpu_simple_lock_t *p)
{
	*p = __SIMPLELOCK_LOCKED;
}
static inline int
__cpu_simple_lock_try(__cpu_simple_lock_t *p)
{
	return atomic_swap_uint(p, __SIMPLELOCK_LOCKED) == __SIMPLELOCK_UNLOCKED;
}
static inline void
__cpu_simple_lock(__cpu_simple_lock_t *p)
{
	while (!__cpu_simple_lock_try(p))
		__asm volatile("" ::: "memory");
}
static inline void
__cpu_simple_unlock(__cpu_simple_lock_t *p)
{
	membar_release();
	*p = __SIMPLELOCK_UNLOCKED;
}
#endif
#else
#include <sys/common_lock.h>
#endif
