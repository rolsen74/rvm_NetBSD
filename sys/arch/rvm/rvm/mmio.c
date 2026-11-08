#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <machine/mmio.h>
#include <machine/frame.h>
#include <machine/trap.h>
#include <machine/pcb.h>
#include <machine/pmap.h>
#include <machine/asyncirq.h>

int rvm_mmio_load8(const volatile uint64_t *, uint64_t *);
extern char rvm_mmio_read[], rvm_mmio_fault[];

int
rvm_mmio_peek8(const volatile uint64_t *address, uint64_t *value)
{
	struct pcb *pcb = lwp_getpcb(curlwp);
	unsigned int guard = rvm_intr_suspend();
	void *old = pcb->pcb_onfault;
	int error;
	KASSERT(!cpu_intr_p() && ((uintptr_t)address & 7) == 0);
	pcb->pcb_onfault = rvm_mmio_fault;
	__asm volatile("" ::: "memory");
	error = rvm_mmio_load8(address, value);
	__asm volatile("" ::: "memory");
	pcb->pcb_onfault = old;
	rvm_intr_resume(guard);
	return error;
}

bool
rvm_mmio_recover(struct rvm_exceptionframe *frame)
{
	struct lwp *l = curlwp;
	vaddr_t base, end, address = (uintptr_t)frame;
	if (USERMODE(frame->header) || (frame->header >> 48) != RVM_EF_VERSION_PHYS_FAULT ||
	    ((frame->header >> 32) & 0xffff) != RVM_VECTOR_MEMORY_READ ||
	    frame->pc != (uintptr_t)rvm_mmio_read || l == NULL || cpu_intr_p() ||
	    l->l_addr == NULL || l->l_md.md_irq_hold == 0 ||
	    ((struct pcb *)lwp_getpcb(l))->pcb_onfault != rvm_mmio_fault ||
	    frame->root != pmap_kernel()->pm_root || (rvm_boot_get_cr() & ~RVM_CR_INSTCACHE) != RVM_CR_MMU)
		return false;
	base = l->l_md.md_kstack_base;
	end = base + l->l_md.md_kstack_size;
	if (end < base || address < base || address > end ||
	    end - address < 63 * sizeof(uint64_t) + RVM_EF_SIZE_V1 ||
	    (address & STACK_ALIGNBYTES)) return false;
	frame->pc = (uintptr_t)rvm_mmio_fault;
	return true;
}
