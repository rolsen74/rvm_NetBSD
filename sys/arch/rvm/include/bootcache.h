/* Bootstrap-only UP/NOWAIT front end for NetBSD's MI pool caches. */
#ifndef _RVM_BOOTCACHE_H_
#define _RVM_BOOTCACHE_H_
#include <sys/pool.h>
#define RVM_BOOTCACHE_CACHES 4
#define RVM_BOOTCACHE_ITEMS 512
pool_cache_t rvm_bootcache_create(size_t, unsigned int, unsigned int,
    int (*)(void *, void *, int), void (*)(void *, void *), void *);
int rvm_bootcache_destroy(pool_cache_t);
int rvm_bootcache_verify(void);
#endif
