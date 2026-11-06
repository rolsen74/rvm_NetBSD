/* GenericDriver clocks. Single-CPU bootstrap; RTC pairs acquire an IRQ guard. */
#ifndef _RVM_CLOCK_H_
#define _RVM_CLOCK_H_
#include <sys/types.h>
struct timespec;
#define RVM_TIMECOUNTER_FREQUENCY 1000000U
uint64_t rvm_clock_monotonic_ns(void);
int rvm_rtc_read(struct timespec *);
void delay(unsigned int);
int rvm_clock_wait_until(uint64_t);
int rvm_rtc_verify(void);
int rvm_delay_verify(void);
int rvm_wallclock_verify(void);
int rvm_deadline_verify(void);
int rvm_tsleep_verify(void);
/* Returns 1 at deadline, 0 on an early wakeup, -1 for unsupported context. */
int rvm_bootsched_sleep_until(uint64_t);
#endif
