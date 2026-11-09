/* All four public types use binary64 input and a signed 64-bit result. */
        .text
        .globl RVM_ENTRY
RVM_ENTRY:
        fpushm f0
        push.q r2
        fpopm f0
        rfstore.q f0,r0
        fpopm f0
        rts
