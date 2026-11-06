/* Resident bootstrap heap using MI vmem, with bounded metadata. */
#ifndef _RVM_BOOTHEAP_H_
#define _RVM_BOOTHEAP_H_
#include <sys/types.h>
#define RVM_BOOTHEAP_TAGS 200
#define RVM_BOOTHEAP_RECORDS 256
int rvm_bootheap_init(void);
int rvm_bootheap_ready(void);
int rvm_bootheap_verify(void);
unsigned int rvm_bootheap_live(void);
vsize_t rvm_bootheap_size(int);
#endif
