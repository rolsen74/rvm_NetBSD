/* NetBSD/RVM portable byte swaps until native routines are available. */
#ifndef _RVM_BSWAP_H_
#define _RVM_BSWAP_H_

#define __BSWAP_RENAME
#define __HAVE_SLOW_BSWAP_BUILTIN
#include <sys/bswap.h>

#endif /* _RVM_BSWAP_H_ */
