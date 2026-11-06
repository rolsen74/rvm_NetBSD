/* Single-CPU asynchronous supervisor clock and IRQ guards. */
#ifndef _RVM_ASYNCIRQ_H_
#define _RVM_ASYNCIRQ_H_
#include <sys/types.h>
#include <machine/irq.h>
struct rvm_async_irqframe {
	uint64_t inte;
	struct rvm_irqframe irq;
};
uint64_t rvm_intr_disable(void);
void rvm_intr_apply(void);
unsigned int rvm_intr_suspend(void);
void rvm_intr_resume(unsigned int);
void rvm_irq_set_sr(uint64_t);
void rvm_async_preemption(int);
int rvm_async_clock_start(void);
int rvm_async_clock_stop(void);
int rvm_async_clock_active(void);
uint64_t rvm_async_clock_mask(void);
uint64_t rvm_async_clock_events(void);
uint64_t rvm_async_clock_ticks(void);
uint64_t rvm_async_clock_catchups(void);
void rvm_async_irq_dispatch(struct rvm_async_irqframe *);
void rvm_async_irq_return(struct rvm_async_irqframe *);
int rvm_async_register_probe(void);
int rvm_async_register_verify(void);
int rvm_async_irq_verify(void);
int rvm_async_preempt_verify(void);
int rvm_async_services_verify(void);
uint64_t rvm_bootsched_preemptions(void);
void rvm_bootsched_preempt(void);
void rvm_async_idle_wait(void);
#endif
