/* Save the caller directly, so restoring it returns beyond this wrapper. */
#include "SYS.h"
#include "assym.h"
ENTRY(swapcontext)
        SYSTRAP(getcontext)
        bcs __cerror
        clr.q (UC_RV,r2)
        move.q (r63),r1
        move.q r1,(UC_PC,r2)
        lea (8,r63),r1
        move.q r1,(UC_SP,r2)
        move.q r3,r2
        SYSTRAP(setcontext)
        bcs __cerror
        rts
