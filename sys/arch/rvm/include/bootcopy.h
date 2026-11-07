/* Supervisor MMU-fault recovery for kernel-to-kernel kcopy. */
#ifndef _RVM_BOOTCOPY_H_
#define _RVM_BOOTCOPY_H_
#define RVM_COPY_EFAULT 14
#ifndef _LOCORE
#include <machine/trap.h>
struct rvm_copyfault_frame {
	struct rvm_exceptionframe saved;
	uint64_t virtual_address;
	uint64_t physical_address;
};
int rvm_kcopy_init(void);
int rvm_kcopy_ready(void);
int rvm_kcopy_verify(void);
void rvm_copyfault_dispatch(struct rvm_copyfault_frame *);
unsigned long rvm_kcopy_faults(void);
#endif
#endif
