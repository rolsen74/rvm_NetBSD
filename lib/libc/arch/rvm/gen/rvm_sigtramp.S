/* sendsig keeps the ucontext pointer in preserved register R10. */
#include "SYS.h"
ENTRY(__sigtramp_siginfo_2)
        move.q r10,r2
        SYSTRAP(setcontext)
        move.q #127,r2
        SYSTRAP(exit)
