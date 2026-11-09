/* Locate errno through libc's current-thread interface. */
#include "SYS.h"
ENTRY(__cerror)
        push.q r0
        sub.q #RVM_C_CALL_HOME,r63
        jsr __errno
        add.q #RVM_C_CALL_HOME,r63
        pop.q r1
        move.l r1,(r0)
        move.q #-1,r0
        move.q #-1,r1
        rts
