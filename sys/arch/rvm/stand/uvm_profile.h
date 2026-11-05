/*
 * Standalone link profile for unmodified MI sources. -fwhole-program drops
 * entry points whose kernel dependencies are not available yet. These exports
 * retain real page initialization, raw-page allocation/free, fixed extents
 * and clock-policy initialization. Full kernel builds do not use this header.
 */
#if defined(RVM_BOOT_PAGE)
/* Public bootstrap entry points validate raw-page-only requests first. */
#define uvm_pagealloc_strat rvm_mi_pagealloc_strat
#define uvm_pagefree rvm_mi_pagefree
#endif
#include <uvm/uvm.h>
#define RVM_BOOT_EXPORT __attribute__((externally_visible))

#if defined(RVM_BOOT_PAGE)
void uvm_page_init(vaddr_t *, vaddr_t *) RVM_BOOT_EXPORT;
void uvm_setpagesize(void) RVM_BOOT_EXPORT;
vaddr_t uvm_pageboot_alloc(vsize_t) RVM_BOOT_EXPORT;
void uvm_pagefree(struct vm_page *) RVM_BOOT_EXPORT;
struct vm_page *uvm_pagealloc_strat(struct uvm_object *, voff_t,
    struct vm_anon *, int, int, int) RVM_BOOT_EXPORT;
int uvm_page_lookup_freelist(struct vm_page *) RVM_BOOT_EXPORT;
paddr_t uvm_vm_page_to_phys(const struct vm_page *) RVM_BOOT_EXPORT;
extern psize_t physmem RVM_BOOT_EXPORT;
extern union uvm_freelist_lock uvm_freelist_locks[] RVM_BOOT_EXPORT;

/* Object pagers are not linked in this raw-page-only bootstrap. */
void rvm_uvm_object_unavailable(void) __dead;
#undef UVM_OBJ_IS_VNODE
#undef UVM_OBJ_IS_AOBJ
#define UVM_OBJ_IS_VNODE(obj) (rvm_uvm_object_unavailable(), false)
#define UVM_OBJ_IS_AOBJ(obj) (rvm_uvm_object_unavailable(), false)
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
