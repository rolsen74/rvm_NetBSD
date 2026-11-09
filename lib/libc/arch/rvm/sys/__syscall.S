#include "SYS.h"
WEAK_ALIAS(syscall,__syscall)
STRONG_ALIAS(_syscall,__syscall)
ENTRY(__syscall)
        /* The indirect ABI takes its eighth argument at USP. */
        move.q (72,r63),r1
        push.q r1
        SYSTRAP(__syscall)
        /* LEA preserves the carry/error result. */
        lea (8,r63),r63
        bcs __cerror
        rts
