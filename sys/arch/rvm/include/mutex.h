/* Bootstrap mutex storage; only UP execution with interrupts disabled works. */
#ifndef _RVM_MUTEX_H_
#define _RVM_MUTEX_H_

#include <sys/types.h>

#ifdef __MUTEX_PRIVATE
#error "RVM full kernel mutex operations are not implemented yet"
#endif

struct kmutex {
	uintptr_t mtx_state; /* 0: free; adaptive: owner LWP; spin: 1 */
	unsigned int mtx_magic;
	int mtx_ipl;
	int mtx_oldipl;
};

#endif /* _RVM_MUTEX_H_ */
