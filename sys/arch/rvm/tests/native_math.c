/* Additional public interfaces from the normal libm catalog and complex ABI. */
#include <complex.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

int math_check(void);
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "native math failure at line %d\n", __LINE__); return 1; \
} } while (0)

static int
near(double a, double b)
{
    return fabs(a-b) < 1e-12;
}

int
main(void)
{
    int result = math_check();
    if (result)
        return result;
    CHECK(fesetenv(FE_DFL_ENV) == 0);
    union { float value; uint32_t bits; } fused;
    fused.value = fmaf(0x1.000002p0f, 0x1.fffffcp-1f, -1.0f);
    CHECK(fused.bits == UINT32_C(0xa8800000)); /* exact -2^-46 */
    CHECK(sqrtf(9.0f) == 3.0f && hypotf(3.0f, 4.0f) == 5.0f);
    CHECK(fabs((double)sinf(0.5f) - 0.479425538604203) < 1e-6);
    double complex z = 3.0 + 4.0*I;
    CHECK(cabs(z) == 5.0 && creal(conj(z)) == 3.0 && cimag(conj(z)) == -4.0);
    z = csqrt(-3.0 + 4.0*I);
    CHECK(near(creal(z), 1.0) && near(cimag(z), 2.0));
    z = cexp(0.5 + 0.25*I);
    CHECK(near(creal(z), exp(0.5)*cos(0.25)) &&
          near(cimag(z), exp(0.5)*sin(0.25)));
    volatile double complex input = 3.0 + 4.0*I;
    z = input*input;
    CHECK(creal(z) == -7.0 && cimag(z) == 24.0);
    puts("RVM normal libm: binary32 FMA, elementary functions and complex argument/result ABI verified");
    return 0;
}
