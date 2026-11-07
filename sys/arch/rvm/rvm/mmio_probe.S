#define _LOCORE
        section ".text",code
        SecAlign 4
        xdef rvm_mmio_load8
        xdef rvm_mmio_read
        xdef rvm_mmio_fault
rvm_mmio_load8:
rvm_mmio_read:
        move.q (r2),r0
        move.q r0,(r3)
        rclr.q r0
        rts
rvm_mmio_fault:
        move.q #14,r0
        rts
