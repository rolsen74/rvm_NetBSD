/* C: number R2, first seven arguments R3..R9, eighth at SP+72:
 * eight-byte return address plus the caller's 64-byte register homes.
 * Kernel: number R0, eight argument slots R2..R9; carry marks errno.
 */
#define _LOCORE
#include <sys/syscall.h>
        .text
        .globl __rvm_rawcall
__rvm_rawcall:
        move.q r2,r0
        move.q r3,r2
        move.q r4,r3
        move.q r5,r4
        move.q r6,r5
        move.q r7,r6
        move.q r8,r7
        move.q r9,r8
        move.q (72,r63),r9
        syscall
        bcc success
        rneg.q r0,r0
success:
        rts

        /* A variadic ioctl must preserve its optional third argument in
         * R4, including scalar IOC_VOID arguments. An assembly wrapper
         * avoids reading a nonexistent C va_arg for no-argument ioctls. */
        .globl ioctl
ioctl:
        move.q #SYS_ioctl,r0
        syscall
        bcc ioctl_success
        move.q r0,r2
        jmp __rvm_error
ioctl_success:
        rts
