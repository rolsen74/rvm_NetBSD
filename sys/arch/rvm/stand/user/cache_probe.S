/* Position-independent instruction bytes for guest cache-coherence checks. */
        .text
        .globl rvm_cache_code_a
        .globl rvm_cache_code_a_end
rvm_cache_code_a:
        move.q #29,r0
        rts
rvm_cache_code_a_end:
        .globl rvm_cache_code_b
        .globl rvm_cache_code_b_end
rvm_cache_code_b:
        move.q #43,r0
        rts
rvm_cache_code_b_end:
