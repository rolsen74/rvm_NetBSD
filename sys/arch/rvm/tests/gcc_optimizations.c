/* Compile separately from the driver to keep inputs unknown to GCC. */
extern int yes(unsigned), no(unsigned);
extern int getvalue(void);

int mask(unsigned x) { if (x & 511) return yes(x); return no(x); }
int mask2(unsigned x) { if (x & 2) return yes(x); return no(x); }
int move_branch(void)
{ int x = getvalue(); if (x) return yes(x); return no(x); }
int loaded(unsigned *p) { if (*p) return yes(*p); return no(*p); }
int loaded_mask(unsigned *p) { if (*p & 256) return yes(*p); return no(*p); }
int mask_only(unsigned *p) { if (*p & 256) return yes(0); return no(0); }
int loaded_compare(unsigned *p, unsigned x)
{ if (*p >= x) return yes(x); return no(x); }
int signed_compare(int *p, int x)
{ if (*p >= x) return yes(x); return no(x); }
int signed_negative(int x) { if (x < 0) return yes(x); return no(x); }
unsigned long wide(unsigned *p) { return *p; }
unsigned long wide8(unsigned char *p) { return *p; }
unsigned long wide16(unsigned short *p) { return *p; }
long signed_wide(int *p) { return *p; }
unsigned narrow(unsigned *p) { return *p; }
int leaf(void) { return -1; }
unsigned long limit(unsigned long x, unsigned long *p)
{ if (x > 256) { *p = 256; x = 256; } return x; }
extern unsigned long sink(unsigned long, unsigned long, unsigned long);
unsigned long zeros(void) { return sink(0, 0, 0); }
long divides(long a, long b, long *r) { *r = a % b; return a / b; }
unsigned long udivides(unsigned long a, unsigned long b, unsigned long *r)
{ *r = a % b; return a / b; }
int divides32(int a, int b, int *r) { *r = a % b; return a / b; }
unsigned udivides32(unsigned a, unsigned b, unsigned *r)
{ *r = a % b; return a / b; }
unsigned long stack_args(unsigned long a, unsigned long b, unsigned long c,
    unsigned long d, unsigned long e, unsigned long f, unsigned long g,
    unsigned long h, unsigned long i, unsigned long j)
{ return a + b + c + d + e + f + g + h + i + j; }
unsigned long local_frame(unsigned long x)
{ volatile unsigned long a[3]; a[0] = x; a[2] = x + 7; return a[0] + a[2]; }
