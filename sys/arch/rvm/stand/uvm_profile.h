/*
 * Standalone link profile for unmodified MI sources. -fwhole-program drops
 * entry points whose kernel dependencies are not available yet. These exports
 * retain real page initialization, raw-page allocation/free, VA maps, fixed
 * extents and clock-policy initialization. Full kernels do not use this header.
 */
#if defined(RVM_BOOT_PAGE)
/* Public entry points validate raw/resident page requests first. */
#define uvm_pagealloc_strat rvm_mi_pagealloc_strat
#define uvm_pagefree rvm_mi_pagefree
#endif
#if defined(RVM_BOOT_MAP)
#define uvm_map rvm_mi_map
#endif
#if defined(RVM_BOOT_VMEM)
#define vmem_alloc rvm_mi_heap_alloc
#define vmem_free rvm_mi_heap_free
#endif
#if defined(RVM_BOOT_POOL)
#define pool_get rvm_mi_pool_get
#define pool_put rvm_mi_pool_put
#define pool_destroy rvm_mi_pool_destroy
#endif
#if defined(RVM_BOOT_KM)
#define uvm_km_alloc rvm_mi_km_alloc
#define uvm_km_free rvm_mi_km_free
#define uvm_map rvm_bootkm_map
/* Only the allocation/free path is retained; arena sizing is not run. */
#define NKMEMPAGES_MIN 1
#define NKMEMPAGES_MAX 256
#endif
#include <uvm/uvm.h>
#define RVM_BOOT_EXPORT __attribute__((externally_visible))

#if defined(RVM_BOOT_POOL)
#include <sys/pool.h>
#include <sys/sysctl.h>
/* These lists are initialized by rvm_mi_pool_globals(), avoiding FHR's
 * unsupported static pointer-to-field initializers. Cache code is dropped.
 */
#undef TAILQ_HEAD_INITIALIZER
#define TAILQ_HEAD_INITIALIZER(head) { NULL, NULL }
#undef SYSCTL_SETUP
#define SYSCTL_SETUP(name, desc) static void __unused name(struct sysctllog **clog)
void rvm_mi_pool_globals(void) RVM_BOOT_EXPORT;
void rvm_mi_pool_setup(struct pool *, size_t, unsigned int,
    struct pool_allocator *) RVM_BOOT_EXPORT;
void *pool_get(struct pool *, int) RVM_BOOT_EXPORT;
void pool_put(struct pool *, void *) RVM_BOOT_EXPORT;
void pool_destroy(struct pool *) RVM_BOOT_EXPORT;
#endif

#if defined(RVM_BOOT_VMEM)
#include <sys/vmem.h>
#include <sys/vmem_impl.h>
/* Keep the public structure layout, but omit unavailable quantum caches. */
#undef QCACHE
#include <sys/evcnt.h>
#undef EVCNT_INITIALIZER
#define EVCNT_INITIALIZER(type, parent, group, name) { 0 }
#undef EVCNT_ATTACH_STATIC
#define EVCNT_ATTACH_STATIC(name)
void *rvm_heap_pool_get(struct pool *, int);
#define pool_get rvm_heap_pool_get
vmem_t *rvm_mi_heap_init(vmem_t *) RVM_BOOT_EXPORT;
int vmem_alloc(vmem_t *, vmem_size_t, vm_flag_t, vmem_addr_t *) RVM_BOOT_EXPORT;
void vmem_free(vmem_t *, vmem_addr_t, vmem_size_t) RVM_BOOT_EXPORT;
void vmem_add_bts(vmem_t *, struct vmem_btag *, unsigned int) RVM_BOOT_EXPORT;
vmem_size_t vmem_size(vmem_t *, int) RVM_BOOT_EXPORT;
#endif

#if defined(RVM_BOOT_KM)
vaddr_t uvm_km_alloc(struct vm_map *, vsize_t, vsize_t, uvm_flag_t)
    RVM_BOOT_EXPORT;
void uvm_km_free(struct vm_map *, vaddr_t, vsize_t, uvm_flag_t)
    RVM_BOOT_EXPORT;
void uvm_km_pgremove_intrsafe(struct vm_map *, vaddr_t, vaddr_t)
    RVM_BOOT_EXPORT;
extern struct vm_map *kernel_map RVM_BOOT_EXPORT;
#endif

#if defined(RVM_BOOT_MAP)
/* A bounded entry store replaces only the unavailable pool-cache backend. */
#include <sys/pool.h>
void *rvm_map_entry_get(pool_cache_t, int, paddr_t *);
void rvm_map_entry_put(pool_cache_t, void *, paddr_t);
pool_cache_t rvm_map_cache_address(void) RVM_BOOT_EXPORT;
#define pool_cache_get_paddr rvm_map_entry_get
#define pool_cache_put_paddr rvm_map_entry_put
void uvm_map_init(void) RVM_BOOT_EXPORT;
void uvm_map_setup(struct vm_map *, vaddr_t, vaddr_t, int) RVM_BOOT_EXPORT;
int uvm_map(struct vm_map *, vaddr_t *, vsize_t, struct uvm_object *,
    voff_t, vsize_t, uvm_flag_t) RVM_BOOT_EXPORT;
void uvm_unmap_remove(struct vm_map *, vaddr_t, vaddr_t,
    struct vm_map_entry **, int) RVM_BOOT_EXPORT;
void uvm_unmap_detach(struct vm_map_entry *, int) RVM_BOOT_EXPORT;
bool uvm_map_lookup_entry(struct vm_map *, vaddr_t, struct vm_map_entry **)
    RVM_BOOT_EXPORT;
void vm_map_lock(struct vm_map *) RVM_BOOT_EXPORT;
void vm_map_unlock(struct vm_map *) RVM_BOOT_EXPORT;
void vm_map_lock_read(struct vm_map *) RVM_BOOT_EXPORT;
void vm_map_unlock_read(struct vm_map *) RVM_BOOT_EXPORT;
void uvm_unmap1(struct vm_map *, vaddr_t, vaddr_t, int) RVM_BOOT_EXPORT;
#include <sys/sysctl.h>
#undef SYSCTL_SETUP
#define SYSCTL_SETUP(name, desc) static void __unused name(struct sysctllog **clog)
#endif
#if defined(RVM_BOOT_PAGE)
void uvm_page_init(vaddr_t *, vaddr_t *) RVM_BOOT_EXPORT;
void uvm_setpagesize(void) RVM_BOOT_EXPORT;
vaddr_t uvm_pageboot_alloc(vsize_t) RVM_BOOT_EXPORT;
void uvm_pagefree(struct vm_page *) RVM_BOOT_EXPORT;
struct vm_page *uvm_pagealloc_strat(struct uvm_object *, voff_t,
    struct vm_anon *, int, int, int) RVM_BOOT_EXPORT;
int uvm_page_lookup_freelist(struct vm_page *) RVM_BOOT_EXPORT;
struct vm_page *uvm_pagelookup(struct uvm_object *, voff_t) RVM_BOOT_EXPORT;
void uvm_pagewire(struct vm_page *) RVM_BOOT_EXPORT;
void uvm_pagelock(struct vm_page *) RVM_BOOT_EXPORT;
void uvm_pageunlock(struct vm_page *) RVM_BOOT_EXPORT;
paddr_t uvm_vm_page_to_phys(const struct vm_page *) RVM_BOOT_EXPORT;
extern psize_t physmem RVM_BOOT_EXPORT;
extern union uvm_freelist_lock uvm_freelist_locks[] RVM_BOOT_EXPORT;

/* Only validated resident objects are supported; no vnode/aobj pager. */
#include <machine/bootobject.h>
#undef UVM_OBJ_IS_VNODE
#undef UVM_OBJ_IS_AOBJ
#define UVM_OBJ_IS_VNODE(obj) (rvm_bootobj_assert(obj), false)
#define UVM_OBJ_IS_AOBJ(obj) (rvm_bootobj_assert(obj), false)
#endif

#if defined(RVM_BOOT_OBJECT)
void uvm_obj_init(struct uvm_object *, const struct uvm_pagerops *, bool, u_int)
    RVM_BOOT_EXPORT;
void uvm_obj_destroy(struct uvm_object *, bool) RVM_BOOT_EXPORT;
void uvm_obj_setlock(struct uvm_object *, krwlock_t *) RVM_BOOT_EXPORT;
bool uvm_obj_clean_p(struct uvm_object *) RVM_BOOT_EXPORT;
bool uvm_obj_nowriteback_p(struct uvm_object *) RVM_BOOT_EXPORT;
bool uvm_obj_page_dirty_p(struct vm_page *) RVM_BOOT_EXPORT;
void uvm_obj_page_set_dirty(struct vm_page *) RVM_BOOT_EXPORT;
void uvm_obj_page_clear_dirty(struct vm_page *) RVM_BOOT_EXPORT;
#endif

#if defined(RVM_BOOT_STATUS)
unsigned int uvm_pagegetdirty(struct vm_page *) RVM_BOOT_EXPORT;
void uvm_pagemarkdirty(struct vm_page *, unsigned int) RVM_BOOT_EXPORT;
#endif

#if defined(RVM_BOOT_RADIX)
#include <sys/kmem.h>
void *rvm_bootradix_alloc(size_t, km_flag_t);
void rvm_bootradix_free(void *, size_t);
#define kmem_intr_alloc rvm_bootradix_alloc
#define kmem_intr_free rvm_bootradix_free
void radix_tree_init_tree(struct radix_tree *) RVM_BOOT_EXPORT;
void radix_tree_fini_tree(struct radix_tree *) RVM_BOOT_EXPORT;
bool radix_tree_empty_tree_p(struct radix_tree *) RVM_BOOT_EXPORT;
int radix_tree_insert_node(struct radix_tree *, uint64_t, void *) RVM_BOOT_EXPORT;
void *radix_tree_remove_node(struct radix_tree *, uint64_t) RVM_BOOT_EXPORT;
void *radix_tree_lookup_node(struct radix_tree *, uint64_t) RVM_BOOT_EXPORT;
radix_tree_tagmask_t radix_tree_get_tag(struct radix_tree *, uint64_t,
    radix_tree_tagmask_t) RVM_BOOT_EXPORT;
void radix_tree_set_tag(struct radix_tree *, uint64_t, radix_tree_tagmask_t)
    RVM_BOOT_EXPORT;
void radix_tree_clear_tag(struct radix_tree *, uint64_t, radix_tree_tagmask_t)
    RVM_BOOT_EXPORT;
bool radix_tree_empty_tagged_tree_p(struct radix_tree *, radix_tree_tagmask_t)
    RVM_BOOT_EXPORT;
#endif

#if defined(RVM_BOOT_CLOCK)
#include <uvm/uvm_pdpolicy.h>
void uvmpdpol_init(void) RVM_BOOT_EXPORT;
void uvmpdpol_pagedequeue(struct vm_page *) RVM_BOOT_EXPORT;
bool uvmpdpol_pageisqueued_p(struct vm_page *) RVM_BOOT_EXPORT;
void uvmpdpol_pagerealize(struct vm_page *) RVM_BOOT_EXPORT;

/* Event registration/link sets are not present; retain plain counter storage. */
#include <uvm/uvm_pdpolicy_impl.h>
#undef PDPOL_EVCNT_DEFINE
#define PDPOL_EVCNT_DEFINE(name) struct evcnt pdpol_evcnt_##name;
#endif

#if defined(RVM_BOOT_EXTENT)
#include <sys/extent.h>
struct extent *extent_create(const char *, u_long, u_long, void *, size_t, int)
    RVM_BOOT_EXPORT;
int extent_alloc(struct extent *, u_long, u_long, u_long, int, u_long *)
    RVM_BOOT_EXPORT;
int extent_free(struct extent *, u_long, u_long, int) RVM_BOOT_EXPORT;
#endif

#if defined(RVM_BOOT_METER)
#include <sys/sysctl.h>
void uvm_pctparam_init(struct uvm_pctparam *, int,
    int (*)(struct uvm_pctparam *, int)) RVM_BOOT_EXPORT;
int uvm_pctparam_get(struct uvm_pctparam *) RVM_BOOT_EXPORT;
/* No sysctl registration until kernel startup and link sets are supported. */
#undef SYSCTL_SETUP
#define SYSCTL_SETUP(name, desc) static void __unused name(struct sysctllog **clog)
#endif
