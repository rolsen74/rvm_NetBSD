/* Static FHR entry. Stack layout is MI argc, argv[], NULL, envp[], NULL. */
        .text
        .globl _start
_start:
        move.q (r63),r2
        lea (8,r63),r3
        move.q r2,r4
        add.q #2,r4
        lsl.q #3,r4
        add.q r63,r4
        sub.q #80,r63
        jsr __rvm_start
stop:
        jmp stop
