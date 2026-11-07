/* Scoped 64-bit MMIO probing; unrelated physical faults remain fatal. */
#ifndef _RVM_MMIO_H_
#define _RVM_MMIO_H_
#include <sys/types.h>
struct rvm_exceptionframe;
int rvm_mmio_peek8(const volatile uint64_t *, uint64_t *);
bool rvm_mmio_recover(struct rvm_exceptionframe *);
#endif
