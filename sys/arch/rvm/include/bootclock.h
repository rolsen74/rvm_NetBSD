/* Standalone MI hardclock/callout services, driven by explicit timer waits. */
#ifndef _RVM_BOOTCLOCK_H_
#define _RVM_BOOTCLOCK_H_
#include <sys/types.h>
struct bintime;
int rvm_bootclock_init(void);
int rvm_bootclock_ready(void);
int rvm_bootclock_busy(void);
void rvm_bootclock_intr(vaddr_t, uint64_t);
void rvm_bootclock_poll(void);
void rvm_bootclock_binuptime(struct bintime *);
int rvm_bootclock_verify(void);
int rvm_callout_cpu_init(void);
void rvm_callout_poll(void);
int rvm_callout_busy(void);
void rvm_bootclock_ticktock(void);
#endif
