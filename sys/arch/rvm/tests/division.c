/* Keep the dividend live while quotient and memory-divisor outputs differ. */
#include <sys/stdint.h>
const char div_ok[] = "RVM signed/unsigned division input constraints verified.\n";
const char div_bad[] = "RVM scalar division regression.\n";
#define FN __attribute__((noinline,noclone))
static FN long sd(long n, long *p) { long q = n / 1000; *p = n; return q; }
static FN unsigned long ud(unsigned long n, unsigned long *p) { unsigned long q = n / 1000; *p = n; return q; }
static FN int si(int n, int *p) { int q = n / 1000; *p = n; return q; }
static FN unsigned int ui(unsigned int n, unsigned int *p) { unsigned int q = n / 1000; *p = n; return q; }
static FN long sm(long n, volatile long *d, long *p) { long q = n / *d; *p = n; return q; }
static FN unsigned long um(unsigned long n, volatile unsigned long *d, unsigned long *p) { unsigned long q = n / *d; *p = n; return q; }
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(div_bad); rvm_boot_stop(); } } while (0)
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	long s; unsigned long u; int s32; unsigned int u32;
	volatile long ds = 1000; volatile unsigned long du = 1000;
	rvm_normal_early_init(bi);
	REQUIRE(sd(19999999, &s) == 19999 && s == 19999999);
	REQUIRE(sd(-19999999, &s) == -19999 && s == -19999999);
	REQUIRE(sd(INT64_MIN, &s) == -9223372036854775L && s == INT64_MIN);
	REQUIRE(ud(UINT64_MAX, &u) == 18446744073709551UL && u == UINT64_MAX);
	REQUIRE(ud(19999999, &u) == 19999 && u == 19999999);
	REQUIRE(si(INT32_MIN, &s32) == -2147483 && s32 == INT32_MIN);
	REQUIRE(ui(UINT32_MAX, &u32) == 4294967 && u32 == UINT32_MAX);
	REQUIRE(sm(-19999999, &ds, &s) == -19999 && s == -19999999);
	REQUIRE(um(UINT64_MAX, &du, &u) == 18446744073709551UL && u == UINT64_MAX);
	rvm_early_puts(div_ok);
}
