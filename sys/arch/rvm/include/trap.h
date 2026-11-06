/* Supervisor software-trap bring-up. This is not the NetBSD syscall ABI. */
#ifndef _RVM_TRAP_H_
#define _RVM_TRAP_H_
#define RVM_XF_HEADER 504
#define RVM_XF_PC 512
#define RVM_XF_ROOT 520
#define RVM_XF_SIZE 528
#define RVM_SVC_PROBE 0x52564d5356430001UL
#define RVM_SVC_RESULT 0x52564d5254450000UL
#ifndef _LOCORE
#include <sys/types.h>
struct rvm_exceptionframe {
	uint64_t r[63];
	uint64_t header;
	uint64_t pc;
	uint64_t root;
};
int rvm_trap_init(void);
void rvm_supervisor_dispatch(struct rvm_exceptionframe *);
int rvm_svc_probe(void);
unsigned long rvm_trap_count(void);
#endif
#endif
