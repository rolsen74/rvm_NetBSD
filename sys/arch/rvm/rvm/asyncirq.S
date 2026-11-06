/* INTE, CR, all non-SP GPRs, then the version-0 hardware frame. */
#define _LOCORE
#include <machine/psl.h>
        section ".text",code
        SecAlign 4
        xdef rvm_intr_disable
rvm_intr_disable:
        mfctrl cr,r0
        move.q r0,r1
        and.q #0xfffffffffffffffb,r1
        mtctrl r1,cr
        rts
        xdef rvm_check_mode
        xref rvm_bad_mode
rvm_check_mode:
        mfctrl cr,r0
        cmp.q #8,r0
        beq .Lmode_ok
        move.q (r63),r2
        move.q #8,r0
        mtctrl r0,cr
        jmp rvm_bad_mode
.Lmode_ok:
        rts
        xdef rvm_irq_set_sr
rvm_irq_set_sr:
        mtctrl r2,sr
        rts

        xdef rvm_async_interrupt_entry
        xref rvm_async_irq_dispatch
        xref rvm_async_irq_return
rvm_async_interrupt_entry:
        pushm r0-r62
        mfctrl cr,r0
        pushm r0
        mfctrl inte,r0
        pushm r0
        move.q #8,r0
        mtctrl r0,cr
        rclr.q r0
        mtctrl r0,inte
        move.q r63,r2
        jsr rvm_async_irq_dispatch
        move.q r63,r2
        jsr rvm_async_irq_return
        popm r1
        popm r0
        /* Neither timer line can nest in the restore window. RTE restores
         * the interrupted SR only after every GPR and SP is back in place. */
        move.q #0x3f0020,r2
        mtctrl r2,sr
        mtctrl r1,inte
        mtctrl r0,cr
        popm r0-r62
        rte

        xdef rvm_async_halt
rvm_async_halt:
        /* Called with CR=8, then enable and wait in one recognized window. */
        move.q #12,r0
        mtctrl r0,cr
        xdef rvm_async_halt_pc
rvm_async_halt_pc:
        halt
        xdef rvm_async_after_halt
rvm_async_after_halt:
        move.q #8,r0
        mtctrl r0,cr
        rts
