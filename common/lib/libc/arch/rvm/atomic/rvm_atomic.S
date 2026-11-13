/* Restartable compare-and-swap for the static, single-LWP RVM userland.
 * R2/R3/R4 are the address/expected/desired arguments. Within each range,
 * only the final store has side effects; a restart reloads the old value.
 * libc registers every range before invoking any atomic operation.
 */
#include <machine/asm.h>
ENTRY(_atomic_cas_8)
        clr.q r0
        .globl _atomic_cas_8_ras_start
_atomic_cas_8_ras_start:
        move.b (r2),r0
        cmp.b r3,r0
        bne _atomic_cas_8_ras_end
        move.b r4,(r2)
        .globl _atomic_cas_8_ras_end
_atomic_cas_8_ras_end:
        rts
WEAK_ALIAS(atomic_cas_8,_atomic_cas_8)
WEAK_ALIAS(atomic_cas_8_ni,_atomic_cas_8)
STRONG_ALIAS(_atomic_cas_8_ni,_atomic_cas_8)
ENTRY(_atomic_cas_16)
        clr.q r0
        .globl _atomic_cas_16_ras_start
_atomic_cas_16_ras_start:
        move.w (r2),r0
        cmp.w r3,r0
        bne _atomic_cas_16_ras_end
        move.w r4,(r2)
        .globl _atomic_cas_16_ras_end
_atomic_cas_16_ras_end:
        rts
WEAK_ALIAS(atomic_cas_16,_atomic_cas_16)
WEAK_ALIAS(atomic_cas_16_ni,_atomic_cas_16)
STRONG_ALIAS(_atomic_cas_16_ni,_atomic_cas_16)
ENTRY(_atomic_cas_32)
        clr.q r0
        .globl _atomic_cas_32_ras_start
_atomic_cas_32_ras_start:
        move.l (r2),r0
        cmp.l r3,r0
        bne _atomic_cas_32_ras_end
        move.l r4,(r2)
        .globl _atomic_cas_32_ras_end
_atomic_cas_32_ras_end:
        rts
WEAK_ALIAS(atomic_cas_32,_atomic_cas_32)
WEAK_ALIAS(atomic_cas_32_ni,_atomic_cas_32)
STRONG_ALIAS(_atomic_cas_32_ni,_atomic_cas_32)
STRONG_ALIAS(__sync_val_compare_and_swap_4,_atomic_cas_32)
WEAK_ALIAS(atomic_cas_uint,_atomic_cas_32)
WEAK_ALIAS(atomic_cas_uint_ni,_atomic_cas_32)
STRONG_ALIAS(_atomic_cas_uint,_atomic_cas_32)
STRONG_ALIAS(_atomic_cas_uint_ni,_atomic_cas_32)
ENTRY(_atomic_cas_64)
        clr.q r0
        .globl _atomic_cas_64_ras_start
_atomic_cas_64_ras_start:
        move.q (r2),r0
        cmp.q r3,r0
        bne _atomic_cas_64_ras_end
        move.q r4,(r2)
        .globl _atomic_cas_64_ras_end
_atomic_cas_64_ras_end:
        rts
WEAK_ALIAS(atomic_cas_64,_atomic_cas_64)
WEAK_ALIAS(atomic_cas_64_ni,_atomic_cas_64)
STRONG_ALIAS(_atomic_cas_64_ni,_atomic_cas_64)
STRONG_ALIAS(__sync_val_compare_and_swap_8,_atomic_cas_64)
WEAK_ALIAS(atomic_cas_ulong,_atomic_cas_64)
WEAK_ALIAS(atomic_cas_ulong_ni,_atomic_cas_64)
STRONG_ALIAS(_atomic_cas_ulong,_atomic_cas_64)
STRONG_ALIAS(_atomic_cas_ulong_ni,_atomic_cas_64)
WEAK_ALIAS(atomic_cas_ptr,_atomic_cas_64)
WEAK_ALIAS(atomic_cas_ptr_ni,_atomic_cas_64)
STRONG_ALIAS(_atomic_cas_ptr,_atomic_cas_64)
STRONG_ALIAS(_atomic_cas_ptr_ni,_atomic_cas_64)
