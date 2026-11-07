/* Runtime checks for argument homes, stacked arguments and stdarg. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <machine/boot.h>
#include <machine/psl.h>
#include <machine/bootsched.h>
#include <uvm/uvm.h>
#include <sys/stdarg.h>
#define NOINLINE __attribute__((noinline, noclone))
const char abi_bad[] = "RVM argument ABI regression at line 0x";
const char abi_ok[] = "RVM stacked arguments, scalar homes, stdarg and va_copy verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(abi_bad); rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static NOINLINE long many(long a, long b, long c, long d, long e, long f,
    long g, long h, long i, long j, long k, long l)
{
	return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 +
	    h * 8 + i * 9 + j * 10 + k * 11 + l * 12;
}
static NOINLINE void escape(void *p)
{ __asm volatile("" : : "m"(*(volatile unsigned char *)p) : "memory"); }
static NOINLINE int narrow(bool a, signed char b, unsigned char c, short d,
    unsigned short e, int f, unsigned int g, long h, bool i, short j, long k)
{
	escape(&a); escape(&b); escape(&c); escape(&d); escape(&e);
	escape(&f); escape(&g); escape(&h); escape(&i); escape(&j); escape(&k);
	return a && b == -7 && c == 240 && d == -1234 && e == 60000 &&
	    f == -300000 && g == 4000000000U && h == -0x123456789L &&
	    i && j == -2222 && k == 0x123456789abcdefL;
}
static NOINLINE long sum(int count, ...)
{
	va_list ap; long total = 0; int n;
	va_start(ap, count);
	for (n = 0; n < count; n++) total += va_arg(ap, long) * (n + 1);
	va_end(ap); return total;
}
static NOINLINE int mixed(int marker, ...)
{
	va_list ap, copy;
	const char *p;
	int i, a, b;
	va_start(ap, marker);
	REQUIRE(va_arg(ap, int) == -17);
	p = va_arg(ap, const char *); REQUIRE(p == abi_ok);
	REQUIRE(va_arg(ap, unsigned long) == 0x123456789abcdefUL);
	va_copy(copy, ap);
	for (i = 0; i < 12; i++) {
		a = va_arg(ap, int); b = va_arg(copy, int);
		REQUIRE(a == i * 13 - 50 && b == a);
	}
	va_end(copy); va_end(ap); return marker == 42;
}
static NOINLINE long nested(long start, ...)
{
	va_list ap; volatile long before[4] = { 11, 22, 33, 44 };
	long a, b, result;
	va_start(ap, start); a = va_arg(ap, long);
	result = sum(12, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L, 11L, 12L);
	b = va_arg(ap, long); va_end(ap);
	REQUIRE(before[0] == 11 && before[3] == 44);
	return start + a + b + result;
}
struct big { long a, b, c, d; };
static NOINLINE struct big aggregate(struct big v, long extra)
{
	v.a += extra; v.b -= extra; v.c ^= extra; v.d += v.a;
	return v;
}
static void check_abi(void)
{
	volatile long canary[4] = { 0x123456789abcdefL, -1, 3, 99 };
	struct big v = { 10, 20, 30, 40 }, w;
	REQUIRE(many(1,2,3,4,5,6,7,8,9,10,11,12) == 650);
	REQUIRE(narrow(true, -7, 240, -1234, 60000, -300000, 4000000000U,
	    -0x123456789L, true, -2222, 0x123456789abcdefL));
	REQUIRE(sum(0) == 0 && sum(1, -19L) == -19);
	REQUIRE(sum(7, 1L,2L,3L,4L,5L,6L,7L) == 140);
	REQUIRE(sum(12, 1L,2L,3L,4L,5L,6L,7L,8L,9L,10L,11L,12L) == 650);
	REQUIRE(mixed(42, -17, abi_ok, 0x123456789abcdefUL,
	    -50,-37,-24,-11,2,15,28,41,54,67,80,93));
	REQUIRE(nested(10, 20L, 30L) == 710);
	w = aggregate(v, 7); REQUIRE(w.a == 17 && w.b == 13 && w.c == 25 && w.d == 57);
	REQUIRE(v.a == 10 && v.b == 20 && v.c == 30 && v.d == 40);
	REQUIRE(canary[0] == 0x123456789abcdefL && canary[1] == -1 && canary[3] == 99);
}
void rvm_early_init(const struct rvm_bootinfo *bi)
{
	rvm_normal_early_init(bi); REQUIRE(rvm_boot_stage == 32);
	check_abi(); rvm_early_puts(abi_ok); rvm_boot_stop();
}
