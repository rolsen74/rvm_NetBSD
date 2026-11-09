/* FP control transfers use integer argument/result registers. */
#include <machine/asm.h>
ENTRY(__rvm_libc_fpcr_read)
        mfctrl FPCR,r0
        rts
ENTRY(__rvm_libc_fpsr_read)
        mfctrl FPSR,r0
        rts
ENTRY(__rvm_libc_fpcr_write)
        mtctrl r2,FPCR
        rts
ENTRY(__rvm_libc_fpsr_write)
        mtctrl r2,FPSR
        rts
