/* Full GPR restoration plus RTE of a version-3 MMU exception. */
#define _LOCORE
#include <machine/asm.h>
#include <machine/bootcopy.h>
        section ".text",code
        SecAlign 4
        xref __rvm_rodata_base
        xref __rvm_data_base
        xref __rvm_bss_base
        xdef rvm_copyfault_entry
        xref rvm_copyfault_dispatch
rvm_copyfault_entry:
        pushm r0-r62
        /* Absolute relocated LEAs bootstrap the section bases themselves. */
        lea __rvm_rodata_base,r50
        lea __rvm_data_base,r51
        lea __rvm_bss_base,r52
        move.q r63,r2
#if RVM_C_CALL_HOME
        sub.q #RVM_C_CALL_HOME,r63
#endif
        jsr rvm_copyfault_dispatch
#if RVM_C_CALL_HOME
        add.q #RVM_C_CALL_HOME,r63
#endif
        popm r0-r62
        rte

        /* Leaf: R2 source, R3 destination, R4 length; R0 errno. */
        xdef rvm_kcopy_bytes
        xdef rvm_kcopy_read
        xdef rvm_kcopy_write
        xdef rvm_kcopy_read_back
        xdef rvm_kcopy_write_back
        xdef rvm_kcopy_fault
rvm_kcopy_bytes:
        tst.q r4
        beq rvm_kcopy_done
        cmp.q r2,r3
        bhi rvm_kcopy_backwards
rvm_kcopy_read:
        move.b (r2)+,r0
rvm_kcopy_write:
        move.b r0,(r3)+
        sub.q #1,r4
        bne rvm_kcopy_read
rvm_kcopy_done:
        rclr.l r0
        rts
rvm_kcopy_backwards:
        radd.q r4,r2,r2
        radd.q r4,r3,r3
rvm_kcopy_read_back:
        move.b -(r2),r0
rvm_kcopy_write_back:
        move.b r0,-(r3)
        sub.q #1,r4
        bne rvm_kcopy_read_back
        rclr.l r0
        rts
rvm_kcopy_fault:
        move.l #RVM_COPY_EFAULT,r0
        rts
