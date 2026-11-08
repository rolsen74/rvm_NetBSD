/* Raw encodings until the assemblers gain the v0000 FP mnemonic table.
 * FPusha/FPopa use one integer address register, with no stack adjustment.
 */
#define _LOCORE
#include <machine/fpu.h>
        section ".text",code
        SecAlign 4
        xdef rvm_fpu_save
rvm_fpu_save:
        dc.w 0x01c3
        dc.b 2
        /* mfctrl FPCR,r0; mfctrl FPSR,r1 */
        dc.w 0x0069,12
        dc.b 0
        dc.w 0x0069,13
        dc.b 1
        move.q r0,(RVM_FPREG_CR,r2)
        move.q r1,(RVM_FPREG_SR,r2)
        rts
        xdef rvm_fpu_restore
rvm_fpu_restore:
        dc.w 0x01c1
        dc.b 2
        move.q (RVM_FPREG_CR,r2),r0
        move.q (RVM_FPREG_SR,r2),r1
        /* mtctrl r0,FPCR; mtctrl r1,FPSR */
        dc.w 0x0068
        dc.b 0
        dc.w 12
        dc.w 0x0068
        dc.b 1
        dc.w 13
        rts
