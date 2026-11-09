/* User-side counterpart of the kernel's raw FPU bank accessors. */
#define _LOCORE
#include <machine/fpu.h>
        .text
        .globl rvm_fpu_save
rvm_fpu_save:
        fpusha r2
        mfctrl FPCR,r0
        mfctrl FPSR,r1
        move.q r0,(RVM_FPREG_CR,r2)
        move.q r1,(RVM_FPREG_SR,r2)
        rts
        .globl rvm_fpu_restore
rvm_fpu_restore:
        fpopa r2
        move.q (RVM_FPREG_CR,r2),r0
        move.q (RVM_FPREG_SR,r2),r1
        mtctrl r0,FPCR
        mtctrl r1,FPSR
        rts
