/* Frame layout checks without dependencies on previously built kernel objects. */
#define NOINLINE __attribute__((noinline, noclone))
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_copy(to, from) __builtin_va_copy(to, from)
#define va_end(ap) __builtin_va_end(ap)

static NOINLINE long
many(long a, long b, long c, long d, long e, long f,
    long g, long h, long i, long j, long k, long l)
{
    return a + b*2 + c*3 + d*4 + e*5 + f*6 + g*7 + h*8 + i*9 + j*10 + k*11 + l*12;
}

static NOINLINE void
escape(void *p)
{
    __asm__ volatile("" : : "m"(*(volatile unsigned char *)p) : "memory");
}

static NOINLINE int
narrow(_Bool a, signed char b, unsigned char c, short d, unsigned short e,
    int f, unsigned int g, long h, _Bool i, short j, long k)
{
    escape(&a); escape(&b); escape(&c); escape(&d); escape(&e); escape(&f);
    escape(&g); escape(&h); escape(&i); escape(&j); escape(&k);
    return a && b == -7 && c == 240 && d == -1234 && e == 60000 &&
        f == -300000 && g == 4000000000U && h == -0x123456789L &&
        i && j == -2222 && k == 0x123456789abcdefL;
}

static NOINLINE long
sum(int count, ...)
{
    va_list ap;
    long total = 0;
    va_start(ap, count);
    for (int n = 0; n < count; n++) total += va_arg(ap, long) * (n + 1);
    va_end(ap);
    return total;
}

static NOINLINE int
mixed(int marker, ...)
{
    va_list ap, copy;
    va_start(ap, marker);
    if (va_arg(ap, int) != -17 || va_arg(ap, unsigned long) != 0x123456789abcdefUL)
        return 0;
    va_copy(copy, ap);
    for (int i = 0; i < 12; i++) {
        int a = va_arg(ap, int), b = va_arg(copy, int);
        if (a != i*13 - 50 || a != b) return 0;
    }
    va_end(copy); va_end(ap);
    return marker == 42;
}

static NOINLINE long
nested(long start, ...)
{
    va_list ap;
    volatile long before[4] = { 11, 22, 33, 44 };
    va_start(ap, start);
    long a = va_arg(ap, long);
    long result = sum(12, 1L,2L,3L,4L,5L,6L,7L,8L,9L,10L,11L,12L);
    long b = va_arg(ap, long);
    va_end(ap);
    return before[0] == 11 && before[3] == 44 ? start + a + b + result : -1;
}

struct big { long a, b, c, d; };
static NOINLINE struct big
aggregate(struct big v, long extra)
{
    v.a += extra; v.b -= extra; v.c ^= extra; v.d += v.a;
    return v;
}

static NOINLINE long
dynamic(long a, long b, long c, long d, long e, long f, long g, long h, long i)
{
    (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
    unsigned long count = (a & 7) + 1;
    volatile long *p = __builtin_alloca(count * sizeof(*p));
    p[0] = b;
    p[count - 1] = i;
    long total = sum(7, 1L,2L,3L,4L,5L,6L,7L);
    return p[0] + p[count - 1] + total;
}

int
frame_abi_checks(void)
{
    volatile long canary[4] = { 0x123456789abcdefL, -1, 3, 99 };
    if (many(1,2,3,4,5,6,7,8,9,10,11,12) != 650) return 1;
    if (!narrow(1, -7, 240, -1234, 60000, -300000, 4000000000U,
        -0x123456789L, 1, -2222, 0x123456789abcdefL)) return 2;
    if (sum(0) != 0 || sum(1, -19L) != -19 ||
        sum(7, 1L,2L,3L,4L,5L,6L,7L) != 140 ||
        sum(12, 1L,2L,3L,4L,5L,6L,7L,8L,9L,10L,11L,12L) != 650) return 3;
    if (!mixed(42, -17, 0x123456789abcdefUL,
        -50,-37,-24,-11,2,15,28,41,54,67,80,93)) return 4;
    if (nested(10, 20L, 30L) != 710) return 5;
    struct big v = { 10, 20, 30, 40 }, w = aggregate(v, 7);
    if (w.a != 17 || w.b != 13 || w.c != 25 || w.d != 57 ||
        v.a != 10 || v.b != 20 || v.c != 30 || v.d != 40) return 6;
    if (canary[0] != 0x123456789abcdefL || canary[1] != -1 || canary[3] != 99) return 7;
    return dynamic(5, 11, 0, 0, 0, 0, 0, 0, 29) == 180 ? 0 : 8;
}
