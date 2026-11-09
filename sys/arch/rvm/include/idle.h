/* Normal-kernel idle/HALT interrupt-return protocol. */
#ifndef _RVM_IDLE_H_
#define _RVM_IDLE_H_
#include <sys/types.h>
void rvm_idle_halt(void);
void rvm_idle_irq_return(uint64_t *);
uint64_t rvm_idle_halt_wakeups(void);
uint64_t rvm_idle_halt_skips(void);
#endif
