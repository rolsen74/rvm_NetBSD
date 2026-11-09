/* PTRACE may successfully return -1; clear errno before invoking it. */
#include "SYS.h"
ENTRY(ptrace)
        push.q r2
        push.q r3
        push.q r4
        push.q r5
        sub.q #RVM_C_CALL_HOME,r63
        jsr __errno
        add.q #RVM_C_CALL_HOME,r63
        clr.l (r0)
        pop.q r5
        pop.q r4
        pop.q r3
        pop.q r2
        SYSTRAP(ptrace)
        bcs __cerror
        rts
