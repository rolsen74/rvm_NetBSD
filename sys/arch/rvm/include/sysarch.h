#ifndef _RVM_SYSARCH_H_
#define _RVM_SYSARCH_H_
#include <sys/types.h>
#define RVM_SYNC_ICACHE 1
struct rvm_sync_icache_args {
	uint64_t address;
	uint64_t length;
};
#endif
