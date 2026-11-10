/* CPU flag builtins: snapshots must precede compiler-generated flag changes. */
#ifndef __RVM_FLAG_BUILTINS__
/* Keep --baseline usable with the preceding compiler. */
int flag_builtin_checks(void) { return 0; }
#else
#define FLAG_PAIR(name, bit) \
    if ((__builtin_rvm_set##name() & (1UL << (bit))) == 0) return 1 + (bit); \
    if ((__builtin_rvm_clr##name() & (1UL << (bit))) != 0) return 6 + (bit)

int
flag_builtin_checks(void)
{
    FLAG_PAIR(c, 0);
    FLAG_PAIR(v, 1);
    FLAG_PAIR(z, 2);
    FLAG_PAIR(n, 3);
    FLAG_PAIR(x, 4);
    unsigned long saved = __builtin_rvm_get_cr();
    unsigned long disabled = __builtin_rvm_clri();
    unsigned long enabled = __builtin_rvm_seti();
    __asm__ volatile ("mtctrl %0,cr" :: "r" (saved) : "memory");
    if ((disabled & 4) || !(enabled & 4) ||
        (disabled & ~4UL) != (saved & ~4UL) ||
        (enabled & ~4UL) != (saved & ~4UL)) return 11;
    if (!(__builtin_rvm_get_sr() & 32)) return 12;
    return 0;
}

void flag_write_only(void)
{
    __builtin_rvm_setc();
    __builtin_rvm_clrc();
    __builtin_rvm_setv();
    __builtin_rvm_clrv();
    __builtin_rvm_setz();
    __builtin_rvm_clrz();
    __builtin_rvm_setn();
    __builtin_rvm_clrn();
    __builtin_rvm_setx();
    __builtin_rvm_clrx();
    __builtin_rvm_clri();
    __builtin_rvm_seti();
}

unsigned long flag_snapshot(void) { return __builtin_rvm_setx(); }
/* Compile only: actually leaving supervisor mode needs a prepared user context. */
void flag_leave_supervisor(void) { __builtin_rvm_clrs(); }

#endif
