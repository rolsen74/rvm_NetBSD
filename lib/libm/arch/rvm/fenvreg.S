/* Integer ABI accessors; no FP register or caller stack storage is touched. */
        .text
        .globl __rvm_fpcr_read
__rvm_fpcr_read:
        mfctrl FPCR,r0
        rts
        .globl __rvm_fpsr_read
__rvm_fpsr_read:
        mfctrl FPSR,r0
        rts
        .globl __rvm_fpcr_write
__rvm_fpcr_write:
        mtctrl r2,FPCR
        rts
        .globl __rvm_fpsr_write
__rvm_fpsr_write:
        mtctrl r2,FPSR
        rts
