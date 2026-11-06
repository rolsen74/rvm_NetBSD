/* Timer IRQ bring-up and explicit waits; ordinary kernel execution keeps IRQs off. */
#ifndef _RVM_IRQ_H_
#define _RVM_IRQ_H_
#define RVM_TIMER_IRQ 60
#define RVM_TIMER2_IRQ 40
#define RVM_TIMER2_VECTOR (32 + RVM_TIMER2_IRQ)
#define RVM_CLOCK_HZ 50
#define RVM_TIMER_VECTOR (32 + RVM_TIMER_IRQ)
#ifndef _LOCORE
#include <sys/types.h>
#include <machine/trap.h>
struct rvm_irqframe {
	uint64_t cr;
	struct rvm_exceptionframe exception;
};
int rvm_timer_irq_verify(void);
int rvm_clock_start(void);
int rvm_clock_wait(void);
int rvm_clock_stop(void);
int rvm_clock_active(void);
uint64_t rvm_clock_ticks(void);
uint64_t rvm_clock_events(unsigned int);
int rvm_clock_verify(void);
int rvm_clock_probe(void);
void rvm_timer_irq_dispatch(struct rvm_irqframe *);
int rvm_timer_irq_probe(void);
unsigned int rvm_timer_irq_halted(void);
unsigned int rvm_timer_irq_early(void);
uint64_t rvm_irq_get_inte(void);
void rvm_irq_set_inte(uint64_t);
void rvm_irq_set_cr(uint64_t);
#endif
#endif
