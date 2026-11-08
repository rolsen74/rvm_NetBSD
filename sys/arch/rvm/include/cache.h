/* RVM instruction-cache and translation controls; data caching stays off. */
#ifndef _RVM_CACHE_H_
#define _RVM_CACHE_H_
#ifdef RVM_KERNEL
#include "opt_rvm.h"
#endif
#ifdef RVM_KERNEL_ICACHE
#ifdef RVM_MMU_TLB
#define RVM_KERNEL_CR 26
#else
#define RVM_KERNEL_CR 10
#endif
#else
#ifdef RVM_MMU_TLB
#define RVM_KERNEL_CR 24
#else
#define RVM_KERNEL_CR 8
#endif
#endif
#ifdef RVM_USER_ICACHE
#define RVM_USER_CACHE_MASK 2
#ifdef RVM_MMU_TLB
#define RVM_USER_CR 30
#else
#define RVM_USER_CR 14
#endif
#else
#define RVM_USER_CACHE_MASK 0
#ifdef RVM_MMU_TLB
#define RVM_USER_CR 28
#else
#define RVM_USER_CR 12
#endif
#endif
#ifndef _LOCORE
/* Global UP maintenance. A later optimization can target individual pages.
 * TLB tags include root/mode; invalidate after all mapping and R/M changes. */
void rvm_tlb_invalidate_all(void);
void rvm_icache_invalidate_all(void);
static inline void
rvm_cache_sync(void)
{
#ifdef RVM_MMU_TLB
    rvm_tlb_invalidate_all();
#endif
#if defined(RVM_KERNEL_ICACHE) || defined(RVM_USER_ICACHE)
    rvm_icache_invalidate_all();
#endif
    __asm volatile("" ::: "memory");
}
#endif
#endif
