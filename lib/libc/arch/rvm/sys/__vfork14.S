/* Keep the return address off the stack shared with the child. */
#include "SYS.h"
ENTRY(__vfork14)
        move.q (r63),r9
        add.q #8,r63
        SYSTRAP(__vfork14)
        bcc 1f
        sub.q #8,r63
        move.q r9,(r63)
        jmp __cerror
1:
        jmp (r9)
