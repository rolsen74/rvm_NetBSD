/* MI LWP stack setup and the shared-supervisor-pmap context switch. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/atomic.h>
#include <uvm/uvm.h>
#include <machine/pcb.h>
#include <machine/frame.h>
#include <machine/switch.h>
#include <machine/asyncirq.h>

struct lwp *rvm_cpu_switch(struct pcb *, struct pcb *, struct lwp *);
void rvm_lwp_trampoline(void);
void rvm_cpu_lwp_start(void (*)(void *), void *, struct lwp *);
__CTASSERT(offsetof(struct pcb, pcb_gpr[63]) == RVM_PCB_SP);
__CTASSERT(offsetof(struct pcb, pcb_ssp) == RVM_PCB_SSP);
__CTASSERT(offsetof(struct pcb, pcb_pc) == RVM_PCB_PC);

void
cpu_lwp_fork(struct lwp *parent, struct lwp *child, void *stack __unused,
    size_t stacksize __unused, void (*func)(void *), void *arg)
{
	struct pcb *pcb = child->l_addr;
	vaddr_t top = (vaddr_t)pcb + USPACE;
	struct trapframe *tf = (void *)((top - sizeof(*tf)) & ~(vaddr_t)15);
	uint64_t *frame = (void *)((vaddr_t)tf - RVM_SWITCH_SIZE);

	KASSERT(parent == curlwp);
	KASSERT(pcb != NULL && func != NULL);
	memset(pcb, 0, sizeof(*pcb));
	memset(tf, 0, sizeof(*tf));
	if (parent->l_md.md_tf != NULL) *tf = *parent->l_md.md_tf;
	memset(frame, 0, RVM_SWITCH_SIZE);
	frame[0] = (uintptr_t)func;
	frame[1] = (uintptr_t)arg;
	frame[RVM_SWITCH_NREG] = (uintptr_t)rvm_lwp_trampoline;
	pcb->pcb_gpr[63] = pcb->pcb_ssp = (uintptr_t)frame;
	pcb->pcb_pc = (uintptr_t)rvm_lwp_trampoline;
	pcb->pcb_mmu_table = pmap_kernel()->pm_root;
	child->l_md.md_pcb = pcb;
	child->l_md.md_tf = tf;
	child->l_md.md_kstack_base = (vaddr_t)pcb + roundup(sizeof(*pcb), 16);
	child->l_md.md_kstack_size = USPACE - roundup(sizeof(*pcb), 16);
	child->l_md.md_irq_hold = 0;
}

struct lwp *
cpu_switchto(struct lwp *old, struct lwp *new, bool returning __unused)
{
	KASSERT(curcpu()->ci_cpl >= IPL_SCHED);
	rvm_intr_disable();
	curcpu()->ci_curlwp = new;
	return rvm_cpu_switch(old->l_addr, new->l_addr, old);
}

void
rvm_cpu_lwp_start(void (*func)(void *), void *arg, struct lwp *previous)
{
	lwp_startup(previous, curlwp);
	func(arg);
	lwp_exit(curlwp);
	panic("RVM LWP exit returned");
}

void
cpu_proc_fork(struct proc *parent, struct proc *child)
{
	child->p_md = parent->p_md;
}

void cpu_lwp_free(struct lwp *l __unused, int proc __unused) { }
void cpu_lwp_free2(struct lwp *l __unused) { }
