/* The linker collects this byte last in output BSS. The native brk veneers
 * must start after every loaded section, including read-only data and BSS. */
        .section .rvm_end
        .globl _end
        .globl __rvm_end
_end:
__rvm_end:
        .byte 0
