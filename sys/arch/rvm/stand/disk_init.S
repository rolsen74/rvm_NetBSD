/* Minimal PID 1 for disk/exec bring-up, deliberately independent of libc.
 * It opens the real console, reads a file from FFS, and sleeps forever.
 * This is an integration fixture, not NetBSD's production init or a shell.
 */
#define _LOCORE
#include <sys/syscall.h>
#include <sys/errno.h>

        .text
        .globl _start
_start:
        sub.q #80,r63
        move.q #SYS_getpid,r0
        syscall
        cmp.q #1,r0
        bne failed
        move.q #SYS_open,r0
        lea console_path,r2
        move.q #1,r3
        rclr.q r4
        syscall
        bcs failed
        move.q r0,r12
        move.q #SYS_dup2,r0
        move.q r12,r2
        move.q #1,r3
        syscall
        bcs failed
        move.q #SYS_dup2,r0
        move.q r12,r2
        move.q #2,r3
        syscall
        bcs failed
        move.q #SYS_write,r0
        move.q r12,r2
        lea hello,r3
        lea hello_end,r4
        sub.q r3,r4
        syscall
        bcs failed
        move.q #SYS_open,r0
        lea motd_path,r2
        rclr.q r3
        rclr.q r4
        syscall
        bcs failed
        move.q r0,r13
read_loop:
        move.q #SYS_read,r0
        move.q r13,r2
        lea buffer,r3
        move.q #256,r4
        syscall
        bcs failed
        tst.q r0
        beq read_done
        move.q r0,r4
        move.q #SYS_write,r0
        move.q r12,r2
        lea buffer,r3
        syscall
        bcs failed
        jmp read_loop
read_done:
        move.q #SYS_close,r0
        move.q r13,r2
        syscall
        move.q #SYS_write,r0
        move.q r12,r2
        lea success,r3
        lea success_end,r4
        sub.q r3,r4
        syscall
        lea bad_paths,r16
bad_loop:
        move.q (r16)+,r2
        tst.q r2
        beq bad_done
        move.q #SYS_execve,r0
        lea init_argv,r3
        rclr.q r4
        syscall
        bcc failed
        cmp.q #ENOEXEC,r0
        bne failed
        jmp bad_loop
bad_done:
        move.q #SYS_write,r0
        move.q r12,r2
        lea rejected,r3
        lea rejected_end,r4
        sub.q r3,r4
        syscall
        move.q #SYS_fork,r0
        syscall
        bcs failed
        tst.q r0
        beq child_exec
        move.q r0,r14
        move.q #SYS___wait450,r0
        move.q r14,r2
        lea child_status,r3
        rclr.q r4
        rclr.q r5
        syscall
        bcs failed
        cmp.q r14,r0
        bne failed
        lea child_status,r2
        move.l (r2),r0
        cmp.l #9472,r0
        bne failed
        move.q #SYS_write,r0
        move.q r12,r2
        lea child_ok,r3
        lea child_ok_end,r4
        sub.q r3,r4
        syscall
sleep_loop:
        move.q #SYS___nanosleep50,r0
        lea sleep_time,r2
        rclr.q r3
        syscall
        jmp sleep_loop
child_exec:
        lea child_argument,r16
        move.q #70000,r17
fill_argument:
        move.b #65,(r16)+
        sub.q #1,r17
        bne fill_argument
        move.b #0,(r16)
        move.q #SYS_execve,r0
        lea child_path,r2
        lea child_argv,r3
        rclr.q r4
        syscall
        jmp failed
failed:
        /* A failed fixture must be visible to automated boot checks. */
        move.q r0,r2
        move.q #SYS_exit,r0
        syscall
        jmp failed

        .data
        .balign 8
        .globl console_path, motd_path, hello, success, sleep_time, buffer
        .globl hello_end, success_end
        .globl init_argv, bad_paths, child_path, child_argv, child_status
        .globl child_argument
        .globl rejected, rejected_end, child_ok, child_ok_end
console_path: .asciz "/dev/console"
motd_path: .asciz "/etc/motd"
hello: .ascii "NetBSD/RVM: PID 1 loaded from FHR on FFS; /dev/console opened\n"
hello_end:
success: .ascii "NetBSD/RVM: disk init read /etc/motd successfully; sleeping\n"
success_end:
rejected: .ascii "NetBSD/RVM: malformed FHR executables rejected before replacing PID 1\n"
rejected_end:
child_ok: .ascii "NetBSD/RVM: disk fork/exec/wait integration verified; PID 1 sleeping\n"
child_ok_end:
child_path: .asciz "/sbin/exec-script"
        .globl bad_header, bad_crc, bad_reloc, bad_size
bad_header: .asciz "/sbin/bad-header"
bad_crc: .asciz "/sbin/bad-crc"
bad_reloc: .asciz "/sbin/bad-reloc"
bad_size: .asciz "/sbin/bad-size"
        .balign 8
init_argv: .quad child_path, 0
child_argv: .quad child_path, child_argument, 0
bad_paths: .quad bad_header, bad_crc, bad_reloc, bad_size, 0
child_status: .long 0
        .balign 8
sleep_time: .quad 1, 0
buffer: .zero 256
child_argument: .zero 70001
