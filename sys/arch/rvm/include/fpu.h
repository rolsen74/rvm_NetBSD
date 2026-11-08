/* RVM v0000 binary64 FPU state, including raw NaN payloads and status. */
#ifndef _RVM_FPU_H_
#define _RVM_FPU_H_
#define RVM_NFPREG 64
#define RVM_FPCR_MASK 0x3
#define RVM_FPSR_EXCEPT_MASK 0x1f
#define RVM_FPSR_COMPARE_MASK 0xf00
#define RVM_FPSR_MASK 0xf1f
#define RVM_FPREG_CR 512
#define RVM_FPREG_SR 520
#define RVM_FPREG_SIZE 528
#ifndef _LOCORE
#include <sys/types.h>
struct fpreg {
    uint64_t f_regs[RVM_NFPREG];
    uint64_t f_fpcr;
    uint64_t f_fpsr;
};
void rvm_fpu_save(struct fpreg *);
void rvm_fpu_restore(const struct fpreg *);
#endif
#endif
