/* Exercise the public fenv API and the real NetBSD binary64 math routines. */
#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <machine/fpu.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fesetenv(&initial); \
    fprintf(stderr, "math/fenv failure at line %d\n", __LINE__); return 164; } } while (0)

static uint64_t
bits(double d)
{
    union { double d; uint64_t u; } v = { d };
    return v.u;
}

static int
near(double a, double b)
{
    return fabs(a-b) < 1e-12;
}

int math_check(void);
int
math_check(void)
{
#ifdef RVM_SOFTWARE_FLOAT_RUNTIME
    /* compiler-rt's comparison runtime has fixed nearest-even arithmetic
     * and does not report exceptions to FPSR.  The hardware fenv controls
     * and gdtoa's explicit directed decimal rounding remain available.
     */
    const int native = 0;
#else
    const int native = 1;
#endif
    fenv_t initial, env, held, after, invalid;
    fexcept_t flags;
    struct fpreg bank, observed;
    char text[32], *end;
    int exponent;
    double integer;
    CHECK(fegetenv(&initial) == 0);
    CHECK(fesetenv(FE_DFL_ENV) == 0 && fegetround() == FE_TONEAREST &&
        fetestexcept(FE_ALL_EXCEPT) == 0);
    rvm_fpu_save(&bank);
    bank.f_fpsr = 0xa00; /* Comparison bits must survive exception operations. */
    rvm_fpu_restore(&bank);
    for (int i=1; i<=FE_INVALID; i<<=1) {
        CHECK(feraiseexcept(i) == 0 && fetestexcept(FE_ALL_EXCEPT) == ((i<<1)-1));
    }
    CHECK(fegetexceptflag(&flags, FE_INVALID | FE_INEXACT) == 0 &&
        flags == (FE_INVALID | FE_INEXACT));
    CHECK(feclearexcept(FE_INEXACT | FE_DIVBYZERO) == 0 &&
        fetestexcept(FE_ALL_EXCEPT) == (FE_INVALID | FE_UNDERFLOW | FE_OVERFLOW));
    CHECK(fesetexceptflag(&flags, FE_INEXACT | FE_UNDERFLOW) == 0 &&
        fetestexcept(FE_ALL_EXCEPT) == (FE_INVALID | FE_INEXACT | FE_OVERFLOW));
    rvm_fpu_save(&observed);
    CHECK((observed.f_fpsr & RVM_FPSR_COMPARE_MASK) == 0xa00 &&
        memcmp(bank.f_regs, observed.f_regs, sizeof(bank.f_regs)) == 0);
    CHECK(fegetenv(&env) == 0 && feholdexcept(&held) == 0 &&
        memcmp(&held, &env, sizeof(env)) == 0 && fetestexcept(FE_ALL_EXCEPT) == 0);
    CHECK(feraiseexcept(FE_DIVBYZERO) == 0 && feupdateenv(&held) == 0 &&
        fetestexcept(FE_ALL_EXCEPT) == (FE_INVALID | FE_INEXACT | FE_OVERFLOW | FE_DIVBYZERO));
    CHECK(fegetenv(&env) == 0);
    invalid = env; invalid.__fpcr = 4;
    CHECK(fesetenv(&invalid) != 0 && feupdateenv(&invalid) != 0);
    invalid = env; invalid.__fpsr |= 0x20;
    CHECK(fesetenv(&invalid) != 0 && fegetenv(&after) == 0 &&
        memcmp(&env, &after, sizeof(env)) == 0);
    CHECK(fesetround(-1) != 0 && fesetround(4) != 0 && fegetround() == FE_TONEAREST);
    CHECK(fegetexcept() == 0 && feenableexcept(FE_INVALID) == -1 &&
        feenableexcept(0) == 0 && fedisableexcept(FE_ALL_EXCEPT) == 0);

    for (int rounding=0; rounding<4; rounding++) {
        CHECK(fesetenv(FE_DFL_ENV) == 0 && fesetround(rounding) == 0 &&
            fegetround() == rounding);
        double positive = native && rounding == FE_UPWARD ? 2.0 : 1.0;
        double negative = native && rounding == FE_DOWNWARD ? -2.0 : -1.0;
        CHECK(rint(1.25) == positive && rint(-1.25) == negative);
        CHECK(lrint(1.25) == (long)positive && llrint(-1.25) == (long long)negative &&
            lrintl(1.25L) == (long)positive && llrintl(-1.25L) == (long long)negative);
        CHECK(!native || fetestexcept(FE_INEXACT) == FE_INEXACT);
        CHECK(feclearexcept(FE_ALL_EXCEPT) == 0 && feraiseexcept(FE_DIVBYZERO) == 0);
        CHECK(nearbyint(1.25) == positive && nearbyintl(-1.25L) == negative &&
            fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
        CHECK(snprintf(text, sizeof(text), "%.1f", 1.25) == 3 &&
            strcmp(text, rounding == FE_UPWARD ? "1.3" : "1.2") == 0);
        CHECK(snprintf(text, sizeof(text), "%.1f", -1.25) == 4 &&
            strcmp(text, rounding == FE_DOWNWARD ? "-1.3" : "-1.2") == 0);
        double parsed = strtod("1.00000000000000011102230246251565404236316680908203125", &end);
        CHECK(*end == 0 && bits(parsed) == UINT64_C(0x3ff0000000000000) +
            (rounding == FE_UPWARD));
        union { float f; uint32_t u; } single;
        single.f = strtof("1.000000059604644775390625", &end);
        CHECK(*end == 0 && single.u == 0x3f800000U + (rounding == FE_UPWARD));
    }
    CHECK(fesetenv(FE_DFL_ENV) == 0);
    CHECK(lrint(0x1p62) == INT64_C(0x4000000000000000) &&
        llrint(-0x1p63) == INT64_MIN && fetestexcept(FE_ALL_EXCEPT) == 0);
    if (native) {
        CHECK(lrint(0x1p63) == INT64_MIN && fetestexcept(FE_ALL_EXCEPT) == FE_INVALID);
        CHECK(feclearexcept(FE_ALL_EXCEPT) == 0 && llrint(-1.25) == -1 &&
            fetestexcept(FE_ALL_EXCEPT) == FE_INEXACT);
    }
    CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    CHECK(sqrt(4.0) == 2.0 && bits(sqrt(-0.0)) == UINT64_C(0x8000000000000000));
    CHECK(floor(-1.25) == -2.0 && ceil(-1.25) == -1.0 && trunc(-1.75) == -1.0 &&
        round(1.5) == 2.0 && round(-1.5) == -2.0);
    CHECK(lround(1.5) == 2 && llround(-1.5) == -2);
    CHECK(isnan(nan("")) && isnan(nanf("123")) && isnan(nanl("456")));
    if (native) {
        CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
        CHECK(fma(0x1.0000002p0, 0x1.ffffffcp-1, -1.0) == -0x1p-54 &&
            fmal(0x1.0000002p0L, 0x1.ffffffcp-1L, -1.0L) == -0x1p-54L);
        CHECK(fma(DBL_MAX, 2.0, -DBL_MAX) == DBL_MAX &&
            fetestexcept(FE_OVERFLOW | FE_INVALID) == 0);
        for (int rounding=0; rounding<4; rounding++) {
            CHECK(fesetround(rounding) == 0 &&
                bits(fma(1.0, 1.0, 0x1p-53)) == UINT64_C(0x3ff0000000000000) +
                    (rounding == FE_UPWARD) && fegetround() == rounding);
        }
        CHECK(fesetenv(FE_DFL_ENV) == 0);
    }
    CHECK(frexp(12.0, &exponent) == 0.75 && exponent == 4 && ldexp(0.75, 4) == 12.0);
    CHECK(modf(-1.25, &integer) == -0.25 && integer == -1.0);
    CHECK(fmod(7.0, 2.0) == 1.0 && remainder(7.0, 2.0) == -1.0);
    int quotient;
    CHECK(remquo(7.0, 2.0, &quotient) == -1.0 && (quotient & 7) == 4);
    CHECK(near(sin(0.5), 0.479425538604203) && near(cos(0.5), 0.877582561890373) &&
        near(tan(0.5), 0.546302489843790));
    CHECK(near(asin(0.5), 0.523598775598299) && near(acos(0.5), 1.047197551196598) &&
        near(atan(1.0), 0.785398163397448) && near(atan2(1.0, -1.0), 2.356194490192345));
    CHECK(near(exp(1.0), 2.718281828459045) && near(log(2.0), 0.693147180559945) &&
        near(log10(100.0), 2.0) && near(log2(8.0), 3.0) && pow(2.0, 10.0) == 1024.0);
    CHECK(near(log1p(0.5), 0.405465108108164) && near(expm1(0.5), 0.648721270700128) &&
        hypot(3.0, 4.0) == 5.0 && near(cbrt(27.0), 3.0));
    CHECK(near(sinh(0.5), 0.521095305493747) && near(cosh(0.5), 1.127625965206381) &&
        near(tanh(0.5), 0.462117157260010));
    CHECK(ilogb(8.0) == 3 && logb(8.0) == 3.0 &&
        bits(nextafter(1.0, 2.0)) == UINT64_C(0x3ff0000000000001));
    errno = 0;
    CHECK(feclearexcept(FE_ALL_EXCEPT) == 0 && isnan(sqrt(-1.0)) &&
        errno == EDOM && (!native || fetestexcept(FE_INVALID) == FE_INVALID));
    errno = 0;
    CHECK(isinf(log(0.0)) && errno == ERANGE &&
        (!native || fetestexcept(FE_DIVBYZERO) == FE_DIVBYZERO));
    errno = 0;
    CHECK(isinf(exp(1000.0)) && errno == ERANGE &&
        (!native || fetestexcept(FE_OVERFLOW) == FE_OVERFLOW));
    rvm_fpu_save(&observed);
    CHECK(memcmp(bank.f_regs, observed.f_regs, sizeof(bank.f_regs)) == 0);
    CHECK(fesetenv(&initial) == 0);
    if (native)
        puts("NetBSD/RVM: fenv API, directed decimal conversion and binary64 libm verified");
    else
        puts("NetBSD/RVM: fenv controls, directed decimal conversion and software libm verified");
    return 0;
}
