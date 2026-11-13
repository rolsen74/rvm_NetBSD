/* Internal reverse-map representation, shared by the two pmap backends. */
#ifndef _RVM_PMAP_PRIVATE_H_
#define _RVM_PMAP_PRIVATE_H_
struct rvm_pv_entry {
	struct rvm_pv_entry *next, *previous;
	struct vm_page *page;
	unsigned int wired, reserved;
#ifdef RVM_KERNEL
	pmap_t pmap; /* NULL for the preallocated kernel PV slots. */
	vaddr_t va;
	struct rvm_pv_entry *pm_next;
#endif
};
#ifdef RVM_KERNEL
unsigned int rvm_pmap_guard(void);
void rvm_pmap_unguard(unsigned int);
volatile uint64_t *rvm_user_pte(pmap_t, vaddr_t);
int rvm_user_pmap_prepare(pmap_t);
int rvm_user_enter(pmap_t, vaddr_t, paddr_t, vm_prot_t, u_int);
void rvm_user_remove(pmap_t, vaddr_t, vaddr_t);
void rvm_user_protect(pmap_t, vaddr_t, vaddr_t, vm_prot_t);
void rvm_user_unwire(pmap_t, vaddr_t);
void rvm_set_user_root(paddr_t);
struct vmspace;
int rvm_copyuser(struct vmspace *, vaddr_t, void *, size_t, bool, bool, size_t *);
#endif
#endif
