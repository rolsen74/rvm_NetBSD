#include <machine/asm.h>
#include <sys/syscall.h>

/* The int API must discard these upper bits, independent of register history. */
ENTRY(rvm_syscall_dirty_number)
        move.q #0xdeadbeef00000000+SYS_getpid,r2
        jmp syscall

/* The quad_t API must preserve them and reject this out-of-range number. */
ENTRY(rvm_syscall_wide_number)
        move.q #0x100000000+SYS_getpid,r2
        jmp __syscall
