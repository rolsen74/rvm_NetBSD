/* Both halves of the R0/R1 return pair must remain legal during reload. */
#define NOINLINE __attribute__((noinline, noclone))
typedef unsigned __int128 wide;
typedef unsigned long word;

NOINLINE wide wide_make(word high, word low)
{
    return ((wide)high << 64) | low;
}

NOINLINE wide wide_flip(wide value)
{
    return value ^ (((wide)0x123456789abcdef0UL << 64) |
        0xfedcba9876543210UL);
}

NOINLINE void wide_store(wide *out, word high, word low)
{
    *out = wide_make(high, low);
}

int wide_return_checks(void)
{
    word highs[] = { 0, 1, 0x8000000000000000UL, ~0UL };
    word lows[] = { ~0UL, 0x123456789abcdef0UL, 0, 1 };
    for (unsigned i = 0; i < 4; ++i) {
        wide value = wide_make(highs[i], lows[i]);
        if ((word)(value >> 64) != highs[i] || (word)value != lows[i])
            return 1;
        if (wide_flip(wide_flip(value)) != value)
            return 2;
        wide stored;
        wide_store(&stored, highs[i], lows[i]);
        if (stored != value)
            return 3;
    }
    return 0;
}
