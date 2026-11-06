/* Kernel-LWP creation. User trap state, process fork and exit remain pending. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/mutex.h>
#include <uvm/uvm.h>
#include <machine/boot.h>
#include <machine/psl.h>
#include <machine/pcb.h>
#include <machine/switch.h>
#include <machine/uarea.h>

const char rvm_lwp_bad[] = "Invalid RVM kernel LWP lifecycle";

void
cpu_lwp_fork(struct lwp *parent, struct lwp *child, void *stack,
    size_t stacksize, void (*func)(void *), void *arg)
{
	vaddr_t base;
	if (rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() ||
	    curcpu()->ci_cpl != IPL_NONE || parent != curlwp || child == NULL ||
	    child == parent || parent->l_addr == NULL ||
	    child->l_addr == NULL || child->l_md.md_pcb != NULL ||
	    child->l_stat != LSIDL || !(child->l_flag & LW_SYSTEM) ||
	    child->l_proc == NULL || child->l_proc->p_vmspace != NULL ||
	    stack != NULL || stacksize != 0 || func == NULL)
		panic(rvm_lwp_bad);
	/* UVM installs l_addr before calling MD fork; PCB is at u-area offset 0. */
	base = (vaddr_t)child->l_addr;
	child->l_addr = NULL;
	if (!rvm_cpu_lwp_prepare(child, base, USPACE, func, arg))
		panic(rvm_lwp_bad);
	child->l_md.md_tf = NULL;
	child->l_md.md_startup = 1;
}

/* No lazy coprocessor state exists yet. UVM still owns the u-area. */
void
cpu_lwp_free(struct lwp *l, int proc __unused)
{
	if (l == NULL || !(l->l_flag & LW_SYSTEM))
		panic(rvm_lwp_bad);
}

void
cpu_lwp_free2(struct lwp *l)
{
	if (rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() ||
	    curcpu()->ci_cpl != IPL_NONE || l == NULL || l == curlwp || !(l->l_flag & LW_SYSTEM) ||
	    (l->l_pflag & LP_RUNNING) ||
	    (l->l_stat != LSZOMB && l->l_stat != LSIDL) || l->l_addr == NULL ||
	    l->l_md.md_pcb != l->l_addr)
		panic(rvm_lwp_bad);
	if (!rvm_uarea_unbind(l)) panic(rvm_lwp_bad);
	memset(&l->l_md, 0, sizeof(l->l_md));
	/* Do not clear l_addr: uvm_lwp_exit() needs it to free the u-area. */
}

/* Checked boundary for the IRQ-off bootstrap's selected MI startup body. */
void
rvm_kernel_lwp_startup(struct lwp *previous, struct lwp *l)
{
	if (rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() || l == NULL || l != curlwp ||
	    previous == NULL || previous == l || curcpu()->ci_onproc != l ||
	    curcpu()->ci_cpl != IPL_SCHED || curcpu()->ci_mtx_count != -2 ||
	    !(previous->l_pflag & LP_RUNNING) || previous->l_mutex == NULL ||
	    !mutex_owned(previous->l_mutex) || previous->l_mutex->mtx_ipl != IPL_SCHED ||
	    l->l_md.md_startup != 1 ||
	    !(l->l_flag & LW_SYSTEM) || !(l->l_pflag & LP_RUNNING) ||
	    !(l->l_pflag & LP_MPSAFE) || l->l_proc == NULL ||
	    l->l_proc->p_vmspace != NULL)
		panic(rvm_lwp_bad);
	l->l_md.md_startup = 0;
	lwp_startup(previous, l);
}

static struct lwp child __attribute__((section(".data"))) = { 0 };
static struct proc kernel_proc __attribute__((section(".data"))) = { 0 };
static kmutex_t handoff_lock __attribute__((section(".data"))) = { 0 };
static struct lwp *controller __attribute__((section(".data"))) = NULL;
static unsigned int entered __attribute__((section(".data"))) = 0;

static void
kernel_entry(void *arg)
{
	if (arg != &child || curlwp != &child || curcpu()->ci_cpl != IPL_NONE ||
	    curcpu()->ci_mtx_count != 0 || (controller->l_pflag & LP_RUNNING) ||
	    mutex_owned(&handoff_lock) || child.l_md.md_startup != 0)
		panic(rvm_lwp_bad);
	entered++;
	/* Model the scheduler's return selection. Normal exit is still pending. */
	splsched();
	controller->l_pflag |= LP_RUNNING;
	curcpu()->ci_onproc = controller;
	cpu_switchto(&child, controller, false);
	panic(rvm_lwp_bad);
}

int
rvm_cpu_lwp_verify(void)
{
	vaddr_t stack;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	kmutex_t *oldlock;
	unsigned int oldflags, oldstat, n;
	if (!rvm_cpu_bootstrap_lwp())
		return 0;
	controller = curlwp;
	oldflags = controller->l_pflag; oldstat = controller->l_stat;
	oldlock = controller->l_mutex;
	mutex_init(&handoff_lock, MUTEX_SPIN, IPL_SCHED);
	entered = 0;
	for (n = 0; n < 2; n++) {
		stack = uvm_km_alloc(kernel_map, USPACE, 0,
		    UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
		if (stack == 0) return 0;
		memset(&child, 0, sizeof(child));
		child.l_cpu = curcpu(); child.l_flag = LW_SYSTEM;
		child.l_stat = LSIDL; child.l_proc = &kernel_proc;
		child.l_pflag = LP_MPSAFE;
		child.l_addr = (void *)stack;
		cpu_lwp_fork(controller, &child, NULL, 0, kernel_entry, &child);
		child.l_stat = LSONPROC; child.l_pflag |= LP_RUNNING;
		controller->l_stat = n == 0 ? LSONPROC : LSZOMB;
		controller->l_pflag |= LP_RUNNING;
		controller->l_mutex = &handoff_lock;
		splsched();
		mutex_spin_enter(&handoff_lock);
		/* mi_switch's count convention; bootstrap mutexes do not model it. */
		curcpu()->ci_mtx_count = -2;
		curcpu()->ci_onproc = &child;
		if (cpu_switchto(controller, &child, false) != &child || entered != n + 1)
			return 0;
		spl0();
		child.l_pflag &= ~LP_RUNNING; child.l_stat = LSZOMB;
		cpu_lwp_free(&child, 0);
		cpu_lwp_free2(&child);
		if (child.l_addr != (void *)stack || child.l_md.md_pcb != NULL)
			return 0;
		uvm_km_free(kernel_map, stack, USPACE, UVM_KMF_WIRED);
	}
	controller->l_pflag = oldflags; controller->l_stat = oldstat;
	controller->l_mutex = oldlock;
	mutex_destroy(&handoff_lock);
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before &&
	    curcpu()->ci_mtx_count == 0 && curcpu()->ci_cpl == IPL_NONE;
}
