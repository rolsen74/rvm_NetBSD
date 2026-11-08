/* EARLY-only hardware user-mode roundtrip; not the NetBSD syscall ABI. */
#define _LOCORE
#include <machine/asm.h>
        section ".text",code
        SecAlign 4
        xdef rvm_user_probe
/* R2=root, R3=user PC, R4=user SP; caller masks IRQs across the probe. */
rvm_user_probe:
        pushm r10-r62
        mfctrl sr,r10
        mfctrl mut,r11
        mfctrl usp,r12
        mfctrl cr,r13
        pushm r10-r13
        mtctrl r2,mut
        mtctrl r4,usp
        sub.q #24,r63
        move.q #0x0002000a00000000,r10
        move.q r10,(0,r63)
        move.q r3,(8,r63)
        move.q r2,(16,r63)
        move.q #8,r10
        mtctrl r10,cr
        rte
        xdef rvm_user_probe_return
rvm_user_probe_return:
        popm r10-r13
        mtctrl r12,usp
        mtctrl r11,mut
        mtctrl r10,sr
        mtctrl r13,cr
        popm r10-r62
        rts

        xdef rvm_user_probe_entry
        xref rvm_user_probe_dispatch
rvm_user_probe_entry:
        pushm r0-r62
        move.q r63,r2
        sub.q #RVM_C_CALL_HOME,r63
        jsr rvm_user_probe_dispatch
        add.q #RVM_C_CALL_HOME,r63
        popm r0-r62
        rte

/* Only relative branches and immediate constants: safe to copy into user VA. */
        xdef rvm_user_probe_code
        xdef rvm_user_probe_end
rvm_user_probe_code:
        move.q #0x123456789abcdef0,r1
        sub.q #8,r63
        move.q r1,(r63)
        move.q (r63),r2
        cmp.q r1,r2
        bne .Lbad_user_stack
        add.q #8,r63
        move.q #0x52564d5553455201,r0
        syscall
        cmp.q #0x52564d5553455202,r0
        bne .Lbad_user_stack
        move.q #0x52564d5553455203,r0
        syscall
.Lbad_user_stack:
        rclr.q r0
        syscall
        xdef rvm_user_probe_kernel_read
rvm_user_probe_kernel_read:
        move.q #0xffff800001000000,r2
        move.q (r2),r0
        syscall
        xdef rvm_user_probe_text_write
rvm_user_probe_text_write:
        move.q #0x400000,r2
        move.q #1,(r2)
        syscall
        xdef rvm_user_probe_privilege
rvm_user_probe_privilege:
        rte
        syscall
rvm_user_probe_end:
