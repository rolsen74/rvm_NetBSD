/* R2/R3: old/new PCB; R4: previous LWP, returned in R0 on the new stack. */
#define _LOCORE
#include <machine/switch.h>
        section ".text",code
        SecAlign 4
        /* Keep the standard three-argument MI entry. The current compiler
         * gives a bool argument a one-byte stack home without ABI padding;
         * the C body needs only the two LWP pointers (returning is unused).
         */
        /* bool has an unpadded byte home in the current compiler ABI. */
        xdef cpu_uarea_alloc
        xref rvm_cpu_uarea_alloc
cpu_uarea_alloc:
        and.q #1,r2
        jmp rvm_cpu_uarea_alloc

        xdef cpu_switchto
        xref rvm_cpu_switchto
cpu_switchto:
        jmp rvm_cpu_switchto

        xdef rvm_cpu_switch
rvm_cpu_switch:
        pushm r10-r62
        move.q r63,(RVM_PCB_SP,r2)
        move.q r63,(RVM_PCB_SSP,r2)
        move.q (RVM_SWITCH_PC,r63),r5
        move.q r5,(RVM_PCB_PC,r2)
        move.q (RVM_PCB_SP,r3),r63
        move.q r4,r0
        popm r10-r62
        rts

        xdef rvm_lwp_trampoline
        xref rvm_cpu_lwp_start
rvm_lwp_trampoline:
        move.q r0,r4
        move.q r10,r2
        move.q r11,r3
        jsr rvm_cpu_lwp_start
        xref rvm_boot_stop
        jmp rvm_boot_stop
