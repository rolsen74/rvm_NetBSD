/* Uncached kernel atomics. Preserve the caller's CR, including early boot.
 * No scheduler, LWP or allocator services may be used inside these operations.
 */
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/cpu.h>
#include <machine/asyncirq.h>
#include <machine/irq.h>

uint64_t rvm_atomic_exchange64(volatile uint64_t *, uint64_t);

#ifdef MULTIPROCESSOR
static volatile uint64_t rvm_atomic_gate;

static uint64_t
atomic_enter(void)
{
	uint64_t cr = rvm_intr_disable();
	curcpu()->ci_atomic_active = 1;
	/* No other CPU can enter kernel atomics until secondary startup. */
	if (rvm_cpu_count == 1)
		return cr;
	while (rvm_atomic_exchange64(&rvm_atomic_gate, 1) != 0)
		__asm volatile("" ::: "memory");
	__asm volatile("" ::: "memory");
	return cr | (UINT64_C(1) << 63);
}

static void
atomic_leave(uint64_t cr)
{
	__asm volatile("" ::: "memory");
	if (cr & (UINT64_C(1) << 63))
		(void)rvm_atomic_exchange64(&rvm_atomic_gate, 0);
	curcpu()->ci_atomic_active = 0;
	rvm_irq_set_cr(cr & ~(UINT64_C(1) << 63));
}
#else
static uint64_t atomic_enter(void) { return rvm_intr_disable(); }
static void atomic_leave(uint64_t cr) { rvm_irq_set_cr(cr); }
#endif

#define UPDATE(name, type, op, opname) \
type atomic_##opname##_##name##_nv(volatile type *p, type n) \
{ uint64_t cr = atomic_enter(); type v = *p op n; *p = v; \
  atomic_leave(cr); return v; } \
void atomic_##opname##_##name(volatile type *p, type n) \
{ (void)atomic_##opname##_##name##_nv(p, n); }

#define CAS(name, type) \
type atomic_cas_##name(volatile type *p, type old, type new) \
{ uint64_t cr = atomic_enter(); type v = *p; if (v == old) *p = new; \
  atomic_leave(cr); return v; } \
type atomic_cas_##name##_ni(volatile type *p, type old, type new) \
{ return atomic_cas_##name(p, old, new); } \
type atomic_swap_##name(volatile type *p, type new) \
{ uint64_t cr = atomic_enter(); type v = *p; *p = new; \
  atomic_leave(cr); return v; }

#define ADD(name, type, delta) \
type atomic_add_##name##_nv(volatile type *p, delta n) \
{ uint64_t cr = atomic_enter(); type v = *p + (type)n; *p = v; \
  atomic_leave(cr); return v; } \
void atomic_add_##name(volatile type *p, delta n) \
{ (void)atomic_add_##name##_nv(p, n); }

#define INCDEC(name, type) \
type atomic_inc_##name##_nv(volatile type *p) \
{ uint64_t cr = atomic_enter(); type v = *p + 1; *p = v; \
  atomic_leave(cr); return v; } \
type atomic_dec_##name##_nv(volatile type *p) \
{ uint64_t cr = atomic_enter(); type v = *p - 1; *p = v; \
  atomic_leave(cr); return v; } \
void atomic_inc_##name(volatile type *p) { (void)atomic_inc_##name##_nv(p); } \
void atomic_dec_##name(volatile type *p) { (void)atomic_dec_##name##_nv(p); }

#define INTEGER(name, type) \
UPDATE(name, type, &, and) \
UPDATE(name, type, |, or) \
CAS(name, type) \
INCDEC(name, type)

INTEGER(32, uint32_t)
INTEGER(64, uint64_t)
INTEGER(uint, unsigned int)
INTEGER(ulong, unsigned long)
ADD(32, uint32_t, int32_t)
ADD(64, uint64_t, int64_t)
ADD(int, unsigned int, int)
ADD(long, unsigned long, long)

void *
atomic_cas_ptr(volatile void *p, void *old, void *new)
{
	return (void *)atomic_cas_ulong((volatile unsigned long *)p,
	    (uintptr_t)old, (uintptr_t)new);
}
void *
atomic_cas_ptr_ni(volatile void *p, void *old, void *new)
{
	return atomic_cas_ptr(p, old, new);
}
void *
atomic_swap_ptr(volatile void *p, void *new)
{
	return (void *)atomic_swap_ulong((volatile unsigned long *)p, (uintptr_t)new);
}
void *
atomic_add_ptr_nv(volatile void *p, ssize_t n)
{
	return (void *)atomic_add_long_nv((volatile unsigned long *)p, n);
}
void
atomic_add_ptr(volatile void *p, ssize_t n)
{
	(void)atomic_add_ptr_nv(p, n);
}
void *atomic_inc_ptr_nv(volatile void *p) { return atomic_add_ptr_nv(p, 1); }
void *atomic_dec_ptr_nv(volatile void *p) { return atomic_add_ptr_nv(p, -1); }
void atomic_inc_ptr(volatile void *p) { (void)atomic_inc_ptr_nv(p); }
void atomic_dec_ptr(volatile void *p) { (void)atomic_dec_ptr_nv(p); }

uint16_t
atomic_cas_16(volatile uint16_t *p, uint16_t old, uint16_t new)
{
	uint64_t cr = atomic_enter();
	uint16_t v = *p;
	if (v == old) *p = new;
	atomic_leave(cr);
	return v;
}
uint8_t
atomic_cas_8(volatile uint8_t *p, uint8_t old, uint8_t new)
{
	uint64_t cr = atomic_enter();
	uint8_t v = *p;
	if (v == old) *p = new;
	atomic_leave(cr);
	return v;
}

/* Data caching is disabled; the exchange serializes atomic updates. */
void membar_sync(void) { __asm volatile("" ::: "memory"); }
void membar_acquire(void) { membar_sync(); }
void membar_release(void) { membar_sync(); }
void membar_producer(void) { membar_sync(); }
void membar_consumer(void) { membar_sync(); }
void membar_enter(void) { membar_sync(); }
void membar_exit(void) { membar_sync(); }
