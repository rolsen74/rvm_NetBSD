/* Normal kernel entry. The standalone test image keeps its own locore.S. */
#define _LOCORE
#include <machine/asm.h>
#include <machine/boot.h>
#include <machine/cache.h>
        section ".text",code
        SecAlign 4
        xref rvm_early_init
        xref rvm_boot_trap
        xref rvm_boot_stack
        xref rvm_boot_vectors
        xdef _start
_start:
        rclr.q r0
        mtctrl r0,cr
        mtctrl r0,inte
        lea rvm_boot_stack,r63
        move.q #RVM_BOOT_STACK_SIZE,r1
        radd.q r1,r63,r63
        rclr.q r62
        lea rvm_boot_vectors,r10
        lea rvm_kernel_exception,r11
        move.q #RVM_BOOT_NVECTORS,r12
.Lvectors:
        move.q r11,(r10)+
        sub.q #1,r12
        bne .Lvectors
        lea rvm_boot_vectors,r10
        mtctrl r10,vbr
        sub.q #RVM_C_CALL_HOME,r63
        jsr rvm_early_init
        jmp rvm_boot_stop
rvm_kernel_exception:
        pushm r0-r62
        move.q r63,r2
        sub.q #RVM_C_CALL_HOME,r63
        jsr rvm_boot_trap
        jmp rvm_boot_stop

        xdef rvm_boot_stop
rvm_boot_stop:
        rclr.q r0
        mtctrl r0,inte
        mfctrl cr,r1
        and.q #8,r1
        mtctrl r1,cr
.Lhalt:
        halt
        jmp .Lhalt
        xdef rvm_boot_get_sr
rvm_boot_get_sr:
        mfctrl sr,r0
        rts
        xdef rvm_boot_get_cr
rvm_boot_get_cr:
        mfctrl cr,r0
        rts
        xdef rvm_set_user_root
rvm_set_user_root:
        mtctrl r2,mut
        rts
        xdef rvm_get_user_sp
rvm_get_user_sp:
        mfctrl usp,r0
        rts
        xdef rvm_set_user_sp
rvm_set_user_sp:
        mtctrl r2,usp
        rts
        xdef rvm_boot_enable_mmu
rvm_boot_enable_mmu:
        mtctrl r2,mst
        move.q #RVM_KERNEL_CR,r0
        mtctrl r0,cr
        rts
        xdef rvm_tlb_invalidate_all
rvm_tlb_invalidate_all:
        cinva #1,#4
        rts
        xdef rvm_icache_invalidate_all
rvm_icache_invalidate_all:
        cinva #1,#2
        rts
        xdef rvm_intr_disable
rvm_intr_disable:
        mfctrl cr,r0
        move.q r0,r1
        and.q #0xfffffffffffffffb,r1
        mtctrl r1,cr
        rts
        xdef rvm_irq_set_cr
rvm_irq_set_cr:
        mtctrl r2,cr
        rts
        xdef rvm_irq_set_sr
rvm_irq_set_sr:
        mtctrl r2,sr
        rts
        xdef rvm_irq_set_inte
rvm_irq_set_inte:
        mtctrl r2,inte
        rts
        xdef rvm_irq_get_inte
rvm_irq_get_inte:
        mfctrl inte,r0
        rts
        xdef rvm_cpu_halt
rvm_cpu_halt:
        halt
        rts
        xdef rvm_idle_halt
rvm_idle_halt:
        /* Enter with IRQs disabled. Preserve MMU/cache/TLB controls. */
        mfctrl cr,r0
        ori.q #4,r0
#ifdef RVM_IDLE_RACE_TEST
        /* Wait beyond a timer period with IRQs masked, then enable them
         * immediately before HALT. The EARLY check requires a skipped HALT. */
        move.q #0x0700000000001120,r1
        move.q (r1),r2
.Lidle_pending:
        move.q (r1),r3
        rsub.q r2,r3,r3
        cmp.q #100000000,r3
        bcs .Lidle_pending
#endif
        mtctrl r0,cr
        xdef rvm_idle_halt_pc
rvm_idle_halt_pc:
        halt
        xdef rvm_idle_after_halt
rvm_idle_after_halt:
        and.q #0xfffffffffffffffb,r0
        mtctrl r0,cr
        rts
        xdef rvm_emulator_putchar
rvm_emulator_putchar:
        move.q r2,r3
        move.q #3,r2
        emufunc
        rts
        xdef rvm_kernel_irq_entry
        xref rvm_kernel_irq
rvm_kernel_irq_entry:
        pushm r0-r62
        mfctrl cr,r0
        pushm r0
        move.q #RVM_KERNEL_CR,r0
        mtctrl r0,cr
        move.q r63,r2
        sub.q #RVM_C_CALL_HOME,r63
        jsr rvm_kernel_irq
        add.q #RVM_C_CALL_HOME,r63
        popm r0
        mtctrl r0,cr
        popm r0-r62
        rte
        section ".rodata",data_ro
        SecAlign 4
        xref kernel_pmap_store
        xdef kernel_pmap_ptr
kernel_pmap_ptr:
        dc.q kernel_pmap_store
