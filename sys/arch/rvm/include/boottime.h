/* MI timecounter bootstrap; counter measures serviced hardclock ticks. */
#ifndef _RVM_BOOTTIME_H_
#define _RVM_BOOTTIME_H_
int rvm_timecounter_init(void);
int rvm_timecounter_ready(void);
void rvm_timecounter_tick(void);
int rvm_timecounter_verify(void);
#endif
