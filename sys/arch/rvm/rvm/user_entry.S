/* User entry starts at the canonical trapframe, above an empty kernel stack. */
#define _LOCORE
#include <machine/asm.h>
#include <machine/user.h>
#include <machine/cache.h>
        section ".text",code
        SecAlign 4
        xdef rvm_user_resume
        xref rvm_fpu_restore
rvm_user_resume:
        move.q #RVM_KERNEL_CR,r0
        mtctrl r0,cr
        move.q (RVM_TF_USP,r2),r0
        mtctrl r0,usp
        move.q (RVM_TF_ROOT,r2),r0
        mtctrl r0,mut
        add.q #RVM_TF_FP,r2
        sub.q #RVM_C_CALL_HOME,r63
        jsr rvm_fpu_restore
        add.q #RVM_C_CALL_HOME,r63
        sub.q #RVM_TF_FP,r2
        move.q r2,r63
        sub.q #528,r63
        move.q r63,r3
        move.q r2,r4
        move.q #63,r5
.Lcopy_user_regs:
        move.q (r4)+,r0
        move.q r0,(r3)+
        sub.q #1,r5
        bne .Lcopy_user_regs
        move.q (RVM_TF_SR,r2),r0
        ori.q #0x0002000a00000000,r0
        move.q r0,(r3)+
        move.q (RVM_TF_PC,r2),r0
        move.q r0,(r3)+
        move.q (RVM_TF_ROOT,r2),r0
        move.q r0,(r3)
        move.q #RVM_USER_CR,r0
        mtctrl r0,cr
        popm r0-r62
        rte

        xdef rvm_user_exception_entry
        xref rvm_user_exception
rvm_user_exception_entry:
        pushm r0-r62
        move.q r63,r2
        sub.q #RVM_C_CALL_HOME,r63
        jsr rvm_user_exception
        add.q #RVM_C_CALL_HOME,r63
        popm r0-r62
        rte
