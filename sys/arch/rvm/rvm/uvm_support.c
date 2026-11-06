/*
 * Services for standalone MI pages and maps. One CPU, guarded state changes:
 * checked mutexes/rwlocks may not sleep or contend. spl* masks the clock,
 * and uncached UP barriers also support asynchronous timecounter readers.
 * Full kernels must replace this file with NetBSD's normal kernel services.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/condvar.h>
#include <sys/atomic.h>
#include <sys/xcall.h>
#include <sys/pool.h>
#include <sys/radixtree.h>
#include <sys/vnode.h>
#include <uvm/uvm.h>
#include <machine/boot.h>
#include <machine/pmap.h>
#include <machine/psl.h>
#include <machine/bootobject.h>
#include <machine/bootalloc.h>
#include <machine/bootanon.h>
#include <machine/asyncirq.h>

#define BOOT_MUTEX_MAGIC 0x52564d4cU
struct cpu_info cpu_info_primary __attribute__((section(".data"))) = { 0 };
static struct lwp bootstrap_lwp __attribute__((section(".data"))) = { 0 };
size_t coherency_unit __attribute__((section(".data"))) = COHERENCY_UNIT;
unsigned long rvm_uvm_daemon_requests __attribute__((section(".data"))) = 0;

const char rvm_bad_lock[] = "Invalid bootstrap lock/IPL operation";
const char rvm_no_object[] = "Object/anonymous page services are not available";
const char rvm_no_sleep[] = "Sleeping or dynamic kernel allocation is not available";
const char rvm_bad_page[] = "Invalid raw UVM page operation";
const char rvm_abort[] = "Unexpected early kernel abort";

/* Actual MI bodies, renamed only in the standalone compilation profile. */
struct vm_page *rvm_mi_pagealloc_strat(struct uvm_object *, voff_t,
    struct vm_anon *, int, int, int);
void rvm_mi_pagefree(struct vm_page *);

struct vm_page *
uvm_pagealloc_strat(struct uvm_object *obj, voff_t offset, struct vm_anon *anon,
    int flags, int strategy, int freelist)
{
	if ((anon != NULL && !rvm_bootanon_valid(anon)) ||
	    (obj != NULL && !rvm_bootobj_valid(obj)))
		panic(rvm_no_object);
	if ((anon != NULL && (obj != NULL || offset != 0 ||
	    (flags & UVM_FLAG_COLORMATCH) || !rvm_bootanon_canalloc(anon))) ||
	    (obj == NULL && (offset < 0 || (offset & PAGE_MASK) != 0 ||
	    (offset != 0 && !(flags & UVM_FLAG_COLORMATCH)) ||
	    (u_long)offset >= RVM_BOOTALLOC_SIZE)) ||
	    (obj != NULL && (!rvm_bootobj_offset(obj, offset) ||
	    (flags & UVM_FLAG_COLORMATCH) ||
	    !rw_write_held(obj->vmobjlock) || uvm_pagelookup(obj, offset) != NULL)) ||
	    strategy != UVM_PGA_STRAT_NORMAL ||
	    freelist != VM_FREELIST_DEFAULT ||
	    (flags & ~(UVM_PGA_ZERO | UVM_PGA_USERESERVE | UVM_FLAG_COLORMATCH)) != 0)
		panic(rvm_bad_page);
	return rvm_mi_pagealloc_strat(obj, offset, anon, flags, strategy, freelist);
}

void
uvm_pagefree(struct vm_page *page)
{
	if (page == NULL || (page->flags & PG_FREE) != 0 ||
	    (page->uanon != NULL && !rvm_bootanon_page_valid(page)) ||
	    page->loan_count != 0 ||
	    (page->uobject == NULL && page->wire_count != 0) ||
	    (page->uobject != NULL && (!rvm_bootobj_valid(page->uobject) ||
	    !rw_write_held(page->uobject->vmobjlock))) ||
	    (uvm.page_init_done && PHYS_TO_VM_PAGE(VM_PAGE_TO_PHYS(page)) != page))
		panic(rvm_bad_page);
	if (page->uanon != NULL)
		rvm_bootanon_page_protect(page, VM_PROT_NONE);
	rvm_mi_pagefree(page);
}

void
rvm_uvm_cpuinit(void)
{
	/* A real bootstrap LWP keeps NULL curlwp from matching the absent daemon.
	 * No scheduler, sleep queues or context switching are enabled by this.
	 */
	bootstrap_lwp.l_cpu = &cpu_info_primary;
	bootstrap_lwp.l_stat = LSONPROC;
	bootstrap_lwp.l_flag = LW_SYSTEM;
	cpu_info_primary.ci_curlwp = &bootstrap_lwp;
	cpu_info_primary.ci_onproc = &bootstrap_lwp;
}

const char rvm_bad_mode_pc[] = "IRQ-off service caller PC 0x";
void __dead
rvm_bad_mode(vaddr_t pc)
{
	rvm_early_puts(rvm_bad_mode_pc);
	rvm_early_puthex(pc);
	panic(rvm_bad_lock);
}
void rvm_check_mode(void);
#define check_mode rvm_check_mode

int
splraise(int ipl)
{
	int old;

	rvm_intr_disable();
	old = curcpu()->ci_cpl;
	check_mode();
	if (ipl < IPL_NONE || ipl > IPL_HIGH)
		panic(rvm_bad_lock);
	if (ipl > old)
		curcpu()->ci_cpl = ipl;
	rvm_intr_apply();
	return old;
}

void
splx(int ipl)
{
	rvm_intr_disable();
	check_mode();
	if (ipl < IPL_NONE || ipl > IPL_HIGH)
		panic(rvm_bad_lock);
	curcpu()->ci_cpl = ipl;
	rvm_intr_apply();
}

void
spl0(void)
{
	splx(IPL_NONE);
}

void
mutex_init(kmutex_t *lock, kmutex_type_t type, int ipl)
{
	unsigned int guard = rvm_intr_suspend();
	check_mode();
	if (type < MUTEX_SPIN || type > MUTEX_NODEBUG ||
	    ipl < IPL_NONE || ipl > IPL_HIGH)
		panic(rvm_bad_lock);
	lock->mtx_state = 0;
	lock->mtx_magic = BOOT_MUTEX_MAGIC;
	lock->mtx_ipl = ipl;
	lock->mtx_oldipl = IPL_NONE;
	rvm_intr_resume(guard);
}

void
mutex_enter(kmutex_t *lock)
{
	rvm_intr_disable();
	check_mode();
	if (lock->mtx_magic != BOOT_MUTEX_MAGIC || lock->mtx_state != 0)
		panic(rvm_bad_lock);
	if (curcpu()->ci_irq_locks == UINT_MAX) panic(rvm_bad_lock);
	curcpu()->ci_irq_locks++;
	lock->mtx_oldipl = splraise(lock->mtx_ipl);
	if (lock->mtx_ipl == IPL_NONE && curlwp == NULL) panic(rvm_bad_lock);
	lock->mtx_state = lock->mtx_ipl == IPL_NONE ? (uintptr_t)curlwp : 1;
}

void
mutex_exit(kmutex_t *lock)
{
	check_mode();
	if (lock->mtx_magic != BOOT_MUTEX_MAGIC ||
	    lock->mtx_state != (lock->mtx_ipl == IPL_NONE ? (uintptr_t)curlwp : 1) ||
	    curcpu()->ci_irq_locks == 0)
		panic(rvm_bad_lock);
	lock->mtx_state = 0;
	curcpu()->ci_irq_locks--;
	if (lock->mtx_ipl != IPL_NONE) splx(lock->mtx_oldipl);
	else rvm_intr_apply();
}

void mutex_spin_enter(kmutex_t *lock) { mutex_enter(lock); }
void mutex_spin_exit(kmutex_t *lock) { mutex_exit(lock); }

int
mutex_tryenter(kmutex_t *lock)
{
	rvm_intr_disable();
	check_mode();
	if (lock->mtx_magic != BOOT_MUTEX_MAGIC)
		panic(rvm_bad_lock);
	if (lock->mtx_state != 0) {
		rvm_intr_apply();
		return 0;
	}
	mutex_enter(lock);
	return 1;
}

int
mutex_owned(const kmutex_t *lock)
{
	unsigned int guard = rvm_intr_suspend();
	int result;
	check_mode();
	if (lock->mtx_magic != BOOT_MUTEX_MAGIC) panic(rvm_bad_lock);
	result = lock->mtx_state != 0 &&
	    (lock->mtx_ipl != IPL_NONE || lock->mtx_state == (uintptr_t)curlwp);
	rvm_intr_resume(guard);
	return result;
}

void
mutex_destroy(kmutex_t *lock)
{
	unsigned int guard = rvm_intr_suspend();
	check_mode();
	if (lock->mtx_magic != BOOT_MUTEX_MAGIC || lock->mtx_state != 0)
		panic(rvm_bad_lock);
	lock->mtx_magic = 0;
	rvm_intr_resume(guard);
}

size_t
strlcpy(char *to, const char *from, size_t size)
{
	size_t n = 0;
	while (from[n] != '\0') {
		if (n + 1 < size)
			to[n] = from[n];
		n++;
	}
	if (size != 0)
		to[n < size ? n : size - 1] = '\0';
	return n;
}

void
cpu_count(enum cpu_count which, int64_t delta)
{
	check_mode();
	if ((unsigned int)which >= CPU_COUNT_MAX)
		panic(rvm_bad_lock);
	curcpu()->ci_counts[which] += delta;
}

void
atomic_dec_uint(volatile unsigned int *value)
{
	check_mode();
	/* Interrupts remain disabled for the entire single-CPU bootstrap. */
	--*value;
}

void
atomic_inc_uint(volatile unsigned int *value)
{
	check_mode();
	++*value;
}

/* Public rwlock storage, checked for the same UP/IRQ-off bootstrap mode.
 * The upper bits identify initialized locks; low bits encode shared readers
 * or one writer. No contention, waiters or scheduler operations are possible.
 */
#define BOOT_RW_MAGIC 0x52564d5200000000UL
#define BOOT_RW_WRITER 0x80000000UL
#define BOOT_RW_READERS 0x7fffffffUL

static uintptr_t
rw_state(krwlock_t *lock)
{
	check_mode();
	if ((lock->rw_owner & ~0xffffffffUL) != BOOT_RW_MAGIC)
		panic(rvm_bad_lock);
	return lock->rw_owner & 0xffffffffUL;
}

void rw_init(krwlock_t *lock)
{ check_mode(); lock->rw_owner = BOOT_RW_MAGIC; }

int
rw_tryenter(krwlock_t *lock, const krw_t op)
{
	uintptr_t state = rw_state(lock);

	if (op != RW_READER && op != RW_WRITER)
		panic(rvm_bad_lock);
	if (op == RW_WRITER) {
		if (state != 0)
			return 0;
		lock->rw_owner |= BOOT_RW_WRITER;
	} else {
		if (state & BOOT_RW_WRITER)
			return 0;
		if (state == BOOT_RW_READERS)
			panic(rvm_bad_lock);
		lock->rw_owner++;
	}
	return 1;
}

void rw_enter(krwlock_t *lock, const krw_t op)
{ if (!rw_tryenter(lock, op)) panic(rvm_bad_lock); }

void
rw_exit(krwlock_t *lock)
{
	uintptr_t state = rw_state(lock);

	if (state == BOOT_RW_WRITER)
		lock->rw_owner = BOOT_RW_MAGIC;
	else if (state != 0 && !(state & BOOT_RW_WRITER))
		lock->rw_owner--;
	else
		panic(rvm_bad_lock);
}

int rw_read_held(krwlock_t *lock)
{ uintptr_t s = rw_state(lock); return s != 0 && !(s & BOOT_RW_WRITER); }
int rw_write_held(krwlock_t *lock)
{ return rw_state(lock) == BOOT_RW_WRITER; }
int rw_lock_held(krwlock_t *lock) { return rw_state(lock) != 0; }

void *
memcpy(void *destination, const void *source, size_t size)
{
	unsigned char *to = destination;
	const unsigned char *from = source;

	while (size-- != 0)
		*to++ = *from++;
	return destination;
}

void *
memset(void *destination, int value, size_t size)
{
	unsigned char *to = destination;

	while (size-- != 0)
		*to++ = value;
	return destination;
}

int hz __attribute__((section(".data"))) = 100;
void abort(void) { panic(rvm_abort); }

struct vm_amap *amap_alloc(vaddr_t size __unused, vaddr_t pad __unused,
    int flags __unused) { panic(rvm_no_object); }
int amap_extend(struct vm_map_entry *entry __unused, vsize_t size __unused,
    int flags __unused) { panic(rvm_no_object); }
void amap_splitref(struct vm_aref *a __unused, struct vm_aref *b __unused,
    vaddr_t offset __unused) { panic(rvm_no_object); }
void amap_unref(struct vm_amap *amap __unused, vaddr_t offset __unused,
    vsize_t size __unused, bool all __unused) { panic(rvm_no_object); }
void uvm_fault_unwire_locked(struct vm_map *map __unused,
    vaddr_t start __unused, vaddr_t end __unused) { panic(rvm_no_object); }

#undef bzero
void
bzero(void *memory, size_t size)
{
	volatile unsigned char *p = memory;

	while (size-- != 0)
		*p++ = 0;
}

void
uvm_kick_pdaemon(void)
{
	/* Record the request; no daemon exists yet. MI allocation returns NULL. */
	rvm_uvm_daemon_requests++;
}

void __dead
rvm_uvm_object_unavailable(void)
{
	panic(rvm_no_object);
}

void holdrelel(struct vnode *vnode __unused) { panic(rvm_no_object); }

krwlock_t *rw_obj_alloc(void) { panic(rvm_no_sleep); }
bool rw_obj_free(krwlock_t *lock __unused) { panic(rvm_no_sleep); }
void ubc_purge(struct uvm_object *obj)
{
	if (!LIST_EMPTY(&obj->uo_ubc))
		panic(rvm_no_object);
}
bool pmap_clear_modify(struct vm_page *page __unused) { panic(rvm_no_object); }
void vholdl(struct vnode *vnode __unused) { panic(rvm_no_object); }
void
kpreempt_disable(void)
{
	unsigned int guard = rvm_intr_suspend();
	check_mode();
	if (curlwp == NULL || curlwp != curcpu()->ci_onproc || curlwp->l_cpu != curcpu() ||
	    !(curlwp->l_flag & LW_SYSTEM) || curlwp->l_nopreempt < 0 ||
	    curlwp->l_nopreempt == 0x7fffffff)
		panic(rvm_bad_lock);
	curlwp->l_nopreempt++;
	__asm volatile("" ::: "memory");
	rvm_intr_resume(guard);
}

void
kpreempt_enable(void)
{
	unsigned int guard = rvm_intr_suspend();
	check_mode();
	if (curlwp == NULL || curlwp != curcpu()->ci_onproc || curlwp->l_cpu != curcpu() ||
	    !(curlwp->l_flag & LW_SYSTEM) || curlwp->l_nopreempt <= 0 || curlwp->l_dopreempt)
		panic(rvm_bad_lock);
	__asm volatile("" ::: "memory");
	curlwp->l_nopreempt--;
	rvm_intr_resume(guard);
}

/* No daemon runs, so its progress generation remains zero. Wired NOWAIT km
 * allocation reads a ticket but never waits or asks whether reclaim can run.
 */
uint64_t uvm_wait_prepare(void) { check_mode(); return 0; }
void uvm_wait(const char *name __unused, uint64_t ticket __unused)
{ panic(rvm_no_sleep); }
bool uvm_reclaimable(void) { panic(rvm_no_sleep); }
void uvm_pagewait(struct vm_page *page __unused, krwlock_t *lock __unused,
    const char *message __unused) { panic(rvm_no_sleep); }

/* Pool-only bootstrap services; no clock ticks or passive serialization. */
int cold __attribute__((section(".data"))) = 1;
/* time__uptime is published by the standalone MI timecounter core. */
unsigned int
atomic_inc_uint_nv(volatile unsigned int *value)
{
	check_mode();
	return ++*value;
}

int
ffs(int value)
{
	unsigned int bits = (unsigned int)value;
	int position = 1;
	if (bits == 0)
		return 0;
	while (!(bits & 1)) {
		bits >>= 1;
		position++;
	}
	return position;
}

int
strcmp(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int ratecheck(struct timeval *last __unused, const struct timeval *rate __unused)
{ panic(rvm_no_sleep); }
void log(int level __unused, const char *message __unused, ...)
{ panic(rvm_no_sleep); }
void xc_barrier(unsigned int flags __unused) { panic(rvm_no_sleep); }

/* Only the boot CPU exists. Pool cache invalidation takes its direct UP path. */
int ncpu __aligned(4) __attribute__((section(".data"))) = 1;
/* mp_online uses padded loader assembly storage; GCC emits no scalar alignment. */

/* These bootstrap atomics are valid only with caches and hardware IRQs off.
 * check_mode() enforces CR == MMU. They are not the full MD atomic API.
 */
void *
atomic_cas_ptr(volatile void *address, void *expected, void *replacement)
{
	void *volatile *slot = (void *volatile *)address;
	void *old;
	check_mode();
	__asm volatile("" ::: "memory");
	old = *slot;
	if (old == expected)
		*slot = replacement;
	__asm volatile("" ::: "memory");
	return old;
}

void
membar_release(void)
{
	uint64_t cr = rvm_boot_get_cr();
	/* UP and uncached execution need a compiler fence even when IRQs can
	 * interrupt the reader. Do not mask here: MI generation retries handle
	 * an interrupt or preemption between a barrier and the next load. */
	if (cr != RVM_CR_MMU && cr != (RVM_CR_MMU | RVM_CR_INTERRUPT)) panic(rvm_bad_lock);
	__asm volatile("" ::: "memory");
}

/* One CPU and caches disabled; valid on both sides of IRQ entry. */
void membar_producer(void) { membar_release(); }
void membar_consumer(void) { membar_release(); }

int nullop(void *arg __unused) { return 0; }

/* Reaching either call means the UP bootstrap contract was violated. */
uint64_t xc_broadcast(u_int flags __unused, xcfunc_t fn __unused,
    void *a __unused, void *b __unused) { panic(rvm_no_sleep); }
void xc_wait(uint64_t ticket __unused) { panic(rvm_no_sleep); }

/* Page loans and delayed PG_BUSY anonymous release still require a scheduler. */
krw_t rw_lock_op(krwlock_t *lock __unused) { panic(rvm_no_sleep); }
void rw_obj_hold(krwlock_t *lock __unused) { panic(rvm_no_sleep); }

unsigned int uvm_extrapages __attribute__((section(".data"))) = 0;
void
atomic_add_int(volatile unsigned int *value, int delta)
{
	check_mode();
	*value += delta;
}

/* A guarded allocator checkpoint defers scheduling until the outer boundary. */
void
preempt_point(void)
{
	unsigned int guard = rvm_intr_suspend();
	if (curlwp == NULL || curlwp != curcpu()->ci_onproc || curlwp->l_cpu != curcpu() ||
	    !(curlwp->l_flag & LW_SYSTEM)) panic(rvm_no_sleep);
	if (rvm_async_clock_active()) {
		if (guard == 0 && curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_irq_locks == 0 &&
		    curlwp->l_nopreempt == 0 && !cpu_intr_p()) rvm_bootsched_preempt();
	} else if (curlwp->l_nopreempt != 0 || curlwp->l_dopreempt || curcpu()->ci_want_resched) {
		panic(rvm_no_sleep);
	}
	__asm volatile("" ::: "memory");
	rvm_intr_resume(guard);
}
