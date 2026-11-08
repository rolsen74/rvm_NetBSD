/* exec must reset every FP register and both FP control registers. */
#include <machine/fpu.h>
#include <stdio.h>
int main(void) {
    struct fpreg fp;
    rvm_fpu_save(&fp);
    for (unsigned i = 0; i < RVM_NFPREG; i++) if (fp.f_regs[i]) return 1;
    if (fp.f_fpcr || fp.f_fpsr) return 2;
    return 0;
}
