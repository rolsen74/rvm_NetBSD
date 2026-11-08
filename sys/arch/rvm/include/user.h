/* Native LP64 RVM syscall ABI and user transition interfaces. */
#ifndef _RVM_USER_H_
#define _RVM_USER_H_
#define RVM_SYSCALL_SIZE 2
#define RVM_TF_USP 504
#define RVM_TF_PC 520
#define RVM_TF_ROOT 528
#define RVM_TF_SR 552
#define RVM_TF_FP 568
#define RVM_TF_SIZE 1096
#ifndef _LOCORE
#include <machine/trap.h>
struct trapframe;
struct lwp;
void rvm_user_vectors_init(void);
void rvm_user_exception(struct rvm_exceptionframe *);
void rvm_user_irq_return(struct rvm_exceptionframe *);
void rvm_user_resume(struct trapframe *) __dead;
void rvm_user_return(struct lwp *);
void rvm_syscall(struct trapframe *);
int rvm_console_stdio(void);
unsigned long rvm_user_interrupts(void);
unsigned long rvm_user_restarts(void);
#endif
#endif
