/* Binary64 square root with the integer-register argument/result ABI. */
        .text
        .globl __ieee754_sqrt
__ieee754_sqrt:
        fpushm f0
        push.q r2
        fpopm f0
        fsqrt f0,f0
        fpushm f0
        pop.q r0
        fpopm f0
        rts
