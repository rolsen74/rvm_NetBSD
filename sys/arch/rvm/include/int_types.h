/*
 * NetBSD/RVM - machine integer types.
 *
 * The current RVM GCC does not provide the exact-width type macros used
 * by sys/common_int_types.h.  Describe its LP64 ABI explicitly.
 */

#ifndef _RVM_INT_TYPES_H_
#define _RVM_INT_TYPES_H_

#if !defined(_LP64)
#error "NetBSD/RVM requires an LP64 ABI"
#endif

typedef signed char __int8_t;
typedef unsigned char __uint8_t;
typedef short int __int16_t;
typedef unsigned short int __uint16_t;
typedef int __int32_t;
typedef unsigned int __uint32_t;
typedef long int __int64_t;
typedef unsigned long int __uint64_t;
typedef long int __intptr_t;
typedef unsigned long int __uintptr_t;

#define __BIT_TYPES_DEFINED__

#endif /* _RVM_INT_TYPES_H_ */
