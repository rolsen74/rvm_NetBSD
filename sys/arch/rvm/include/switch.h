/* IRQ-off, shared-kernel-pmap context switch ABI. */
#ifndef _RVM_SWITCH_H_
#define _RVM_SWITCH_H_
#define RVM_SWITCH_FIRST 10
#define RVM_SWITCH_LAST 62
#define RVM_SWITCH_NREG 53
#define RVM_SWITCH_PC 424
#define RVM_SWITCH_SIZE 432
#define RVM_PCB_SP 504
#define RVM_PCB_SSP 520
#define RVM_PCB_PC 528
#ifndef _LOCORE
#include <sys/types.h>
struct lwp;
int rvm_cpu_bootstrap_lwp(void);
int rvm_cpu_lwp_prepare(struct lwp *, vaddr_t, vsize_t, void (*)(void *), void *);
int rvm_cpu_lwp_discard(struct lwp *);
int rvm_cpu_switch_verify(void);
#endif
#endif
