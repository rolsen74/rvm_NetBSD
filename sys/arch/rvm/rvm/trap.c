/* Returning supervisor trap path. IRQs, user syscalls and faults stay fatal. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lwp.h>
#include <sys/cpu.h>
#include <machine/boot.h>
#include <machine/pcb.h>
#include <machine/pmap.h>
#include <machine/frame.h>
#include <machine/switch.h>
#include <machine/trap.h>

static unsigned long calls __attribute__((section(".data"))) = 0;
static unsigned int active __attribute__((section(".data"))) = 0;
static unsigned int installed __attribute__((section(".data"))) = 0;
void rvm_supervisor_entry(void);

int
rvm_trap_init(void)
{
	if (rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() ||
	    curcpu()->ci_cpl != IPL_NONE || !rvm_cpu_bootstrap_lwp()) return 0;
	if (installed) return rvm_boot_vectors[RVM_VECTOR_SYSCALL] == rvm_supervisor_entry;
	rvm_boot_vectors[RVM_VECTOR_SYSCALL] = rvm_supervisor_entry;
	__asm volatile("" ::: "memory");
	installed = 1;
	return 1;
}

unsigned long rvm_trap_count(void) { return calls; }

void
rvm_supervisor_dispatch(struct rvm_exceptionframe *frame)
{
	struct lwp *l = curlwp;
	vaddr_t base, end, address = (vaddr_t)frame;
	uint64_t sr = frame->header & (PSL_IPL_MASK | PSL_FLAG_MASK);
	int oldipl = curcpu()->ci_cpl;
	paddr_t pcpa;
	if (!installed || active || rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() ||
	    l == NULL || curcpu()->ci_onproc != l || l->l_cpu != curcpu() ||
	    !(l->l_flag & LW_SYSTEM) || l->l_addr == NULL || l->l_md.md_pcb != l->l_addr ||
	    (frame->header >> 48) != RVM_EF_VERSION_SYSCALL ||
	    ((frame->header >> 32) & 0xffff) != RVM_VECTOR_SYSCALL ||
	    USERMODE(sr) || (sr & PSL_H) || oldipl < IPL_NONE || oldipl > IPL_HIGH ||
	    frame->root != pmap_kernel()->pm_root || frame->pc == 0 ||
	    !pmap_extract(pmap_kernel(), frame->pc, &pcpa) || frame->r[0] != RVM_SVC_PROBE)
		rvm_boot_trap(frame->r);
	base = l->l_md.md_kstack_base;
	end = base + l->l_md.md_kstack_size;
	if (base == 0 || end < base || address < base || address > end ||
	    end - address < RVM_XF_SIZE || (address & STACK_ALIGNBYTES) != 0)
		rvm_boot_trap(frame->r);
	active = 1;
	/* C may change every condition flag and call-clobbered register. The
	 * assembly restores the full register image, and RTE restores saved SR.
	 */
	splhigh();
	frame->r[0] = frame->r[2] ^ RVM_SVC_RESULT;
	calls++;
	splx(oldipl);
	active = 0;
}
