#include "SYS.h"
#include <sys/errno.h>
WEAK_ALIAS(clone,__clone)
ENTRY(__clone)
        move.q #EINVAL,r0
        tst.q r2
        beq __cerror
        tst.q r3
        beq __cerror
        and.q #-16,r3
        sub.q #(RVM_C_CALL_HOME+16),r3
        move.q r2,(RVM_C_CALL_HOME,r3)
        move.q r5,(RVM_C_CALL_HOME+8,r3)
        move.q r4,r2
        SYSTRAP(__clone)
        bcs __cerror
        tst.q r1
        bne 1f
        rts
1:
        move.q (RVM_C_CALL_HOME,r63),r1
        move.q (RVM_C_CALL_HOME+8,r63),r2
        jsr (r1)
        move.q r0,r2
        SYSTRAP(exit)
