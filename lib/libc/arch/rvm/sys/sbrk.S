#include "SYS.h"
WEAK_ALIAS(sbrk,_sbrk)
ENTRY(_sbrk)
        move.q __curbrk,r1
        add.q r1,r2
        SYSTRAP(break)
        bcs __cerror
        move.q __curbrk,r0
        move.q r2,__curbrk
        rts
