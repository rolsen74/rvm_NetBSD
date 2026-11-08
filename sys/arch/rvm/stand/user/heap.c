/* Native break tracking for static FHR programs. Kernel obreak rounds its
 * mappings to pages while libc retains the precise byte-valued break. */
#include "rvm_user.h"
#include <sys/errno.h>
#include <sys/stdint.h>
#include <sys/syscall.h>

extern char __rvm_end;
char *__minbrk, *__curbrk;

static int
heap_init(void)
{
	if (__minbrk == 0) {
		int page = getpagesize();
		if (page <= 0) return -1;
		uintptr_t end = (uintptr_t)&__rvm_end + 1;
		__minbrk = __curbrk = (char *)((end + page - 1) & ~(uintptr_t)(page - 1));
	}
	return 0;
}
int
brk(void *address)
{
	if (heap_init() != 0) return -1;
	if ((uintptr_t)address < (uintptr_t)__minbrk) { errno = EINVAL; return -1; }
	long error = __rvm_rawcall(SYS_break, (long)address, 0, 0, 0, 0, 0, 0, 0);
	if (error < 0) { errno = -error; return -1; }
	__curbrk = address;
	return 0;
}
void *
sbrk(intptr_t increment)
{
	if (heap_init() != 0) return (void *)-1;
	uintptr_t old = (uintptr_t)__curbrk, next;
	if (increment >= 0) {
		if ((uintptr_t)increment > UINTPTR_MAX - old) { errno = ENOMEM; return (void *)-1; }
		next = old + increment;
	} else {
		uintptr_t amount = (uintptr_t)(-(increment + 1)) + 1;
		if (amount > old - (uintptr_t)__minbrk) { errno = ENOMEM; return (void *)-1; }
		next = old - amount;
	}
	if (increment != 0 && brk((void *)next) != 0) return (void *)-1;
	return (void *)old;
}

/* NetBSD libc's private names are used by its unmodified allocator. */
void *_sbrk(intptr_t);
int _brk(void *);
void *_sbrk(intptr_t n) { return sbrk(n); }
int _brk(void *p) { return brk(p); }
