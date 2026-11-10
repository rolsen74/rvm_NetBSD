/* EARLY resident user program. Native syscalls, no private kernel trap codes. */
#define _LOCORE
#include <sys/syscall.h>
#include <sys/errno.h>
#include <machine/process_test.h>
        section ".text",code
        SecAlign 4
        xdef rvm_process_code
        xdef rvm_process_end
rvm_process_code:
        /* Demand-fault the initially nonresident stack. */
        sub.q #80,r63
        move.q #0x123456789abcdef0,r10
        move.q r10,(r63)
        move.q (r63),r11
        cmp.q r10,r11
        bne rvm_process_failure
        move.q #SYS_getpid,r0
        syscall
        bcs rvm_process_failure
        cmp.q #1,r0
        bls rvm_process_failure
        move.q r0,r12
        /* SYS_syscall indirection must shift arguments correctly. */
        move.q #SYS_syscall,r0
        move.q #SYS_getpid,r2
        syscall
        bcs rvm_process_failure
        cmp.q r12,r0
        bne rvm_process_failure
        move.q #SYS___syscall,r0
        move.q #SYS_getpid,r2
        syscall
        bcs rvm_process_failure
        cmp.q r12,r0
        bne rvm_process_failure
        move.q #0x7fffffff,r0
        syscall
        bcc rvm_process_failure
        cmp.q #ENOSYS,r0
        bne rvm_process_failure
        /* A bad descriptor must return errno with carry set. */
        move.q #SYS_write,r0
        move.q #-1,r2
        move.q #0x500000,r3
        move.q #1,r4
        syscall
        bcc rvm_process_failure
        cmp.q #EBADF,r0
        bne rvm_process_failure
        /* A bad user pointer must recover, without killing this process. */
        move.q #SYS_write,r0
        move.q #1,r2
        rclr.q r3
        move.q #1,r4
        syscall
        bcc rvm_process_failure
        cmp.q #EFAULT,r0
        bne rvm_process_failure
        /* Real sleep(2): the syscall may block and switch stacks/processes. */
        move.q #SYS___nanosleep50,r0
        move.q #0x500100,r2
        rclr.q r3
        syscall
        bcs rvm_process_failure
        /* A timer interrupt must return from the real user IRQ path. */
        move.q #1000000,r15
.Luser_busy:
        sub.q #1,r15
        bne .Luser_busy
        /* Fork from user mode, dirty private data in the child, and reap it. */
        move.q #0x500200,r16
        move.b #0x55,(r16)
        move.q #SYS_fork,r0
        syscall
        bcs rvm_process_failure
        tst.q r0
        beq .Luser_child
        move.q r0,r14
        move.q #SYS___wait450,r0
        move.q r14,r2
        move.q #0x500208,r3
        rclr.q r4
        rclr.q r5
        syscall
        bcs rvm_process_failure
        cmp.q r14,r0
        bne rvm_process_failure
        move.q #0x500208,r2
        move.l (r2),r0
        cmp.l #5888,r0
        bne rvm_process_failure
        move.b (r16),r0
        cmp.b #0x55,r0
        bne rvm_process_failure
        /* Fatal user faults must kill only the child and remain waitable. */
        move.q #1,r17
.Lfatal_test:
        move.q #SYS_fork,r0
        syscall
        bcs rvm_process_failure
        tst.q r0
        beq .Lfatal_child
        move.q r0,r14
        move.q #SYS___wait450,r0
        move.q r14,r2
        move.q #0x500208,r3
        rclr.q r4
        rclr.q r5
        syscall
        bcs rvm_process_failure
        cmp.q r14,r0
        bne rvm_process_failure
        move.q #0x500208,r2
        move.l (r2),r0
        cmp.q #1,r17
        bne .Lexpect_segv
        cmp.l #4,r0
        bne rvm_process_failure
        jmp .Lfatal_next
.Lexpect_segv:
        cmp.l #11,r0
        bne rvm_process_failure
.Lfatal_next:
        add.q #1,r17
        cmp.q #4,r17
        bcs .Lfatal_test
        move.q #SYS___sigaltstack14,r0
        move.q #0x500480,r2
        rclr.q r3
        syscall
        bcs rvm_process_failure
        move.q #SYS___sigaction_sigtramp,r0
        move.q #RVM_PROCESS_SIGNAL,r2
        move.q #0x500400,r3
        rclr.q r4
        move.q #0x5004a0,r5
        move.q (r5),r5
        move.q #2,r6
        syscall
        bcs rvm_process_failure
        move.q #SYS_kill,r0
        move.q r12,r2
        move.q #RVM_PROCESS_SIGNAL,r3
        syscall
        bcs rvm_process_failure
        move.q #0x500210,r2
        move.b (r2),r0
        cmp.b #1,r0
        bne rvm_process_failure
        /* Interrupt a blocking wait with SA_RESTART, then restore/retry it. */
        move.b #0,(r2)
        move.q #SYS_fork,r0
        syscall
        bcs rvm_process_failure
        tst.q r0
        beq .Lsignal_sender
        move.q r0,r14
        move.q #SYS___wait450,r0
        move.q r14,r2
        move.q #0x500208,r3
        rclr.q r4
        rclr.q r5
        syscall
        bcs rvm_process_failure
        cmp.q r14,r0
        bne rvm_process_failure
        move.q #0x500208,r2
        move.l (r2),r0
        cmp.l #10752,r0
        bne rvm_process_failure
        move.q #0x500210,r2
        move.b (r2),r0
        cmp.b #1,r0
        bne rvm_process_failure
        move.q #SYS_write,r0
        move.q #1,r2
        move.q #0x500000,r3
        move.q #RVM_PROCESS_MESSAGE_SIZE,r4
        syscall
        bcs rvm_process_failure
        cmp.q #RVM_PROCESS_MESSAGE_SIZE,r0
        bne rvm_process_failure
        cmp.q #0x123456789abcdef0,r10
        bne rvm_process_failure
        cmp.q r10,r11
        bne rvm_process_failure
        move.q #SYS_exit,r0
        move.q #37,r2
        syscall
.Luser_child:
        move.b #0x66,(r16)
        move.q #SYS_exit,r0
        move.q #23,r2
        syscall
.Lsignal_sender:
        move.q #SYS___nanosleep50,r0
        move.q #0x500100,r2
        rclr.q r3
        syscall
        bcs rvm_process_failure
        move.q #SYS_kill,r0
        move.q r12,r2
        move.q #RVM_PROCESS_SIGNAL,r3
        syscall
        bcs rvm_process_failure
        move.q #SYS___nanosleep50,r0
        move.q #0x500100,r2
        rclr.q r3
        syscall
        move.q #SYS_exit,r0
        move.q #42,r2
        syscall
.Lfatal_child:
        cmp.q #1,r17
        bne .Lfault_memory
        rte
.Lfault_memory:
        cmp.q #2,r17
        bne .Lfault_text
        move.q #0xffff800001000000,r2
        move.q (r2),r0
.Lfault_text:
        move.q #0x400000,r2
        move.q #0,(r2)
rvm_process_failure:
        move.q #SYS_exit,r0
        move.q #99,r2
        syscall
        jmp rvm_process_failure
        xdef rvm_process_signal_handler
rvm_process_signal_handler:
        cmp.q #RVM_PROCESS_SIGNAL,r2
        bne rvm_process_failure
        cmp.q #0x700000,r63
        bcs rvm_process_failure
        cmp.q #0x710000,r63
        bcc rvm_process_failure
        cmp.q r10,r4
        bne rvm_process_failure
        move.q #0x500210,r2
        move.b #1,(r2)
        rts
        xdef rvm_process_signal_trampoline
rvm_process_signal_trampoline:
        move.q r10,r2
        move.q #SYS_setcontext,r0
        syscall
        jmp rvm_process_failure
rvm_process_end:
        /* Kernel helpers, outside the bytes copied into user space. Keep
         * relocations on the actual symbols; add the user VA base only
         * after subtracting the kernel source address. r0/r1 are scratch. */
        xdef rvm_process_handler_address
rvm_process_handler_address:
        move.q #rvm_process_signal_handler,r0
        move.q #rvm_process_code,r1
        rsub.q r1,r0,r0
        add.q #0x400000,r0
        rts
        xdef rvm_process_trampoline_address
rvm_process_trampoline_address:
        move.q #rvm_process_signal_trampoline,r0
        move.q #rvm_process_code,r1
        rsub.q r1,r0,r0
        add.q #0x400000,r0
        rts
        section ".rodata",data_ro
        xdef rvm_process_message
        xdef rvm_process_message_end
rvm_process_message:
        dc.b RVM_PROCESS_MESSAGE,10
rvm_process_message_end:
