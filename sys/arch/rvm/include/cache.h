/* Experimental instruction caching of the immutable kernel text only. */
#ifndef _RVM_CACHE_H_
#define _RVM_CACHE_H_
#ifdef RVM_KERNEL
#include "opt_rvm.h"
#endif
#ifdef RVM_KERNEL_ICACHE
#define RVM_KERNEL_CR 10
#else
#define RVM_KERNEL_CR 8
#endif
#endif
