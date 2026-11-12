/* Bootstrap mutex storage; only UP execution with interrupts disabled works. */
#ifndef _RVM_MUTEX_H_
#define _RVM_MUTEX_H_

#include <sys/types.h>

#if defined(RVM_KERNEL) || defined(_KMEMUSER)
/* MI adaptive/spin mutexes, with one-word storage in normal kernels.
 * Owner flags occupy bits 0..3, spin IPL bits 4..7, lock bit 8.
 */
struct kmutex {
	volatile uintptr_t mtx_owner;
} __aligned(8);
#if defined(RVM_KERNEL) && defined(__MUTEX_PRIVATE)
#include <sys/atomic.h>
#include <machine/intr.h>
#define __HAVE_SIMPLE_MUTEXES 1
#undef MUTEX_SPIN_IPL
#define MUTEX_SPIN_IPL(m) makeiplcookie(((m)->mtx_owner >> 4) & 15)
#define MUTEX_INITIALIZE_SPIN_IPL(m, i) ((m)->mtx_owner |= (uintptr_t)(i) << 4)
#define MUTEX_SPINBIT_LOCK_INIT(m) ((m)->mtx_owner &= ~(uintptr_t)256)
#define MUTEX_SPINBIT_LOCKED_P(m) (((m)->mtx_owner & 256) != 0)
#define MUTEX_SPINBIT_LOCK_TRY(m) rvm_mutex_spin_try(m)
#define MUTEX_SPINBIT_LOCK_UNLOCK(m) atomic_and_ulong(&(m)->mtx_owner, ~(unsigned long)256)
static inline bool
rvm_mutex_spin_try(kmutex_t *mtx)
{
	unsigned long owner = mtx->mtx_owner;
	return (owner & 256) == 0 &&
	    atomic_cas_ulong(&mtx->mtx_owner, owner, owner | 256) == owner;
}
#endif
#else
/* The standalone harness retains its checked, IRQ-guarded mutex services. */
struct kmutex {
	uintptr_t mtx_state; /* 0: free; adaptive: owner LWP; spin: 1 */
	unsigned int mtx_magic;
	int mtx_ipl;
	int mtx_oldipl;
};
#endif

#endif /* _RVM_MUTEX_H_ */
