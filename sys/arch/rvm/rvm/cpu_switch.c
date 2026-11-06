/* MD kernel context switching. MI scheduling and user pmaps remain pending. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lwp.h>
#include <sys/cpu.h>
#include <uvm/uvm.h>
#include <machine/boot.h>
#include <machine/pcb.h>
#include <machine/pmap.h>
#include <machine/psl.h>
#include <machine/switch.h>
#include <machine/uarea.h>

static struct pcb boot_pcb __attribute__((section(".data"))) = { 0 };
const char rvm_switch_bad[] = "Invalid RVM kernel context switch";
struct lwp *rvm_cpu_switch(struct pcb *, struct pcb *, struct lwp *);
void rvm_lwp_trampoline(void);

static int
mode_ok(void)
{
	return rvm_boot_get_cr() == RVM_CR_MMU && !cpu_intr_p();
}

static int
stack_ok(struct lwp *l, vaddr_t sp)
{
	vaddr_t base = l->l_md.md_kstack_base;
	vsize_t size = l->l_md.md_kstack_size;
	return base != 0 && size >= PAGE_SIZE && base <= (vaddr_t)-1 - size &&
	    sp >= base && sp <= base + size - RVM_SWITCH_SIZE && (sp & 3) == 0;
}

int
rvm_cpu_bootstrap_lwp(void)
{
	struct lwp *l = curlwp;
	if (!mode_ok() || l == NULL || l != curcpu()->ci_onproc ||
	    curcpu()->ci_cpl != IPL_NONE)
		return 0;
	if (l->l_addr != NULL)
		return l->l_addr == &boot_pcb;
	boot_pcb.pcb_mmu_table = pmap_kernel()->pm_root;
	boot_pcb.pcb_sr = PSL_S;
	l->l_addr = &boot_pcb;
	l->l_md.md_pcb = &boot_pcb;
	l->l_md.md_kstack_base = (vaddr_t)rvm_boot_stack;
	l->l_md.md_kstack_size = RVM_BOOT_STACK_SIZE;
	return 1;
}

int
rvm_cpu_lwp_prepare(struct lwp *l, vaddr_t base, vsize_t size,
    void (*fn)(void *), void *arg)
{
	struct pcb *pcb;
	uint64_t *frame;
	paddr_t pa;
	vaddr_t va;
	if (!mode_ok() || curcpu()->ci_cpl != IPL_NONE || l == NULL || l == curlwp ||
	    l->l_addr != NULL || fn == NULL || size != USPACE || (base & PAGE_MASK) ||
	    base < VM_KERNEL_VM_BASE || base > VM_KERNEL_VM_BASE + VM_KERNEL_VM_SIZE - size)
		return 0;
	for (va = base; va < base + size; va += PAGE_SIZE) {
		if (!pmap_extract(pmap_kernel(), va, &pa))
			return 0;
	}
	if (!rvm_uarea_bind(l, base)) return 0;
	pcb = (struct pcb *)base;
	memset(pcb, 0, sizeof(*pcb));
	frame = (uint64_t *)(base + size - RVM_SWITCH_SIZE);
	memset(frame, 0, RVM_SWITCH_SIZE);
	frame[0] = (uintptr_t)fn; /* R10 */
	frame[1] = (uintptr_t)arg; /* R11 */
	frame[RVM_SWITCH_NREG] = (uintptr_t)rvm_lwp_trampoline;
	pcb->pcb_gpr[RVM_REG_SP] = (uintptr_t)frame;
	pcb->pcb_ssp = (uintptr_t)frame;
	pcb->pcb_pc = (uintptr_t)rvm_lwp_trampoline;
	pcb->pcb_mmu_table = pmap_kernel()->pm_root;
	pcb->pcb_sr = PSL_S;
	l->l_addr = pcb;
	l->l_md.md_pcb = pcb;
	l->l_md.md_kstack_base = base + roundup(sizeof(*pcb), 16);
	l->l_md.md_kstack_size = size - roundup(sizeof(*pcb), 16);
	l->l_md.md_startup = 0;
	l->l_md.md_irq_hold = 0;
	return 1;
}

int
rvm_cpu_lwp_discard(struct lwp *l)
{
	if (!mode_ok() || curcpu()->ci_cpl != IPL_NONE || l == NULL ||
	    l == curlwp || l->l_addr == NULL || l->l_addr == &boot_pcb)
		return 0;
	if (!rvm_uarea_unbind(l)) return 0;
	l->l_addr = NULL;
	l->l_md.md_pcb = NULL;
	l->l_md.md_kstack_base = 0;
	l->l_md.md_kstack_size = 0;
	l->l_md.md_irq_hold = 0;
	l->l_md.md_startup = 0;
	return 1;
}

struct lwp *
rvm_cpu_switchto(struct lwp *old, struct lwp *next)
{
	struct pcb *oldpcb, *newpcb;
	if (!mode_ok() || curcpu()->ci_cpl != IPL_SCHED || old == NULL || next == NULL ||
	    old != curlwp || (curcpu()->ci_onproc != old && curcpu()->ci_onproc != next) ||
	    old->l_cpu != curcpu() ||
	    next->l_cpu != curcpu() || !(next->l_flag & LW_SYSTEM) ||
	    old->l_nopreempt != 0 || next->l_nopreempt != 0 ||
	    old->l_addr == NULL || next->l_addr == NULL)
		panic(rvm_switch_bad);
	if (old == next)
		return old;
	oldpcb = lwp_getpcb(old); newpcb = lwp_getpcb(next);
	if (old->l_md.md_pcb != oldpcb || next->l_md.md_pcb != newpcb ||
	    newpcb->pcb_mmu_table != pmap_kernel()->pm_root ||
	    !stack_ok(next, newpcb->pcb_gpr[RVM_REG_SP]))
		panic(rvm_switch_bad);
	/* IRQs/caches are off: no observer can see the interval before SP changes. */
	curcpu()->ci_curlwp = next;
	curcpu()->ci_onproc = next;
	__asm volatile("" ::: "memory");
	return rvm_cpu_switch(oldpcb, newpcb, old);
}

/* Bootstrap workers enter directly; cpu_lwp_fork workers use MI startup. */
void __dead
rvm_cpu_lwp_start(void (*fn)(void *), void *arg, struct lwp *previous)
{
	if (!mode_ok() || previous == NULL || previous == curlwp ||
	    curcpu()->ci_onproc != curlwp || curcpu()->ci_cpl != IPL_SCHED)
		panic(rvm_switch_bad);
	if (curlwp->l_md.md_startup != 0)
		rvm_kernel_lwp_startup(previous, curlwp);
	fn(arg);
	panic(rvm_switch_bad); /* Kernel workers must explicitly exit/switch away. */
}

static struct lwp workers[2] __attribute__((section(".data"))) = { 0 };
static struct lwp *controller __attribute__((section(".data"))) = NULL;
static unsigned int progress[2] __attribute__((section(".data"))) = { 0 };

static void
worker(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	volatile uint64_t local[8];
	unsigned int i, n;
	struct lwp *self = curlwp;
	for (i = 0; i < 8; i++) local[i] = 0x52564d5354414300UL + i + id;
	for (n = 0; ; n++) {
		for (i = 0; i < 8; i++) {
			if (local[i] != 0x52564d5354414300UL + i + id || self != curlwp)
				panic(rvm_switch_bad);
		}
		progress[id] = n + 1;
		if (cpu_switchto(self, controller, false) != controller)
			panic(rvm_switch_bad);
	}
}

int
rvm_cpu_switch_verify(void)
{
	vaddr_t stacks[2];
	unsigned int i, n;
	int oldipl;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_cpu_bootstrap_lwp())
		return 0;
	controller = curlwp;
	for (i = 0; i < 2; i++) {
		stacks[i] = uvm_km_alloc(kernel_map, USPACE, 0,
		    UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
		if (stacks[i] == 0)
			return 0;
		memset(&workers[i], 0, sizeof(workers[i]));
		workers[i].l_cpu = curcpu();
		workers[i].l_flag = LW_SYSTEM;
		workers[i].l_stat = LSONPROC;
		progress[i] = 0;
		if (!rvm_cpu_lwp_prepare(&workers[i], stacks[i], USPACE, worker, (void *)(uintptr_t)i))
			return 0;
	}
	oldipl = splsched();
	for (n = 0; n < 16; n++) {
		for (i = 0; i < 2; i++) {
			if (cpu_switchto(controller, &workers[i], false) != &workers[i] ||
			    curlwp != controller || curcpu()->ci_onproc != controller || progress[i] != n + 1)
				return 0;
		}
	}
	splx(oldipl);
	for (i = 0; i < 2; i++) {
		if (!rvm_cpu_lwp_discard(&workers[i]))
			return 0;
		uvm_km_free(kernel_map, stacks[i], USPACE, UVM_KMF_WIRED);
	}
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before &&
	    rvm_boot_get_cr() == RVM_CR_MMU && curcpu()->ci_cpl == IPL_NONE;
}
