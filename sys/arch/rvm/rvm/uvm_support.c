/*
 * Services for the standalone MI page allocator. One CPU, CR == MMU only:
 * mutexes and logical IPLs have real state, but may never sleep or contend.
 * Full kernels must replace this file with NetBSD's normal kernel services.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/condvar.h>
#include <sys/pool.h>
#include <sys/radixtree.h>
#include <sys/vnode.h>
#include <uvm/uvm.h>
#include <machine/boot.h>
#include <machine/pmap.h>
#include <machine/psl.h>

#define BOOT_MUTEX_MAGIC 0x52564d4cU
struct cpu_info cpu_info_primary __attribute__((section(".data"))) = { 0 };
static struct lwp bootstrap_lwp __attribute__((section(".data"))) = { 0 };
size_t coherency_unit __attribute__((section(".data"))) = COHERENCY_UNIT;
unsigned long rvm_uvm_daemon_requests __attribute__((section(".data"))) = 0;

const char rvm_bad_lock[] = "Invalid bootstrap lock/IPL operation";
const char rvm_no_object[] = "Object/anonymous page services are not available";
const char rvm_no_sleep[] = "Sleeping or dynamic kernel allocation is not available";
const char rvm_bad_page[] = "Invalid raw UVM page operation";

/* Actual MI bodies, renamed only in the standalone compilation profile. */
struct vm_page *rvm_mi_pagealloc_strat(struct uvm_object *, voff_t,
    struct vm_anon *, int, int, int);
void rvm_mi_pagefree(struct vm_page *);

struct vm_page *
uvm_pagealloc_strat(struct uvm_object *obj, voff_t offset, struct vm_anon *anon,
    int flags, int strategy, int freelist)
{
	if (obj != NULL || anon != NULL)
		panic(rvm_no_object);
	if (offset != 0 || strategy != UVM_PGA_STRAT_NORMAL ||
	    freelist != VM_FREELIST_DEFAULT ||
	    (flags & ~(UVM_PGA_ZERO | UVM_PGA_USERESERVE)) != 0)
		panic(rvm_bad_page);
	return rvm_mi_pagealloc_strat(obj, offset, anon, flags, strategy, freelist);
}

void
uvm_pagefree(struct vm_page *page)
{
	if (page == NULL || (page->flags & PG_FREE) != 0 || page->uobject != NULL ||
	    page->uanon != NULL || page->loan_count != 0 || page->wire_count != 0 ||
	    (uvm.page_init_done && PHYS_TO_VM_PAGE(VM_PAGE_TO_PHYS(page)) != page))
		panic(rvm_bad_page);
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

static void
check_mode(void)
{
	if (rvm_boot_get_cr() != RVM_CR_MMU)
		panic(rvm_bad_lock);
}

int
splraise(int ipl)
{
	int old = curcpu()->ci_cpl;

	check_mode();
	if (ipl < IPL_NONE || ipl > IPL_HIGH)
		panic(rvm_bad_lock);
	if (ipl > old)
		curcpu()->ci_cpl = ipl;
	return old;
}

void
splx(int ipl)
{
	check_mode();
	if (ipl < IPL_NONE || ipl > IPL_HIGH)
		panic(rvm_bad_lock);
	curcpu()->ci_cpl = ipl;
}

void
spl0(void)
{
	splx(IPL_NONE);
}

void
mutex_init(kmutex_t *lock, kmutex_type_t type, int ipl)
{
	check_mode();
	if (type < MUTEX_SPIN || type > MUTEX_NODEBUG ||
	    ipl < IPL_NONE || ipl > IPL_HIGH)
		panic(rvm_bad_lock);
	lock->mtx_state = 0;
	lock->mtx_magic = BOOT_MUTEX_MAGIC;
	lock->mtx_ipl = ipl;
	lock->mtx_oldipl = IPL_NONE;
}

void
mutex_enter(kmutex_t *lock)
{
	check_mode();
	if (lock->mtx_magic != BOOT_MUTEX_MAGIC || lock->mtx_state != 0)
		panic(rvm_bad_lock);
	lock->mtx_oldipl = splraise(lock->mtx_ipl);
	lock->mtx_state = 1;
}

void
mutex_exit(kmutex_t *lock)
{
	check_mode();
	if (lock->mtx_magic != BOOT_MUTEX_MAGIC || lock->mtx_state != 1)
		panic(rvm_bad_lock);
	lock->mtx_state = 0;
	splx(lock->mtx_oldipl);
}

void mutex_spin_enter(kmutex_t *lock) { mutex_enter(lock); }
void mutex_spin_exit(kmutex_t *lock) { mutex_exit(lock); }

void
cv_init(kcondvar_t *cv, const char *name)
{
	cv->cv_opaque[0] = __UNCONST(name);
	cv->cv_opaque[1] = NULL;
}

void
cv_broadcast(kcondvar_t *cv)
{
	check_mode();
	if (cv->cv_opaque[0] == NULL || cv->cv_opaque[1] != NULL)
		panic(rvm_no_sleep);
	/* No waiters exist: the bootstrap does not permit cv_wait(). */
}

void cv_wait(kcondvar_t *cv __unused, kmutex_t *lock __unused)
{ panic(rvm_no_sleep); }
int cv_wait_sig(kcondvar_t *cv __unused, kmutex_t *lock __unused)
{ panic(rvm_no_sleep); }

void *kmem_alloc(size_t size __unused, km_flag_t flags __unused)
{ panic(rvm_no_sleep); }
void *pool_get(struct pool *pool __unused, int flags __unused)
{ panic(rvm_no_sleep); }
void pool_put(struct pool *pool __unused, void *item __unused)
{ panic(rvm_no_sleep); }

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

void *radix_tree_remove_node(struct radix_tree *tree __unused,
    uint64_t index __unused)
{ panic(rvm_no_object); }
void holdrelel(struct vnode *vnode __unused) { panic(rvm_no_object); }
unsigned int uvm_pagegetdirty(struct vm_page *pg __unused)
{ panic(rvm_no_object); }
void uvm_pagemarkdirty(struct vm_page *pg __unused, unsigned int status __unused)
{ panic(rvm_no_object); }
void wakeup(wchan_t channel __unused) { panic(rvm_no_sleep); }
