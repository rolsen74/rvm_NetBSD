/* Standalone cooperative MI scheduling; hardware preemption remains pending. */
#ifndef _RVM_BOOTSCHED_H_
#define _RVM_BOOTSCHED_H_
#include <sys/types.h>
struct lwp;
struct kmutex;
int rvm_bootsched_init(void);
int rvm_bootsched_fini(void);
void rvm_bootsched_switch(struct lwp *);
void rvm_bootsched_exit(struct lwp *) __dead;
int rvm_bootsched_reap(struct lwp *);
int rvm_bootsched_verify(void);
int rvm_bootsched_ready(struct lwp *);
void rvm_bootsched_yield(void);
void rvm_bootsched_finish(void) __dead;
int rvm_bootsched_runq_verify(void);
int rvm_bootsched_runtime_verify(void);
int rvm_bootsched_idle_init(void (*)(void *), void *);
void rvm_bootsched_sleep(void);
int rvm_bootsched_idle_verify(void);
void rvm_bootsched_idle_lock(struct lwp *);
void rvm_bootsched_idle_unlock(struct lwp *);
void rvm_bootsched_idle_switch(struct lwp *);
struct lwp *rvm_bootsched_pick(void);
void rvm_bootsched_lwp_unlock(struct lwp *);
/* Services used only by the selected MI switch compilation profile. */
void rvm_bootsched_unavailable(void) __dead;
void rvm_bootsched_unlock(struct kmutex *);
void rvm_bootsched_vmcheck(struct lwp *);
#endif
