; NetBSD/RVM early bootstrap

        .text
        .globl  rvm_start
        .globl  _start

_start:
rvm_start:
        jsr     rvm_early_init

.Lunexpected_return:
        jmp     .Lunexpected_return
