/* syscall takes an int number; the ABI leaves its upper word unspecified. */
#include "SYS.h"
WEAK_ALIAS(syscall,_syscall)
ENTRY(_syscall)
        rextu.l r2,r2
        jmp __syscall
