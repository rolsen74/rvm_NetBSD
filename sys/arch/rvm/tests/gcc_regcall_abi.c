/* Register-only calls, parameter spills, and conservative call boundaries. */
#define NOINLINE __attribute__((noinline, noclone))
typedef unsigned long word;

NOINLINE word regcall_eight(word a, word b, word c, word d,
    word e, word f, word g, word h)
{
    return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g + 8*h;
}

static NOINLINE word change(word *p)
{
    *p ^= 0x123456789abcdef0UL;
    return *p;
}

NOINLINE word regcall_address(word value)
{
    word result = change(&value);
    __asm__ volatile ("" : "+m" (value));
    return result == value ? value : 0;
}

NOINLINE word regcall_inc(word value) { return value + 1; }
NOINLINE word regcall_indirect(word (*fn)(word), word value) { return fn(value); }

NOINLINE word regcall_oldstyle(a,b,c,d,e,f,g,h)
    word a,b,c,d,e,f,g,h;
{
    return regcall_eight(a,b,c,d,e,f,g,h);
}

NOINLINE word regcall_legacy(value)
    word value;
{
    return change(&value);
}

NOINLINE int regcall_narrow(_Bool a, signed char b, unsigned char c, short d,
    unsigned short e, int f, unsigned g, long h)
{
    __asm__ volatile ("" : "+m"(a), "+m"(b), "+m"(c), "+m"(d),
        "+m"(e), "+m"(f), "+m"(g), "+m"(h));
    return a && b == -7 && c == 240 && d == -1234 && e == 60000 &&
        f == -300000 && g == 4000000000U && h == -0x123456789L;
}

NOINLINE void regcall_void_stacked(word *out, word a, word b, word c,
    word d, word e, word f, word g, word h, word i)
{
    *out = a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g + 8*h + 9*i;
}
