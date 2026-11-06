/* Bootstrap-only MI pools with wired backing and bounded ownership records. */
#ifndef _RVM_BOOTPOOL_H_
#define _RVM_BOOTPOOL_H_
#include <sys/types.h>
struct pool;
#define RVM_BOOTPOOL_POOLS 8
#define RVM_BOOTPOOL_ITEMS 512
struct pool *rvm_bootpool_create(size_t, unsigned int);
int rvm_bootpool_destroy(struct pool *);
int rvm_bootpool_verify(void);
#endif
