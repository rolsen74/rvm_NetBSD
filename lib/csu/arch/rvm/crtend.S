/* Last data byte in the static image; the native brk veneers start here. */
        .data
        .globl _end
        .globl __rvm_end
_end:
__rvm_end:
        .byte 0
