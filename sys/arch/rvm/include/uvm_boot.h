/* Standalone bootstrap harness for the real MI UVM physical-segment registry. */
#ifndef _RVM_UVM_BOOT_H_
#define _RVM_UVM_BOOT_H_

#include <sys/types.h>

struct rvm_uvm_boot_state {
	unsigned int registered;
	paddr_t first; /* initial PFNs, end exclusive */
	paddr_t end;
	paddr_t probe; /* physical page permanently stolen for the allocation probe */
	unsigned int initialized;
	vaddr_t vm_start;
	vaddr_t vm_end;
	psize_t managed_pages;
};

extern struct rvm_uvm_boot_state rvm_uvm_boot;
int rvm_uvm_bootstrap(void);
int rvm_uvm_pageinit(void);
void rvm_uvm_cpuinit(void);

#endif /* _RVM_UVM_BOOT_H_ */
