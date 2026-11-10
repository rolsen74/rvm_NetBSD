/* Compile separately from the driver to keep inputs unknown to GCC. */
extern int yes(unsigned), no(unsigned);
extern int getvalue(void);
extern unsigned long irq_enter(void);
extern void irq_leave(unsigned long);
extern unsigned long frame_home_callee(unsigned long, ...);
extern unsigned long frame_plain_callee(void);

void mask_add(volatile unsigned long *p, unsigned long n)
{ unsigned long saved = irq_enter(); *p += n; irq_leave(saved); }
unsigned long mask_cas(volatile unsigned long *p, unsigned long old, unsigned long value)
{ unsigned long saved = irq_enter(), found = *p;
  if (found == old) *p = value;
  irq_leave(saved); return found; }
unsigned long read_args(unsigned long a, unsigned long b) { return a + b; }
unsigned long call_only(void) { return frame_plain_callee(); }
unsigned long call_with_home(unsigned long value) { return frame_home_callee(value); }
unsigned long stack_only(unsigned long value)
{ volatile unsigned long slot = value; return slot; }
unsigned long stacked_after_call(unsigned long a, unsigned long b, unsigned long c,
    unsigned long d, unsigned long e, unsigned long f, unsigned long g,
    unsigned long h, unsigned long i)
{ (void)frame_home_callee(a); return i; }
void mask_asm(void)
{ __asm__ volatile("rclr.b r21\n\trclr.l r37" ::: "r21", "r37"); }

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

unsigned long cast_u8(unsigned long v) { return (unsigned char)v; }
unsigned long cast_u16(unsigned long v) { return (unsigned short)v; }
long cast_s8(long v) { return (signed char)v; }
long cast_s16(long v) { return (short)v; }
unsigned long cast_u8_chain(unsigned long v)
{ return (unsigned int)(unsigned short)(unsigned char)v; }
unsigned long cast_u16_chain(unsigned long v)
{ return (unsigned int)(unsigned short)v; }
unsigned long cast_s16_u32(long v) { return (unsigned int)(short)v; }
unsigned long cast_s8_u16(long v) { return (unsigned short)(signed char)v; }
long load_s8(signed char *p) { return *p; }
long load_s16(short *p) { return *p; }

/* This declaration intentionally has no prototype. */
extern unsigned long regcall_legacy();
unsigned long call_unprototyped(void) { return regcall_legacy(37UL); }

int return_pair_compare(void *context, const unsigned long *a,
    const unsigned long *b)
{
    (void)context;
    if (a[0] < b[0]) return -1;
    if (a[0] > b[0]) return 1;
    if (a[1] < b[1]) return -1;
    if (a[1] > b[1]) return 1;
    return 0;
}
int return_sign(int n)
{ if (n > 7) return 1; if (n < -7) return -1; return 0; }
void return_scratch(unsigned long *p, unsigned long a, unsigned long b)
{ *p = (a ^ b) + a; }
unsigned long return_live(unsigned long a, unsigned long b)
{ unsigned long value = a + b; return value + frame_plain_callee(); }
