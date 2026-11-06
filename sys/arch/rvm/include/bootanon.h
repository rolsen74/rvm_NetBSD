/* Resident anonymous descriptors for the IRQ-off, non-sleeping bootstrap. */
#ifndef _RVM_BOOTANON_H_
#define _RVM_BOOTANON_H_
#include <sys/types.h>
#include <uvm/uvm_prot.h>
struct vm_anon;
struct vm_page;
struct vm_amap;
#define RVM_BOOTANON_ANONS 128
int rvm_bootanon_init(void);
int rvm_bootanon_fini(void);
int rvm_bootanon_valid(struct vm_anon *);
int rvm_bootanon_canalloc(struct vm_anon *);
int rvm_bootanon_page_valid(struct vm_page *);
struct vm_page *rvm_bootanon_alloc(struct vm_anon *);
int rvm_bootanon_hold(struct vm_anon *);
int rvm_bootanon_drop(struct vm_anon *);
void rvm_bootanon_page_protect(struct vm_page *, vm_prot_t);
int rvm_bootanon_transfer(struct vm_anon *, struct vm_amap *, int);
int rvm_bootanon_verify(void);
#endif
