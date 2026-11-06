/* Standalone resident objects; no pager, swap, user mappings or reclamation. */
#ifndef _RVM_BOOTOBJECT_H_
#define _RVM_BOOTOBJECT_H_
#include <sys/types.h>
struct uvm_object;
struct vm_page;
#define RVM_BOOT_RADIX_NODES 128
struct uvm_object *rvm_bootobj_create(vsize_t);
int rvm_bootobj_destroy(struct uvm_object *);
struct vm_page *rvm_bootobj_alloc(struct uvm_object *, voff_t);
int rvm_bootobj_free(struct uvm_object *, voff_t);
/* Validation hooks for the constrained MI page entry points. */
int rvm_bootobj_valid(struct uvm_object *);
int rvm_bootobj_offset(struct uvm_object *, voff_t);
void rvm_bootobj_assert(struct uvm_object *);
unsigned int rvm_bootradix_used(void);
#endif
