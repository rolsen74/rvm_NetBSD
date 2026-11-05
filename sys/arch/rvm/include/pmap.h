/*
 * RVM supervisor pmap. User pmaps and reverse-mapping tracking are pending.
 * Caches and interrupts must stay off.
 */
#ifndef _RVM_PMAP_H_
#define _RVM_PMAP_H_

#define PMAP_STEAL_MEMORY
#include <sys/types.h>
#include <uvm/uvm_pmap.h>

/* Also declare it for direct machine/pmap.h users (the MI include is guarded). */
vaddr_t pmap_steal_memory(vsize_t, vaddr_t *, vaddr_t *);

struct pmap {
	paddr_t pm_root;
	struct pmap_statistics pm_stats;
};

#define RVM_BOOT_TEST_VA 0xffff800000000000UL

struct rvm_boot_pmap {
	paddr_t root;
	paddr_t test_page;
	unsigned long tables;
};

extern struct rvm_boot_pmap rvm_boot_pmap;
int pmap_bootstrap(void);
int rvm_boot_pmap_lookup(vaddr_t, paddr_t *, unsigned long *);
void rvm_boot_enable_mmu(paddr_t);
uint64_t rvm_boot_get_cr(void);

#endif /* _RVM_PMAP_H_ */
