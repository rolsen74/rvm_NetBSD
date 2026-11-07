/* Recover only expected data faults at the guarded assembly copy sites. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/errno.h>
#include <machine/boot.h>
#include <machine/bootcopy.h>
#include <machine/frame.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/pmap.h>
#include <machine/asyncirq.h>
void rvm_copyfault_entry(void);
int rvm_kcopy_bytes(const void *, void *, size_t);
extern char rvm_kcopy_read[], rvm_kcopy_write[];
extern char rvm_kcopy_read_back[], rvm_kcopy_write_back[], rvm_kcopy_fault[];
static unsigned int initialized __attribute__((section(".data"))) = 0;
static unsigned long faults __attribute__((section(".data"))) = 0;
const char rvm_copy_bad[] = "Invalid RVM kernel copy context";
__CTASSERT(EFAULT == RVM_COPY_EFAULT);
__CTASSERT(sizeof(struct rvm_copyfault_frame) == 544);
int rvm_kcopy_ready(void)
{
	return initialized && rvm_boot_vectors[RVM_VECTOR_MMU_READ] == rvm_copyfault_entry &&
	    rvm_boot_vectors[RVM_VECTOR_MMU_WRITE] == rvm_copyfault_entry &&
	    rvm_boot_vectors[RVM_VECTOR_MMU_ENABLE] == rvm_copyfault_entry;
}
int rvm_kcopy_init(void)
{
	if (rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() || curlwp == NULL ||
	    curlwp != curcpu()->ci_onproc || curlwp->l_addr == NULL) return 0;
	if (initialized) return rvm_kcopy_ready();
	rvm_boot_vectors[RVM_VECTOR_MMU_READ] = rvm_copyfault_entry;
	rvm_boot_vectors[RVM_VECTOR_MMU_WRITE] = rvm_copyfault_entry;
	rvm_boot_vectors[RVM_VECTOR_MMU_ENABLE] = rvm_copyfault_entry;
	initialized = 1;
	return 1;
}
unsigned long rvm_kcopy_faults(void) { return faults; }
int kcopy(const void *src, void *dst, size_t len)
{
	struct pcb *pcb;
	void *oldfault;
	unsigned int guard;
	int result;
	if (len == 0) return 0;
	if (len - 1 > UINTPTR_MAX - (uintptr_t)src ||
	    len - 1 > UINTPTR_MAX - (uintptr_t)dst) return EFAULT;
	guard = rvm_intr_suspend();
	if (!rvm_kcopy_ready() || cpu_intr_p() || curlwp != curcpu()->ci_onproc ||
#ifndef RVM_KERNEL
	    !(curlwp->l_flag & LW_SYSTEM) ||
#endif
	    curlwp->l_addr == NULL ||
	    curlwp->l_md.md_pcb != curlwp->l_addr) panic(rvm_copy_bad);
	pcb = lwp_getpcb(curlwp); oldfault = pcb->pcb_onfault;
	pcb->pcb_onfault = rvm_kcopy_fault;
	__asm volatile("" ::: "memory");
	result = rvm_kcopy_bytes(src, dst, len);
	__asm volatile("" ::: "memory");
	pcb->pcb_onfault = oldfault;
	rvm_intr_resume(guard); return result;
}
void rvm_copyfault_dispatch(struct rvm_copyfault_frame *frame)
{
	struct lwp *l = curlwp;
	struct rvm_exceptionframe *saved = &frame->saved;
	uint64_t header = saved->header;
	unsigned int vector = (header >> 32) & 0xffff;
	vaddr_t base, end, address = (vaddr_t)frame;
	if (!rvm_kcopy_ready() || rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() ||
	    l == NULL || l != curcpu()->ci_onproc || l->l_addr == NULL ||
	    l->l_md.md_pcb != l->l_addr || l->l_md.md_irq_hold == 0 ||
	    ((struct pcb *)lwp_getpcb(l))->pcb_onfault != rvm_kcopy_fault ||
	    (header >> 48) != RVM_EF_VERSION_MMU_FAULT || USERMODE(header) ||
	    (header & PSL_H) || saved->root != pmap_kernel()->pm_root ||
	    (vector != RVM_VECTOR_MMU_READ && vector != RVM_VECTOR_MMU_WRITE &&
	    vector != RVM_VECTOR_MMU_ENABLE) ||
	    (saved->pc != (vaddr_t)rvm_kcopy_read && saved->pc != (vaddr_t)rvm_kcopy_write &&
	    saved->pc != (vaddr_t)rvm_kcopy_read_back && saved->pc != (vaddr_t)rvm_kcopy_write_back))
		rvm_boot_trap(saved->r);
	base = l->l_md.md_kstack_base; end = base + l->l_md.md_kstack_size;
	if (base == 0 || end < base || address < base || address > end ||
	    end - address < sizeof(*frame) || (address & STACK_ALIGNBYTES))
		rvm_boot_trap(saved->r);
	faults++; saved->pc = (vaddr_t)rvm_kcopy_fault;
}

int rvm_kcopy_verify(void)
{
	unsigned char source[8] = { 11, 22, 33, 44, 55, 66, 77, 88 }, dest[8];
	struct pcb *pcb = lwp_getpcb(curlwp);
	void *old = pcb->pcb_onfault;
	unsigned long before = faults;
	int ok;
	pcb->pcb_onfault = dest;
	ok = kcopy(source, dest, sizeof(source)) == 0 && dest[0] == 11 && dest[7] == 88 &&
	    pcb->pcb_onfault == dest && kcopy(NULL, dest, 1) == EFAULT &&
	    pcb->pcb_onfault == dest && faults == before + 1;
	pcb->pcb_onfault = old;
	return ok;
}
