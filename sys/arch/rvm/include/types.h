

/*
 * NetBSD/RVM machine-dependent types.
 *
 * Initial 64-bit RVM architecture definition.
 * Copyright (c) 2026 Rene W. Olsen.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _RVM_TYPES_H_
#define _RVM_TYPES_H_

#include <sys/cdefs.h>
#include <sys/featuretest.h>
#include <machine/int_types.h>

#ifndef RVM_KERNEL
#define __HAVE_CPU_UAREA_ROUTINES 1
#else
#define __HAVE_SYSCALL_INTERN 1
#endif
#define __HAVE_VM_PAGE_MD 1
#define __HAVE_MM_MD_KERNACC 1
#define __HAVE_RAS
#ifndef _KERNEL
#define __HAVE_ATOMIC64_OPS
#endif

/* RVM's LP64 ABI aligns long/pointer objects to four bytes. Uncached
 * scalar accesses are single instructions even at this ABI alignment.
 * Keep MI alignment checks at the type's native requirement; the MD atomic
 * gate serializes read-modify-write operations across CPUs in SMP kernels.
 */
#ifdef _KERNEL
#define __ATOMIC_ALIGNMENT(p) __alignof__(*(p))
#endif

/*
 * RVM currently targets a 64-bit LP64 ABI.
 * Do not silently compile this header for a 32-bit ABI.
 */
#if ! defined( _LP64 )
#error "NetBSD/RVM currently requires an LP64 ABI"
#endif

#if defined( _KERNEL ) || defined( _KMEMUSER ) || defined( _KERNTYPES ) || defined( _STANDALONE )

typedef __uint64_t paddr_t;
typedef __uint64_t psize_t;
typedef __uint64_t vaddr_t;
typedef __uint64_t vsize_t;

#define PRIxPADDR PRIx64
#define PRIxPSIZE PRIx64
#define PRIuPSIZE PRIu64
#define PRIxVADDR PRIx64
#define PRIxVSIZE PRIx64
#define PRIuVSIZE PRIu64

typedef __int64_t  register_t;
typedef __uint64_t uregister_t;
typedef __int32_t  register32_t;
typedef __uint32_t uregister32_t;

#define PRIxREGISTER   PRIx64
#define PRIxREGISTER32 PRIx32

#endif /* kernel / standalone types */

/* Internal register-sized type used by common NetBSD headers. */
typedef __int64_t __register_t;

/* Basic simple-lock representation; actual atomic operations come later. */
typedef unsigned int __cpu_simple_lock_nv_t;
#define __SIMPLELOCK_LOCKED	  1
#define __SIMPLELOCK_UNLOCKED 0

/* Conservative integer save area; setjmp/longjmp are not implemented yet. */
typedef struct {
	__uint64_t val[66]; /* R0-R63, PC, SR */
} label_t;

/*
 * TODO: Enable __HAVE_* feature flags only after implementing their
 * required machine-dependent operations (atomics, TLS, CPU counters,
 * pmap optimizations, etc.).
 */

#endif /* _RVM_TYPES_H_ */
