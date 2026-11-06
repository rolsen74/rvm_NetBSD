/* Eager, resident anonymous mappings in the bootstrap kernel VA map. */
#ifndef _RVM_BOOTANONMAP_H_
#define _RVM_BOOTANONMAP_H_
#include <sys/types.h>
#define RVM_BOOTANONMAP_MAPPINGS 16
vaddr_t rvm_bootanonmap_alloc(vsize_t);
vaddr_t rvm_bootanonmap_share(vaddr_t);
int rvm_bootanonmap_free(vaddr_t, vsize_t);
unsigned int rvm_bootanonmap_live(void);
int rvm_bootanonmap_verify(void);
#endif
