#include "SYS.h"
#include "assym.h"
WEAK_ALIAS(getcontext,_getcontext)
ENTRY(_getcontext)
        SYSTRAP(getcontext)
        bcs __cerror
        clr.q (UC_RV,r2)
        move.q (r63),r1
        move.q r1,(UC_PC,r2)
        lea (8,r63),r1
        move.q r1,(UC_SP,r2)
        rts
