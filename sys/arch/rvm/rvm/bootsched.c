/* Directed kernel-LWP handoffs through real mi_switch(), with IRQs off.
 * This bridge supplies no process cleanup, timer or preemption.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/mutex.h>
#include <sys/idle.h>
#include <uvm/uvm.h>
#include <machine/boot.h>
#include <machine/psl.h>
#include <machine/pcb.h>
#include <machine/switch.h>
#include <machine/bootsched.h>
#include <machine/irq.h>
#include <machine/bootclock.h>

#define MAX_WORKERS 16
static kmutex_t sched_lock __attribute__((section(".data"))) = { 0 };
static kmutex_t worker_lock __attribute__((section(".data"))) = { 0 };
static struct lwp *owner __attribute__((section(".data"))) = NULL;
static struct lwp *members[MAX_WORKERS] __attribute__((section(".data"))) = { 0 };
static kmutex_t *saved_lock __attribute__((section(".data"))) = NULL;
static unsigned int saved_flags __attribute__((section(".data"))) = 0;
static __typeof__(cpu_info_primary.ci_schedstate.spc_queue[0])
    queues[PRI_COUNT] __attribute__((section(".data"))) = { 0 };
static int saved_spc_flags __attribute__((section(".data"))) = 0;
static pri_t saved_maxpri __attribute__((section(".data"))) = 0;
static pri_t saved_curpri __attribute__((section(".data"))) = 0;
static struct lwp idle_lwp __attribute__((section(".data"))) = { 0 };
static struct proc idle_proc __attribute__((section(".data"))) = { 0 };
static void (*idle_poll)(void *) __attribute__((section(".data"))) = NULL;
static void *idle_poll_arg __attribute__((section(".data"))) = NULL;
void rvm_cpu_halt(void);
const char rvm_runtime_bad[] = "RVM LWP runtime clock moved backwards";
const char rvm_sched_bad[] = "Invalid RVM directed MI switch";

void __dead
rvm_bootsched_unavailable(void)
{
	panic(rvm_sched_bad);
}

static int
mode_ok(void)
{
	return rvm_boot_get_cr() == RVM_CR_MMU && !cpu_intr_p();
}

static int
member(struct lwp *l)
{
	unsigned int i;
	for (i = 0; i < MAX_WORKERS; i++)
		if (members[i] == l) return (int)i;
	return -1;
}

void
rvm_bootsched_vmcheck(struct lwp *l)
{
	struct pcb *pcb;
	if (owner == NULL || !mode_ok() || l == NULL || l != curlwp ||
	    !(l->l_flag & LW_SYSTEM) || l->l_cpu != curcpu() ||
	    (l->l_proc == NULL && l != owner) ||
	    (l->l_proc != NULL && l->l_proc->p_vmspace != NULL) ||
	    l->l_addr == NULL || l->l_md.md_pcb != l->l_addr)
		panic(rvm_sched_bad);
	pcb = lwp_getpcb(l);
	if (pcb->pcb_mmu_table != pmap_kernel()->pm_root)
		panic(rvm_sched_bad);
}

/* This profile models the MI spin count, including its extra IPL hold.
 * Other bootstrap mutex callers retain their existing lock/IPL bookkeeping.
 */
void
rvm_bootsched_unlock(kmutex_t *lock)
{
	if (owner == NULL || !mode_ok() || curcpu()->ci_cpl != IPL_SCHED ||
	    (curcpu()->ci_mtx_count != -2 && curcpu()->ci_mtx_count != -1) ||
	    (lock != &sched_lock && lock != &worker_lock) || !mutex_owned(lock))
		panic(rvm_sched_bad);
	mutex_spin_exit(lock);
	curcpu()->ci_mtx_count++;
	if (curcpu()->ci_mtx_count == 0) splx(curcpu()->ci_mtx_oldspl);
	else splsched();
}

int
rvm_bootsched_init(void)
{
	unsigned int i;
	if (!mode_ok() || owner != NULL || curcpu()->ci_cpl != IPL_NONE ||
	    curcpu()->ci_mtx_count != 0 || curlwp == NULL ||
	    curlwp->l_stat != LSONPROC ||
	    curcpu()->ci_schedstate.spc_mutex != NULL || curcpu()->ci_data.cpu_idlelwp != NULL ||
	    curcpu()->ci_schedstate.spc_queue != NULL || curcpu()->ci_schedstate.spc_count != 0 ||
	    curcpu()->ci_schedstate.spc_lwplock != NULL || !rvm_cpu_bootstrap_lwp())
		return 0;
	for (i = 0; i < __arraycount(curcpu()->ci_schedstate.spc_bitmap); i++)
		if (curcpu()->ci_schedstate.spc_bitmap[i] != 0) return 0;
	for (i = 0; i < PRI_COUNT; i++) TAILQ_INIT(&queues[i]);
	saved_spc_flags = curcpu()->ci_schedstate.spc_flags;
	saved_maxpri = curcpu()->ci_schedstate.spc_maxpriority;
	saved_curpri = curcpu()->ci_schedstate.spc_curpriority;
	curcpu()->ci_schedstate.spc_queue = queues;
	owner = curlwp;
	saved_lock = owner->l_mutex; saved_flags = owner->l_pflag;
	mutex_init(&sched_lock, MUTEX_SPIN, IPL_SCHED);
	mutex_init(&worker_lock, MUTEX_SPIN, IPL_SCHED);
	owner->l_mutex = &worker_lock;
	owner->l_pflag |= LP_RUNNING | LP_MPSAFE;
	curcpu()->ci_schedstate.spc_mutex = &sched_lock;
	curcpu()->ci_schedstate.spc_lwplock = &worker_lock;
	return 1;
}

int
rvm_bootsched_fini(void)
{
	unsigned int i;
	if (!mode_ok() || owner == NULL || curlwp != owner || rvm_clock_active() ||
	    curcpu()->ci_cpl != IPL_NONE || curcpu()->ci_mtx_count != 0 ||
	    curcpu()->ci_schedstate.spc_count != 0 || curcpu()->ci_schedstate.spc_mcount != 0 ||
	    mutex_owned(&sched_lock) || mutex_owned(&worker_lock)) return 0;
	for (i = 0; i < MAX_WORKERS; i++) if (members[i] != NULL) return 0;
	if (curcpu()->ci_data.cpu_idlelwp != NULL) {
		if (curcpu()->ci_data.cpu_idlelwp != &idle_lwp ||
		    (idle_lwp.l_pflag & LP_RUNNING) || idle_lwp.l_stat != LSIDL) return 0;
		cpu_lwp_free2(&idle_lwp);
		curcpu()->ci_data.cpu_idlelwp = NULL;
		uvm_lwp_exit(&idle_lwp);
		memset(&idle_lwp, 0, sizeof(idle_lwp));
		idle_poll = NULL; idle_poll_arg = NULL;
	}
	curcpu()->ci_schedstate.spc_queue = NULL;
	curcpu()->ci_schedstate.spc_flags = saved_spc_flags;
	curcpu()->ci_schedstate.spc_maxpriority = saved_maxpri;
	curcpu()->ci_schedstate.spc_curpriority = saved_curpri;
	owner->l_mutex = saved_lock; owner->l_pflag = saved_flags;
	curcpu()->ci_schedstate.spc_mutex = NULL;
	curcpu()->ci_schedstate.spc_lwplock = NULL;
	mutex_destroy(&worker_lock); mutex_destroy(&sched_lock);
	owner = NULL;
	return 1;
}

static void
handoff(struct lwp *next, int exiting)
{
	struct lwp *self = curlwp;
	int slot;
	if (!mode_ok() || owner == NULL || rvm_bootclock_busy() || curcpu()->ci_cpl != IPL_NONE ||
	    curcpu()->ci_mtx_count != 0 || self == NULL || next == NULL || self == next ||
	    self->l_stat != LSONPROC || !(self->l_pflag & LP_RUNNING) ||
	    self->l_mutex != &worker_lock || self->l_switchto != NULL ||
	    self->l_nopreempt != 0 || next->l_nopreempt != 0 ||
	    self->l_lwpctl != NULL || next->l_lwpctl != NULL ||
	    (self->l_pflag & (LP_INTR | LP_PREEMPTING)) || self->l_blcnt != 0 ||
	    next->l_cpu != curcpu() || !(next->l_flag & LW_SYSTEM) || (next->l_flag & LW_IDLE) ||
	    !(next->l_pflag & LP_MPSAFE) || (next->l_pflag & (LP_RUNNING | LP_INTR)) ||
	    next->l_addr == NULL || next->l_md.md_pcb != next->l_addr ||
	    (next->l_proc == NULL && next != owner) ||
	    (next->l_proc != NULL && next->l_proc->p_vmspace != NULL) ||
	    curcpu()->ci_data.cpu_softints != 0 || (exiting && self == owner))
		panic(rvm_sched_bad);
	rvm_bootsched_vmcheck(self);
	if (next->l_md.md_startup == 1 && next->l_stat == LSIDL && next != owner) {
		if (member(next) != -1 || (slot = member(NULL)) == -1)
			panic(rvm_sched_bad);
		members[slot] = next;
	} else if (next->l_stat != LSSLEEP || next->l_md.md_startup != 0 ||
	    next->l_mutex != &worker_lock || (next != owner && member(next) == -1)) {
		panic(rvm_sched_bad);
	}
	if (exiting) cpu_lwp_free(self, 0);
	/* Directed switches keep the outgoing LWP off the run queues. */
	self->l_stat = exiting ? LSZOMB : LSSLEEP;
	next->l_stat = LSONPROC; next->l_pflag |= LP_RUNNING;
	next->l_mutex = &worker_lock;
	self->l_switchto = next;
	curcpu()->ci_mtx_oldspl = curcpu()->ci_cpl;
	mutex_spin_enter(&worker_lock);
	mutex_spin_enter(&sched_lock);
	curcpu()->ci_mtx_count = -2; /* Two held scheduler locks on MI entry. */
	mi_switch(self);
	if (exiting || curlwp != self || curcpu()->ci_onproc != self ||
	    self->l_stat != LSONPROC || !(self->l_pflag & LP_RUNNING) ||
	    self->l_switchto != NULL || curcpu()->ci_mtx_count != 0 ||
	    curcpu()->ci_cpl != IPL_NONE || mutex_owned(&sched_lock) || mutex_owned(&worker_lock))
		panic(rvm_sched_bad);
}

void rvm_bootsched_switch(struct lwp *next) { handoff(next, 0); }
void __dead rvm_bootsched_exit(struct lwp *next)
{
	handoff(next, 1);
	panic(rvm_sched_bad);
}

int
rvm_bootsched_reap(struct lwp *l)
{
	int slot;
	if (!mode_ok() || owner == NULL || curcpu()->ci_cpl != IPL_NONE ||
	    l == NULL || l == curlwp || l == owner || (slot = member(l)) == -1 ||
	    l->l_stat != LSZOMB || (l->l_pflag & LP_RUNNING) || l->l_addr == NULL)
		return 0;
	cpu_lwp_free2(l);
	uvm_lwp_exit(l);
	l->l_mutex = NULL;
	members[slot] = NULL;
	return 1;
}


/* The MI self-selection path drops both locks without changing stacks. */
void
rvm_bootsched_lwp_unlock(struct lwp *l)
{
	if (l != curlwp || l->l_mutex != &worker_lock)
		panic(rvm_sched_bad);
	rvm_bootsched_unlock(l->l_mutex);
}

struct lwp *
rvm_bootsched_pick(void)
{
	struct lwp *l = sched_nextlwp();
	if (l == NULL && curcpu()->ci_data.cpu_idlelwp != &idle_lwp)
		panic(rvm_sched_bad);
	return l;
}

int
rvm_bootsched_ready(struct lwp *l)
{
	int slot = -1;
	pri_t priority;
	kmutex_t *previous_lock;
	if (!mode_ok() || owner == NULL || l == NULL || l == curlwp ||
	    curcpu()->ci_cpl != IPL_NONE || curcpu()->ci_mtx_count != 0 ||
	    l->l_cpu != curcpu() || !(l->l_flag & LW_SYSTEM) || (l->l_flag & LW_IDLE) ||
	    !(l->l_pflag & LP_MPSAFE) || (l->l_pflag & (LP_RUNNING | LP_INTR | LP_PREEMPTING)) ||
	    l->l_lwpctl != NULL || l->l_nopreempt != 0 || l->l_target_cpu != NULL ||
	    l->l_addr == NULL || l->l_md.md_pcb != l->l_addr ||
	    (l->l_proc == NULL && l != owner) ||
	    (l->l_proc != NULL && l->l_proc->p_vmspace != NULL) ||
	    l->l_class != SCHED_OTHER)
		return 0;
	if (l->l_stat == LSIDL && l->l_md.md_startup == 1 && l != owner) {
		if (member(l) != -1 || (slot = member(NULL)) == -1) return 0;
	} else if (l->l_stat != LSSLEEP || l->l_md.md_startup != 0 ||
	    l->l_mutex != &worker_lock || (l != owner && member(l) == -1)) return 0;
	mutex_spin_enter(&worker_lock); mutex_spin_enter(&sched_lock);
	previous_lock = l->l_mutex;
	l->l_mutex = &worker_lock;
	priority = lwp_eprio(l);
	if (priority < 0 || priority >= PRI_COUNT) {
		l->l_mutex = previous_lock;
		mutex_spin_exit(&sched_lock); mutex_spin_exit(&worker_lock);
		return 0;
	}
	if (slot != -1) members[slot] = l;
	l->l_mutex = &sched_lock; l->l_stat = LSRUN;
	sched_enqueue(l);
	mutex_spin_exit(&sched_lock); mutex_spin_exit(&worker_lock);
	return 1;
}

static void
runqueue_switch(int action)
{
	struct lwp *self = curlwp;
	int exiting = action == 1;
	int sleeping = action == 2;
	if (!mode_ok() || owner == NULL || rvm_bootclock_busy() || self == NULL ||
	    curcpu()->ci_cpl != IPL_NONE || curcpu()->ci_mtx_count != 0 ||
	    self->l_stat != LSONPROC || !(self->l_pflag & LP_RUNNING) ||
	    self->l_mutex != &worker_lock || self->l_switchto != NULL ||
	    self->l_nopreempt != 0 || self->l_lwpctl != NULL ||
	    self->l_blcnt != 0 || self->l_target_cpu != NULL ||
	    (self->l_pflag & (LP_INTR | LP_PREEMPTING)) || self->l_class != SCHED_OTHER ||
	    curcpu()->ci_data.cpu_softints != 0 ||
	    (exiting && self == owner) ||
	    ((exiting || sleeping) && curcpu()->ci_schedstate.spc_count == 0 &&
	    curcpu()->ci_data.cpu_idlelwp == NULL))
		panic(rvm_sched_bad);
	rvm_bootsched_vmcheck(self);
	curcpu()->ci_mtx_oldspl = curcpu()->ci_cpl;
	mutex_spin_enter(&worker_lock); mutex_spin_enter(&sched_lock);
	if (lwp_eprio(self) < 0 || lwp_eprio(self) >= PRI_COUNT) panic(rvm_sched_bad);
	if (exiting) { cpu_lwp_free(self, 0); self->l_stat = LSZOMB; }
	else if (sleeping) self->l_stat = LSSLEEP;
	curcpu()->ci_mtx_count = -2;
	mi_switch(self);
	if (exiting || self != curlwp || curcpu()->ci_cpl != IPL_NONE ||
	    curcpu()->ci_mtx_count != 0 || self->l_stat != LSONPROC ||
	    !(self->l_pflag & LP_RUNNING) || self->l_mutex != &worker_lock ||
	    mutex_owned(&worker_lock) || mutex_owned(&sched_lock)) panic(rvm_sched_bad);
}

void rvm_bootsched_sleep(void) { runqueue_switch(2); }
void rvm_bootsched_yield(void) { runqueue_switch(0); }
void __dead rvm_bootsched_finish(void)
{
	runqueue_switch(1);
	panic(rvm_sched_bad);
}

int
rvm_bootsched_idle_init(void (*poll)(void *), void *arg)
{
	vaddr_t stack;
	if (!mode_ok() || owner == NULL || curlwp != owner ||
	    curcpu()->ci_cpl != IPL_NONE || curcpu()->ci_mtx_count != 0 ||
	    curcpu()->ci_data.cpu_idlelwp != NULL) return 0;
	stack = uvm_uarea_system_alloc(curcpu());
	if (!stack) return 0;
	memset(&idle_lwp, 0, sizeof(idle_lwp));
	idle_lwp.l_cpu = curcpu();
	idle_lwp.l_flag = LW_SYSTEM; idle_lwp.l_pflag = LP_MPSAFE;
	idle_lwp.l_proc = &idle_proc; idle_lwp.l_stat = LSIDL;
	idle_lwp.l_priority = PRI_IDLE;
	uvm_lwp_setuarea(&idle_lwp, stack);
	uvm_lwp_fork(owner, &idle_lwp, NULL, 0, idle_loop, NULL);
	idle_lwp.l_flag |= LW_IDLE;
	idle_lwp.l_mutex = &worker_lock;
	idle_poll = poll; idle_poll_arg = arg;
	curcpu()->ci_data.cpu_idlelwp = &idle_lwp;
	return 1;
}

void
rvm_bootsched_idle_lock(struct lwp *l)
{
	if (!mode_ok() || l != &idle_lwp || l != curlwp ||
	    curcpu()->ci_cpl != IPL_NONE || curcpu()->ci_mtx_count != 0 ||
	    l->l_mutex != &worker_lock) panic(rvm_sched_bad);
	curcpu()->ci_mtx_oldspl = IPL_NONE;
	mutex_spin_enter(&worker_lock);
}

void
rvm_bootsched_idle_unlock(struct lwp *l)
{
	if (l != &idle_lwp || l != curlwp || curcpu()->ci_cpl != IPL_SCHED ||
	    curcpu()->ci_mtx_count != 0 || !mutex_owned(&worker_lock)) panic(rvm_sched_bad);
	mutex_spin_exit(&worker_lock);
}

void
rvm_bootsched_idle_switch(struct lwp *l)
{
	if (!mode_ok() || l != &idle_lwp || l != curlwp || l->l_stat != LSIDL ||
	    curcpu()->ci_cpl != IPL_SCHED || curcpu()->ci_mtx_count != 0 ||
	    !mutex_owned(&worker_lock) || !mutex_owned(&sched_lock)) panic(rvm_sched_bad);
	curcpu()->ci_mtx_count = -2;
	mi_switch(l);
	if (l != curlwp || l->l_stat != LSIDL || curcpu()->ci_mtx_count != 0 ||
	    curcpu()->ci_cpl != IPL_NONE || mutex_owned(&worker_lock) ||
	    mutex_owned(&sched_lock)) panic(rvm_sched_bad);
}

void
cpu_idle(void)
{
	if (!mode_ok() || curlwp != &idle_lwp || !CURCPU_IDLE_P() ||
	    curcpu()->ci_cpl != IPL_NONE || curcpu()->ci_mtx_count != 0 ||
	    idle_lwp.l_stat != LSIDL) panic(rvm_sched_bad);
	/* Polling is a bootstrap event source, not a simulated interrupt. */
	if (idle_poll != NULL) idle_poll(idle_poll_arg);
	if (curlwp != &idle_lwp || curcpu()->ci_cpl != IPL_NONE ||
	    curcpu()->ci_mtx_count != 0 || !mode_ok()) panic(rvm_sched_bad);
	if (!sched_curcpu_runnable_p()) {
		if (rvm_clock_active()) {
			if (!rvm_clock_wait()) panic(rvm_sched_bad);
			/* Deliver deferred wakeups after IRQ return, with IRQs off. */
			if (idle_poll != NULL) idle_poll(idle_poll_arg);
		} else rvm_cpu_halt();
	}
}

static struct lwp workers[2] __attribute__((section(".data"))) = { 0 };
static struct proc kernel_proc __attribute__((section(".data"))) = { 0 };
static unsigned int progress[2] __attribute__((section(".data"))) = { 0 };

static void
worker(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	unsigned int n;
	volatile uint64_t canary = 0x52564d5357495443UL + id;
	for (n = 0; n < 8; n++) {
		if (canary != 0x52564d5357495443UL + id) panic(rvm_sched_bad);
		progress[id]++;
		if (n == 7) rvm_bootsched_exit(owner);
		rvm_bootsched_switch(owner);
	}
	panic(rvm_sched_bad);
}

int
rvm_bootsched_verify(void)
{
	unsigned int i, n;
	vaddr_t stack;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_bootsched_init()) return 0;
	for (i = 0; i < 2; i++) {
		stack = uvm_km_alloc(kernel_map, USPACE, 0,
		    UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
		if (stack == 0) return 0;
		memset(&workers[i], 0, sizeof(workers[i]));
		workers[i].l_addr = (void *)stack; workers[i].l_cpu = curcpu();
		workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		workers[i].l_proc = &kernel_proc; workers[i].l_stat = LSIDL;
		progress[i] = 0;
		cpu_lwp_fork(owner, &workers[i], NULL, 0, worker, (void *)(uintptr_t)i);
	}
	for (n = 0; n < 8; n++) {
		for (i = 0; i < 2; i++) {
			rvm_bootsched_switch(&workers[i]);
			if (progress[i] != n + 1 || (workers[i].l_pflag & LP_RUNNING)) return 0;
		}
	}
	for (i = 0; i < 2; i++) if (!rvm_bootsched_reap(&workers[i])) return 0;
	return rvm_bootsched_fini() && curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before;
}

static void
queue_worker(void *arg)
{
	unsigned int id = (uintptr_t)arg, n;
	volatile uint64_t canary = 0x52564d52554e5100UL + id;
	for (n = 0; n < 8; n++) {
		if (canary != 0x52564d52554e5100UL + id) panic(rvm_sched_bad);
		progress[id]++;
		if (n == 7) rvm_bootsched_finish();
		rvm_bootsched_yield();
	}
	panic(rvm_sched_bad);
}

int
rvm_bootsched_runq_verify(void)
{
	unsigned int i;
	vaddr_t stack;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_bootsched_init()) return 0;
	/* No other runnable LWPs: MI must select self and release both locks. */
	rvm_bootsched_yield();
	for (i = 0; i < 2; i++) {
		stack = uvm_km_alloc(kernel_map, USPACE, 0,
		    UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
		if (!stack) return 0;
		memset(&workers[i], 0, sizeof(workers[i]));
		workers[i].l_addr = (void *)stack; workers[i].l_cpu = curcpu();
		workers[i].l_flag = LW_SYSTEM; workers[i].l_pflag = LP_MPSAFE;
		workers[i].l_proc = &kernel_proc; workers[i].l_stat = LSIDL;
		progress[i] = 0;
		cpu_lwp_fork(owner, &workers[i], NULL, 0, queue_worker, (void *)(uintptr_t)i);
		if (!rvm_bootsched_ready(&workers[i])) return 0;
	}
	while (workers[0].l_stat != LSZOMB || workers[1].l_stat != LSZOMB)
		rvm_bootsched_yield();
	for (i = 0; i < 2; i++)
		if (progress[i] != 8 || !rvm_bootsched_reap(&workers[i])) return 0;
	return rvm_bootsched_fini() && curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before;
}

static unsigned int idle_wakes __attribute__((section(".data"))) = 0;
static void
wake_owner(void *arg)
{
	if (arg != owner || !rvm_bootsched_ready(owner)) panic(rvm_sched_bad);
	idle_wakes++;
}

int
rvm_bootsched_idle_verify(void)
{
	unsigned int n;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_bootsched_init() || !rvm_bootsched_idle_init(wake_owner, curlwp)) return 0;
	idle_wakes = 0;
	for (n = 0; n < 8; n++) {
		rvm_bootsched_sleep();
		if (idle_wakes != n + 1 || (idle_lwp.l_pflag & LP_RUNNING) ||
		    idle_lwp.l_stat != LSIDL || curcpu()->ci_schedstate.spc_count != 0) return 0;
	}
	return rvm_bootsched_fini() && curcpu()->ci_data.cpu_idlelwp == NULL &&
	    curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before;
}

static uint64_t clock_deadline __attribute__((section(".data"))) = 0;
static void
wake_clock_owner(void *arg)
{
	if (arg != owner) panic(rvm_sched_bad);
	if (rvm_clock_ticks() >= clock_deadline && !rvm_bootsched_ready(owner))
		panic(rvm_sched_bad);
}

int
rvm_clock_verify(void)
{
	unsigned int n;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	uint64_t initial = rvm_clock_ticks();
	if (!rvm_bootsched_init() || !rvm_bootsched_idle_init(wake_clock_owner, curlwp) ||
	    !rvm_clock_start()) return 0;
	for (n = 0; n < 4; n++) {
		clock_deadline = rvm_clock_ticks() + 2;
		rvm_bootsched_sleep();
		if (rvm_clock_ticks() != clock_deadline || curcpu()->ci_intr_depth != 0) return 0;
	}
	return rvm_clock_stop() && rvm_bootsched_fini() &&
	    rvm_clock_ticks() == initial + 8 && curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before;
}

static void
runtime_worker(void *arg)
{
	unsigned int n;
	if (arg != &workers[0]) panic(rvm_runtime_bad);
	for (n = 0; n < 2; n++) if (!rvm_clock_wait()) panic(rvm_runtime_bad);
	rvm_bootsched_finish();
}

int
rvm_bootsched_runtime_verify(void)
{
	struct bintime before;
	vaddr_t stack;
	int64_t pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_bootsched_init()) return 0;
	rvm_bootsched_yield(); /* Commit any pending controller time. */
	before = owner->l_rtime;
	stack = uvm_km_alloc(kernel_map, USPACE, 0,
	    UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
	if (!stack || !rvm_clock_start()) return 0;
	memset(&workers[0], 0, sizeof(workers[0]));
	workers[0].l_addr = (void *)stack; workers[0].l_cpu = curcpu();
	workers[0].l_flag = LW_SYSTEM; workers[0].l_pflag = LP_MPSAFE;
	workers[0].l_proc = &kernel_proc; workers[0].l_stat = LSIDL;
	cpu_lwp_fork(owner, &workers[0], NULL, 0, runtime_worker, &workers[0]);
	if (!rvm_bootsched_ready(&workers[0])) return 0;
	rvm_bootsched_yield();
	if (workers[0].l_stat != LSZOMB || workers[0].l_rtime.sec != 0 ||
	    workers[0].l_rtime.frac != 4 * ((1UL << 63) / RVM_CLOCK_HZ) ||
	    !bintimecmp(&owner->l_rtime, &before, ==)) return 0;
	if (!rvm_bootsched_reap(&workers[0]) ||
	    !rvm_bootsched_idle_init(wake_clock_owner, owner)) return 0;
	clock_deadline = rvm_clock_ticks() + 2;
	rvm_bootsched_sleep();
	if (!bintimecmp(&owner->l_rtime, &before, ==) ||
	    idle_lwp.l_rtime.sec != 0 || idle_lwp.l_rtime.frac != 0) return 0;
	return rvm_clock_stop() && rvm_bootsched_fini() &&
	    curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages;
}
