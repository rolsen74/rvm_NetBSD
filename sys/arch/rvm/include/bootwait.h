/* Selected MI sleep queues and non-interruptible waits for system LWPs. */
#ifndef _RVM_BOOTWAIT_H_
#define _RVM_BOOTWAIT_H_
#include <sys/types.h>
struct lwp;
struct kmutex;
int rvm_wait_init(void);
int rvm_wait_ready(void);
int rvm_wait_lock_valid(const struct kmutex *);
int rvm_wait_blocking(void);
void rvm_wait_lwp_fini(struct lwp *);
void rvm_wait_lwp_lock(struct lwp *);
void rvm_wait_lwp_unlock(struct lwp *);
void rvm_wait_lwp_unlock_to(struct lwp *, struct kmutex *);
void rvm_wait_switch(struct lwp *);
int rvm_wait_scheduler_ready(void);
void rvm_wait_resched(struct lwp *, unsigned int);
int rvm_cv_verify(void);
int rvm_cv_timeout_verify(void);
int rvm_kpause_verify(void);
int rvm_wchan_verify(void);
int rvm_cv_bintime_verify(void);
#endif
