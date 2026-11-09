#include "SYS.h"
WEAK_ALIAS(pipe,_pipe)
ENTRY(_pipe)
        SYSTRAP(pipe)
        bcs __cerror
        move.l r0,(r2)
        move.l r1,(4,r2)
        clr.q r0
        rts
