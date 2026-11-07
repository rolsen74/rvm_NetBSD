/* Checked UP generic software interrupts for the standalone kernel. */
#ifndef _RVM_BOOTSOFTINT_H_
#define _RVM_BOOTSOFTINT_H_
#include <sys/types.h>
struct lwp;
int rvm_softint_init(void);
int rvm_softint_fini(void);
int rvm_softint_ready(void);
int rvm_softint_member(struct lwp *);
int rvm_softint_pending(void);
int rvm_softint_verify(void);
int rvm_softclock_verify(void);
void rvm_softint_lwp_lock(struct lwp *);
void rvm_softint_switch(struct lwp *);
#endif
