/* The fixture builder appends a genuine two-page FHR BSS section at
 * 0x410000. Current GAS FHR BSS emission truncates large reservations;
 * constructing that section explicitly also tests the kernel independently.
 */
#define _LOCORE
#include <sys/syscall.h>
        .text
        .globl _start
_start:
        sub.q #80,r63
        move.q #0x410000,r2
        move.q #0x420000,r3
        move.q #0x42fff8,r4
        move.q (r2),r0
        tst.q r0
        bne failed
        move.q (r3),r0
        tst.q r0
        bne failed
        move.q (r4),r0
        tst.q r0
        bne failed
        move.q #0x123456789abcdef0,r5
        move.q r5,(r2)
        move.q r5,(r3)
        move.q r5,(r4)
        move.q (r2),r0
        cmp.q r5,r0
        bne failed
        move.q (r3),r0
        cmp.q r5,r0
        bne failed
        move.q (r4),r0
        cmp.q r5,r0
        bne failed
        move.q #41,r2
        jmp finish
failed:
        move.q #99,r2
finish:
        move.q #SYS_exit,r0
        syscall
stop:
        jmp stop
