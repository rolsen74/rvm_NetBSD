/*
 * RVM supervisor pmap with managed kernel aliases and access tracking.
 * User pmaps and cache-enabled execution remain pending.
 */
#ifndef _RVM_PMAP_H_
#define _RVM_PMAP_H_

#define PMAP_STEAL_MEMORY
#include <sys/types.h>
/* Per-managed-page reverse mappings and sticky hardware access history. */
struct rvm_pv_entry;
struct vm_page_md {
	struct rvm_pv_entry *pv_head;
	unsigned long attrs;
};
#define VM_MDPAGE_INIT(pg) do { (pg)->mdpage.pv_head = NULL; (pg)->mdpage.attrs = 0; } while (0)
#include <uvm/uvm_pmap.h>

/* Also declare it for direct machine/pmap.h users (the MI include is guarded). */
vaddr_t pmap_steal_memory(vsize_t, vaddr_t *, vaddr_t *);

struct pmap {
	paddr_t pm_root;
	struct pmap_statistics pm_stats;
	unsigned int pm_refs;
};

#define RVM_BOOT_TEST_VA 0xffff800000000000UL

struct rvm_boot_pmap {
	paddr_t root;
	paddr_t test_page;
	unsigned long tables;
};

extern struct rvm_boot_pmap rvm_boot_pmap;
int pmap_bootstrap(void);
int rvm_pmap_managed_verify(void);
int rvm_boot_pmap_lookup(vaddr_t, paddr_t *, unsigned long *);
void rvm_boot_enable_mmu(paddr_t);
uint64_t rvm_boot_get_cr(void);

#endif /* _RVM_PMAP_H_ */
