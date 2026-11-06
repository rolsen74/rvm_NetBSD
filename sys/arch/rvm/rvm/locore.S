/* NetBSD/RVM supervisor entry after FHR relocation and BSS clearing. */
#define _LOCORE
#include <machine/boot.h>

        section ".text",code
        SecAlign 4
        xref rvm_early_init
        xref rvm_boot_trap
        xref rvm_boot_stack
        xref rvm_boot_vectors
        xdef  _start
        xdef  rvm_start
_start:
rvm_start:
        /* Loader enters with caches/MMU off. No stack access before LEA. */
        rclr.q  r0
        mtctrl  r0,cr
        mtctrl  r0,inte
        lea     rvm_boot_stack,r63
        move.q  #RVM_BOOT_STACK_SIZE,r1
        radd.q  r1,r63,r63
        rclr.q  r62

        /* All vectors stop with diagnostics during this bring-up stage. */
        lea     rvm_boot_vectors,r10
        lea     rvm_boot_exception,r11
        move.q  #RVM_BOOT_NVECTORS,r12
.Lvector:
        move.q  r11,(r10)+
        sub.q   #1,r12
        bne     .Lvector
        lea     rvm_boot_vectors,r10
        mtctrl  r10,vbr
        jsr     rvm_early_init
        jmp     rvm_boot_stop

rvm_boot_exception:
        /* Preserve GPRs before C; fatal only, no RTE/context restoration. */
        pushm   r0-r62
        move.q  r63,r2
        jsr     rvm_boot_trap
        jmp     rvm_boot_stop

        /* Version-2 supervisor entry: complete GPR image, then hardware frame. */
        xdef rvm_supervisor_entry
        xref rvm_supervisor_dispatch
rvm_supervisor_entry:
        pushm r0-r62
        move.q r63,r2
        jsr rvm_supervisor_dispatch
        popm r0-r62
        rte

        /* The saved CR precedes GPRs and the version-0 hardware frame. */
        xdef rvm_timer_interrupt_entry
        xref rvm_timer_irq_dispatch
rvm_timer_interrupt_entry:
        pushm r0-r62
        mfctrl cr,r0
        pushm r0
        move.q #8,r0
        mtctrl r0,cr
        move.q r63,r2
        jsr rvm_timer_irq_dispatch
        popm r0
        /* Only the selected timer IRQ was enabled; the handler masked it.
         * Thus restoring CR before the GPR image cannot admit another IRQ.
         */
        mtctrl r0,cr
        popm r0-r62
        rte

        xdef rvm_irq_get_inte
rvm_irq_get_inte:
        mfctrl inte,r0
        rts

        xdef rvm_irq_set_inte
rvm_irq_set_inte:
        mtctrl r2,inte
        rts
        xdef rvm_irq_set_cr
rvm_irq_set_cr:
        mtctrl r2,cr
        rts

        /* One idle wait. IRQ setup/return will be added before enabling IRQs. */
        xdef rvm_cpu_halt
rvm_cpu_halt:
        halt
        rts

        xdef  rvm_boot_stop
rvm_boot_stop:
        rclr.q  r0
        mtctrl  r0,inte
        /* Preserve translation for inspection once memory bootstrap succeeds. */
        mfctrl  cr,r1
        and.q   #8,r1
        mtctrl  r1,cr
.Lstop:
        halt
        jmp     .Lstop

        xdef  rvm_boot_get_sr
rvm_boot_get_sr:
        mfctrl  sr,r0
        rts

        xdef rvm_boot_enable_mmu
rvm_boot_enable_mmu:
        mtctrl r2,mst
        move.q #8,r0
        mtctrl r0,cr
        rts

        xdef rvm_boot_get_cr
rvm_boot_get_cr:
        mfctrl cr,r0
        rts

        /* SnakeOS EmuFunc.i: command r2=3, character r3, result r0. */
        xdef  rvm_emulator_putchar
rvm_emulator_putchar:
        move.q  r2,r3
        move.q  #3,r2
        emufunc
        rts

        /* Capture R3-R9 until GCC's va_start supports this ABI.
         * PUSHF saves mask (8), R3-R9 (56), R62 (8).
         * Early UVM diagnostics do not use stacked arguments.
         */
        xref rvm_uvm_diagnostic
        xdef printf
printf:
        pushf r3-r9/r62
        move.q #0,r4
        jmp rvm_uvm_print_entry
        xdef panic
panic:
        pushf r3-r9/r62
        move.q #1,r4
rvm_uvm_print_entry:
        move.q r62,r3
        add.q #8,r3
        jsr rvm_uvm_diagnostic
        popf
        rts

        /* GCC's current RVM backend cannot emit symbolic pointer constants. */
        section ".rodata",data_ro
        SecAlign 4
        xref kernel_pmap_store
        xdef kernel_pmap_ptr
kernel_pmap_ptr:
        dc.q kernel_pmap_store

        /* Keep following FHR data four-byte aligned: GCC omits scalar padding. */
        section ".data",data
        SecAlign 4
        xdef mp_online
mp_online:
        dc.l 0
