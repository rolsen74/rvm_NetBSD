/* crt0 and syscall bridge for the libc-free C exec integration fixture. */
#define _LOCORE
#include <sys/syscall.h>
        .text
        .globl _start, rvm_call, rvm_mount
_start:
        move.q (r63),r2
        lea (8,r63),r3
        move.q r2,r4
        add.q #2,r4
        lsl.q #3,r4
        add.q r63,r4
        sub.q #80,r63
        jsr disk_main
        move.q r0,r2
        move.q #SYS_exit,r0
        syscall
stop:
        jmp stop
rvm_call:
rvm_mount:
        move.q r2,r0
        move.q r3,r2
        move.q r4,r3
        move.q r5,r4
        move.q r6,r5
        move.q r7,r6
        syscall
        bcc call_done
        rneg.q r0,r0
call_done:
        rts
