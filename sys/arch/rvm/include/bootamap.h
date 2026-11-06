/* Bounded resident MI anonymous maps for the UP/NOWAIT bootstrap. */
#ifndef _RVM_BOOTAMAP_H_
#define _RVM_BOOTAMAP_H_
#include <sys/types.h>
#include <sys/rwlock.h>
struct vm_amap;
struct vm_anon;
struct vm_page;
#define RVM_BOOTAMAP_MAPS 16
#define RVM_BOOTAMAP_SLOTS 256
int rvm_bootamap_init(void);
int rvm_bootamap_fini(void);
struct vm_amap *rvm_bootamap_create(vsize_t);
int rvm_bootamap_put(struct vm_amap *, vsize_t, struct vm_anon *);
struct vm_anon *rvm_bootamap_lookup(struct vm_amap *, vsize_t);
struct vm_anon *rvm_bootamap_take(struct vm_amap *, vsize_t);
struct vm_page *rvm_bootamap_page(struct vm_amap *, vsize_t);
int rvm_bootamap_hold(struct vm_amap *);
int rvm_bootamap_drop(struct vm_amap *);
krwlock_t *rvm_bootamap_owner_lock(struct vm_amap *);
int rvm_bootamap_releasing(struct vm_amap *);
int rvm_bootamap_page_freeable(struct vm_amap *);
int rvm_bootamap_entry_pin(struct vm_amap *, vsize_t);
int rvm_bootamap_entry_drop(struct vm_amap *);
int rvm_bootamap_verify(void);
#endif
