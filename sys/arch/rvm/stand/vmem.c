/* Keep the MI allocator body unchanged. Force the two interfaces with more
 * than eight parameters inline into the bounded bootstrap interface: RVM
 * GCC's outgoing stack-argument ABI is not complete yet.
 */
#include <sys/param.h>
#include <sys/vmem.h>
#include <sys/vmem_impl.h>

inline __attribute__((always_inline)) vmem_t *vmem_init(vmem_t *, const char *,
    vmem_addr_t, vmem_size_t, vmem_size_t, vmem_import_t *, vmem_release_t *,
    vmem_t *, vmem_size_t, vm_flag_t, int);
inline __attribute__((always_inline)) int vmem_xalloc(vmem_t *, vmem_size_t,
    vmem_size_t, vmem_size_t, vmem_size_t, vmem_addr_t, vmem_addr_t,
    vm_flag_t, vmem_addr_t *);

#include "../../../kern/subr_vmem.c"

extern const char rvm_heap_name[];
int rvm_heap_import(vmem_t *, vmem_size_t, vmem_size_t *, vm_flag_t, vmem_addr_t *);
void rvm_heap_release(vmem_t *, vmem_addr_t, vmem_size_t);

vmem_t *
rvm_mi_heap_init(vmem_t *arena)
{
	return vmem_init(arena, rvm_heap_name, 0, 0, 8,
	    __FPTRCAST(vmem_import_t *, rvm_heap_import), rvm_heap_release,
	    NULL, 0, VM_NOSLEEP | VM_PRIVTAGS | VM_XIMPORT, IPL_NONE);
}

/* Only this private pool may reach refill, and no dynamic pool exists yet.
 * Returning NULL lets MI's NOSLEEP tag-exhaustion path return ENOMEM.
 */
void *
rvm_heap_pool_get(struct pool *pool, int flags)
{
	if (pool != &vmem_btag_pool || flags != PR_NOWAIT)
		panic(rvm_heap_name);
	return NULL;
}
