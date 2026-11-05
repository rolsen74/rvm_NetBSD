/* Physical allocator used before UVM is initialized. */
#ifndef _RVM_BOOTMEM_H_
#define _RVM_BOOTMEM_H_

#include <machine/bootinfo.h>

struct rvm_bootmem {
	paddr_t ram_end;
	paddr_t loaded_end;
	paddr_t next;
	paddr_t limit;
	paddr_t free_start; /* page-rounded, excludes bootstrap allocations */
	paddr_t free_end;
	unsigned int sealed; /* allocation is forbidden once free RAM is handed off */
};

extern struct rvm_bootinfo rvm_bootinfo;
extern struct rvm_bootmem rvm_bootmem;

int rvm_bootmem_init(const struct rvm_bootinfo *);
paddr_t rvm_bootmem_alloc(psize_t, psize_t);
int rvm_bootmem_seal(void);

#endif /* _RVM_BOOTMEM_H_ */
