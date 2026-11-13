/* Normal kernel entry. The standalone test image keeps its own locore.S. */
#define _LOCORE
#include "opt_multiprocessor.h"
#include <machine/asm.h>
#include <machine/boot.h>
#include <machine/cache.h>
#include <machine/psl.h>
        section ".text",code
        SecAlign 4
        xref __rvm_rodata_base
        xref __rvm_data_base
        xref __rvm_bss_base
        xref rvm_early_init
        xref rvm_boot_trap
        xref rvm_boot_stack
        xref rvm_boot_vectors
        xref rvm_secondary_boot_stack_tops
        xref rvm_secondary_ci_ptrs
        xref rvm_secondary_arrived
        xref rvm_secondary_go
        xref rvm_icnt_clock_ready
#ifdef MULTIPROCESSOR
        xref rvm_secondary_hatch
        xref idle_loop
#endif
        xref cpu_info_primary
#ifdef MULTIPROCESSOR
#endif
        xdef _start
        xdef __rvm_section_bases_v1
__rvm_section_bases_v1:
_start:
        /* Absolute relocated LEAs bootstrap the section bases themselves. */
        lea __rvm_rodata_base,r50
        lea __rvm_data_base,r51
        lea __rvm_bss_base,r52
        cpuid r0
        bne rvm_secondary_start
        rclr.q r0
        mtctrl r0,cr
        mtctrl r0,inte
        /* The loader may leave an instruction countdown armed. */
        dc.w 0x0068
        dc.b 0,0,RVM_CREG_ICNT
#ifdef MULTIPROCESSOR
        lea cpu_info_primary,r0
        mtctrl r0,ud1
#endif
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
#if RVM_C_CALL_HOME
        sub.q #RVM_C_CALL_HOME,r63
#endif
        jsr rvm_early_init
        jmp rvm_boot_stop
rvm_secondary_start:
        rclr.q r0
        mtctrl r0,cr
        mtctrl r0,inte
        dc.w 0x0068
        dc.b 0,0,RVM_CREG_ICNT
#ifdef MULTIPROCESSOR
        cpuid r0
        cmp.q #8,r0
        bcc rvm_boot_stop
        rclr.q r11
.Lsecondary_index:
        add.q #8,r11
        sub.q #1,r0
        bne .Lsecondary_index
        lea rvm_secondary_ci_ptrs,r10
        radd.q r11,r10,r10
        move.q (r10),r0
        mtctrl r0,ud1
        lea rvm_secondary_boot_stack_tops,r10
        radd.q r11,r10,r10
        move.q (r10),r63
        lea rvm_secondary_arrived,r10
        radd.q r11,r10,r10
        move.q #1,(r10)
#endif
.Lsecondary_wait:
#ifdef MULTIPROCESSOR
        lea rvm_secondary_go,r10
        move.q (r10),r0
        tst.q r0
        bne .Lsecondary_go
#endif
        halt #RVM_ICNT_QUANTUM
        jmp .Lsecondary_wait
#ifdef MULTIPROCESSOR
.Lsecondary_go:
        lea rvm_boot_vectors,r10
        mtctrl r10,vbr
#if RVM_C_CALL_HOME
        sub.q #RVM_C_CALL_HOME,r63
#endif
        jsr rvm_secondary_hatch
        move.q r0,r63
        rclr.q r2
#if RVM_C_CALL_HOME
        sub.q #RVM_C_CALL_HOME,r63
#endif
        jsr idle_loop
        jmp rvm_boot_stop
#endif
rvm_kernel_exception:
        pushm r0-r62
        /* Absolute relocated LEAs bootstrap the section bases themselves. */
        lea __rvm_rodata_base,r50
        lea __rvm_data_base,r51
        lea __rvm_bss_base,r52
        move.q r63,r2
#if RVM_C_CALL_HOME
        sub.q #RVM_C_CALL_HOME,r63
#endif
        jsr rvm_boot_trap
        jmp rvm_boot_stop

        xdef rvm_boot_exit
rvm_boot_exit:
        emuexit
        jmp rvm_boot_stop

        xdef rvm_boot_reset
rvm_boot_reset:
        vmove.q #16,r2
        emufunc
        jmp rvm_boot_stop

        xdef rvm_boot_stop
rvm_boot_stop:
        rclr.q r0
        mtctrl r0,inte
        /* Stop instruction-count exceptions before the terminal HALT. */
        dc.w 0x0068
        dc.b 0,0,RVM_CREG_ICNT
        mfctrl cr,r1
        and.q #8,r1
        mtctrl r1,cr
.Lhalt:
        halt #0
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
        clri
        rts
        xdef rvm_curcpu
rvm_curcpu:
        mfctrl ud1,r0
        rts
        xdef rvm_atomic_exchange64
rvm_atomic_exchange64:
        /* AtmExg.q r3,(r2),r0: opcode 0x0183, three register bytes. */
        dc.w 0x0183
        dc.b 3,2,0
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
        halt #0
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
        halt #RVM_ICNT_QUANTUM
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
        /* Absolute relocated LEAs bootstrap the section bases themselves. */
        lea __rvm_rodata_base,r50
        lea __rvm_data_base,r51
        lea __rvm_bss_base,r52
        mfctrl cr,r0
        pushm r0
        move.q #RVM_KERNEL_CR,r0
        mtctrl r0,cr
        move.q r63,r2
#if RVM_C_CALL_HOME
        sub.q #RVM_C_CALL_HOME,r63
#endif
        jsr rvm_kernel_irq
#if RVM_C_CALL_HOME
        add.q #RVM_C_CALL_HOME,r63
#endif
        /* VMAsm predates ICNT: MTCtrl R0,ICNT is opcode 0x68, 0,0,14. */
        move.q #RVM_ICNT_QUANTUM,r0
        dc.w 0x0068
        dc.b 0,0,RVM_CREG_ICNT
        popm r0
        mtctrl r0,cr
        popm r0-r62
        rte
        xdef rvm_icnt_start
rvm_icnt_start:
        move.q #RVM_ICNT_QUANTUM,r0
        dc.w 0x0068
        dc.b 0,0,RVM_CREG_ICNT
        rts
        xdef rvm_icnt_ensure
rvm_icnt_ensure:
        /* MFctrl ICNT,r0 is opcode 0x69, 0,14,0.  A zero ICNT has
         * disabled scheduling on this CPU; leave active countdowns alone. */
        lea rvm_icnt_clock_ready,r1
        move.q (r1),r1
        tst.q r1
        beq .Licnt_ready
        dc.w 0x0069
        dc.b 0,RVM_CREG_ICNT,0
        tst.q r0
        bne .Licnt_ready
        move.q #RVM_ICNT_QUANTUM,r0
        dc.w 0x0068
        dc.b 0,0,RVM_CREG_ICNT
.Licnt_ready:
        rts
        section ".rodata",data_ro
        SecAlign 4
        xref kernel_pmap_store
        xdef kernel_pmap_ptr
kernel_pmap_ptr:
        dc.q kernel_pmap_store
