/* Compile with -mfpu: these operations must execute real v0000 instructions. */
#include <machine/fpu.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef __RVM_FPU_GPR_ABI__
#error This check requires native -mfpu compiler support
#endif
#define NOINLINE __attribute__((noinline,noclone))
static NOINLINE double add(double a, double b) { return a+b; }
static NOINLINE double sub(double a, double b) { return a-b; }
static NOINLINE double mul(double a, double b) { return a*b; }
static NOINLINE double divide(double a, double b) { return a/b; }
static NOINLINE double square(double a) { return a*a; }
static NOINLINE double absolute(double a) { return __builtin_fabs(a); }
static NOINLINE double root(double a) { return __builtin_sqrt(a); }
static uint64_t bits(double d) { union {double d;uint64_t u;} v = {d}; return v.u; }
static double value(uint64_t u) { union {uint64_t u;double d;} v = {u}; return v.d; }
static struct fpreg initial, bank, result;
#define CHECK(c) do { if (!(c)) { rvm_fpu_restore(&initial); fprintf(stderr,"native FPU failure at line %d\n",__LINE__); return 163; } } while(0)
static void reset(unsigned rounding) {
    bank.f_fpcr = rounding; bank.f_fpsr = 0x400;
    rvm_fpu_restore(&bank);
}
static int state(unsigned rounding, unsigned exceptions) {
    rvm_fpu_save(&result);
    return !memcmp(result.f_regs, bank.f_regs, sizeof(bank.f_regs)) &&
        result.f_fpcr == rounding && result.f_fpsr == (0x400 | exceptions);
}
int main(void) {
    rvm_fpu_save(&initial);
    for (unsigned i=0;i<RVM_NFPREG;i++) bank.f_regs[i]=UINT64_C(0x7ff8000012340000)|i;
    reset(0);
    volatile double a=7.5,b=2.0;
    CHECK(add(a,b)==9.5 && sub(a,b)==5.5 && mul(a,b)==15.0 && divide(a,b)==3.75);
    CHECK(square(3.0)==9.0 && root(4.0)==2.0 && absolute(-3.5)==3.5 && state(0,0));
    CHECK(bits(root(-0.0))==UINT64_C(0x8000000000000000) && bits(absolute(-0.0))==0);
    volatile double half=value(UINT64_C(0x3ca0000000000000)); /* 2^-53 */
    for (unsigned rounding=0;rounding<4;rounding++) {
        reset(rounding);
        uint64_t expected=UINT64_C(0x3ff0000000000000)+(rounding==3);
        CHECK(bits(add(1.0,half))==expected && state(rounding,1));
        reset(rounding);
        expected=UINT64_C(0xbff0000000000000)+(rounding==2);
        CHECK(bits(sub(-1.0,half))==expected && state(rounding,1));
    }
    reset(0);
    CHECK(bits(divide(1.0,0.0))==UINT64_C(0x7ff0000000000000) && state(0,8));
    reset(0);
    CHECK((bits(root(-1.0))&UINT64_C(0x7fffffffffffffff))>UINT64_C(0x7ff0000000000000) && state(0,16));
    reset(0);
    CHECK(bits(mul(value(UINT64_C(0x7fefffffffffffff)),2.0))==UINT64_C(0x7ff0000000000000) && state(0,5));
    reset(0);
    CHECK(bits(divide(value(1),2.0))==0 && state(0,3));
    rvm_fpu_restore(&initial);
    puts("NetBSD/RVM: GCC native FPU arithmetic, sqrt/abs, rounding, exceptions and FP-register preservation verified");
    return 0;
}
