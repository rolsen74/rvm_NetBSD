/* User-side counterpart of the kernel's raw FPU bank accessors. */
#define _LOCORE
#include <machine/fpu.h>
        .text
        .globl rvm_fpu_save
rvm_fpu_save:
        .short 0x01c3
        .byte 2
        .short 0x0069,12
        .byte 0
        .short 0x0069,13
        .byte 1
        move.q r0,(RVM_FPREG_CR,r2)
        move.q r1,(RVM_FPREG_SR,r2)
        rts
        .globl rvm_fpu_restore
rvm_fpu_restore:
        .short 0x01c1
        .byte 2
        move.q (RVM_FPREG_CR,r2),r0
        move.q (RVM_FPREG_SR,r2),r1
        .short 0x0068
        .byte 0
        .short 12
        .short 0x0068
        .byte 1
        .short 13
        rts
