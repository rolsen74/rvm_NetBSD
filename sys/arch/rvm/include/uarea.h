/* Guarded system-LWP u-areas. User u-areas still use the MI fallback. */
#ifndef _RVM_UAREA_H_
#define _RVM_UAREA_H_
#include <sys/types.h>
#define RVM_UAREA_SLOTS 16
struct lwp;
int rvm_uarea_bind(struct lwp *, vaddr_t);
int rvm_uarea_unbind(struct lwp *);
int rvm_uarea_verify(void);
int rvm_lwpvm_verify(void);
#endif
