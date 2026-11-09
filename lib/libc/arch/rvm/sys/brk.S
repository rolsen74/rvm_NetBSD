#include "SYS.h"
        .data
        .balign 8
        .globl __minbrk
        .globl __curbrk
__minbrk:
        .quad _end
__curbrk:
        .quad _end
WEAK_ALIAS(brk,_brk)
ENTRY(_brk)
        move.q __minbrk,r1
        cmp.q r1,r2
        bcc 1f
        move.q r1,r2
1:
        SYSTRAP(break)
        bcs __cerror
        move.q r2,__curbrk
        clr.q r0
        rts
