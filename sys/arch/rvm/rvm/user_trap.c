/* Normalize hardware frames at the user/kernel boundary. Kernel data faults
 * continue to use the scoped kcopy recovery path. All user returns pass MI
 * signal/reschedule handling, with IRQs enabled while sleeping is permitted.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/userret.h>
#include <sys/ras.h>
#include <uvm/uvm.h>
#include <machine/frame.h>
#include <machine/user.h>
#include <machine/asyncirq.h>
#include <machine/boot.h>
#include <machine/bootcopy.h>
#include <machine/switch.h>
#include <machine/pmap_private.h>
#include <machine/mmio.h>
#include <machine/cache.h>

void rvm_user_exception_entry(void);
static unsigned long user_interrupts;
unsigned long rvm_user_interrupts(void) { return user_interrupts; }
__CTASSERT(offsetof(struct trapframe, tf_usp) == RVM_TF_USP);
__CTASSERT(offsetof(struct trapframe, tf_pc) == RVM_TF_PC);
__CTASSERT(offsetof(struct trapframe, tf_mmu_table) == RVM_TF_ROOT);
__CTASSERT(offsetof(struct trapframe, tf_sr) == RVM_TF_SR);
__CTASSERT(sizeof(struct trapframe) == RVM_TF_SIZE);
__CTASSERT(offsetof(struct trapframe, tf_fp) == RVM_TF_FP);
__CTASSERT(offsetof(struct fpreg, f_fpcr) == RVM_FPREG_CR);
__CTASSERT(offsetof(struct fpreg, f_fpsr) == RVM_FPREG_SR);
__CTASSERT(sizeof(struct fpreg) == RVM_FPREG_SIZE);

void
rvm_user_vectors_init(void)
{
	/* MMU read/write/invalid also serve kernel kcopy, which forwards user faults. */
	const unsigned int vectors[] = { 0, 1, 2, 6, 7, 8, 9, 10, 11 };
	for (size_t i = 0; i < __arraycount(vectors); i++)
		rvm_boot_vectors[vectors[i]] = rvm_user_exception_entry;
}

static struct trapframe *
import_frame(struct rvm_exceptionframe *raw)
{
	struct lwp *l = curlwp;
	struct trapframe *tf = l->l_md.md_tf;
	if (!USERMODE(raw->header) || tf == NULL || l->l_flag & LW_SYSTEM ||
	    raw->root != l->l_proc->p_vmspace->vm_map.pmap->pm_root)
		rvm_boot_trap(raw->r);
	memcpy(tf->tf_r, raw->r, sizeof(tf->tf_r));
	/* Kernel C code uses integer registers only. Capture the user bank
	 * before enabling interrupts or allowing a scheduler switch. */
	rvm_fpu_save(&tf->tf_fp);
	tf->tf_usp = rvm_get_user_sp();
	tf->tf_ssp = (uintptr_t)tf;
	tf->tf_pc = raw->pc;
	tf->tf_sr = raw->header & (PSL_FLAG_MASK | PSL_IPL_MASK);
	tf->tf_mmu_table = raw->root;
	tf->tf_vector = (raw->header >> 32) & 0xffff;
	tf->tf_version = raw->header >> 48;
	tf->tf_level = PSL_IPL(raw->header);
	tf->tf_fault_va = tf->tf_fault_pa = 0;
	if (tf->tf_version == RVM_EF_VERSION_MMU_FAULT) {
		const struct rvm_copyfault_frame *fault = (const void *)raw;
		tf->tf_fault_va = fault->virtual_address;
		tf->tf_fault_pa = fault->physical_address;
	}
	return tf;
}

void
rvm_user_return(struct lwp *l)
{
	KASSERT(l == curlwp && !(l->l_flag & LW_SYSTEM));
	l->l_md.md_astpending = 0;
	mi_userret(l);
	/* A descheduled user RAS must restart before it can commit a store
	 * based on a value read before another LWP/process ran. Signal frame
	 * capture performs the same adjustment in cpu_getmcontext(). */
	if (l->l_proc->p_raslist != NULL) {
		void *restart = ras_lookup(l->l_proc, (void *)l->l_md.md_tf->tf_pc);
		if (restart != (void *)-1)
			l->l_md.md_tf->tf_pc = (uintptr_t)restart;
	}
	if (rvm_user_pmap_prepare(l->l_proc->p_vmspace->vm_map.pmap) != 0) {
		mutex_enter(l->l_proc->p_lock);
		sigexit(l, SIGKILL);
	}
	l->l_md.md_tf->tf_mmu_table = l->l_proc->p_vmspace->vm_map.pmap->pm_root;
	pmap_activate(l);
}

static void
export_frame(struct rvm_exceptionframe *raw, struct trapframe *tf)
{
	/* Never return with supervisor, halt, priority or reserved status bits. */
	if (tf->tf_pc < VM_MIN_ADDRESS || tf->tf_pc >= VM_MAXUSER_ADDRESS ||
	    tf->tf_usp < VM_MIN_ADDRESS || tf->tf_usp > VM_MAXUSER_ADDRESS ||
	    (tf->tf_sr & ~PSL_ALLCC)) {
		/* User-selected registers must never turn into a kernel panic. */
		rvm_intr_apply();
		mutex_enter(curlwp->l_proc->p_lock);
		sigexit(curlwp, SIGILL);
	}
	memcpy(raw->r, tf->tf_r, sizeof(tf->tf_r));
	raw->pc = tf->tf_pc;
	raw->root = tf->tf_mmu_table = curlwp->l_proc->p_vmspace->vm_map.pmap->pm_root;
	raw->header = (raw->header & UINT64_C(0xffffffff00000000)) | tf->tf_sr;
	rvm_set_user_sp(tf->tf_usp);
	pmap_activate(curlwp);
	rvm_fpu_restore(&tf->tf_fp);
}

void
rvm_user_exception(struct rvm_exceptionframe *raw)
{
	uint64_t cr = rvm_intr_disable();
	KASSERT(!USERMODE(raw->header) ||
	    (cr & (RVM_CR_INSTCACHE | RVM_CR_DATACACHE) & ~RVM_USER_CACHE_MASK) == 0);
	/* User fetches stay uncached; only the shared immutable kernel text
	 * uses I-cache. Restore the original CR before returning through RTE. */
	rvm_irq_set_cr(RVM_KERNEL_CR);
	if (!USERMODE(raw->header) && rvm_mmio_recover(raw)) {
		rvm_irq_set_cr(cr);
		return;
	}
	struct lwp *l = curlwp;
	struct trapframe *tf = import_frame(raw);
	ksiginfo_t ksi;
	int error;
	vm_prot_t prot;
	vaddr_t va;
	rvm_intr_apply();
	if (tf->tf_vector == RVM_VECTOR_SYSCALL &&
	    tf->tf_version == RVM_EF_VERSION_SYSCALL) {
		l->l_proc->p_md.md_syscall(tf);
	} else if (tf->tf_version == RVM_EF_VERSION_MMU_FAULT &&
	    tf->tf_vector >= RVM_VECTOR_MMU_EXEC && tf->tf_vector <= RVM_VECTOR_MMU_ENABLE) {
		va = tf->tf_fault_va;
		/* An invalid-table fault doesn't identify R/W/X. Populate using an
		 * allowed access; retrying the instruction then checks its exact rights. */
		prot = tf->tf_vector == RVM_VECTOR_MMU_WRITE ? VM_PROT_WRITE :
		    tf->tf_vector == RVM_VECTOR_MMU_EXEC ? VM_PROT_EXECUTE : VM_PROT_READ;
		error = va >= VM_MIN_ADDRESS && va < VM_MAXUSER_ADDRESS ?
		    uvm_fault(&l->l_proc->p_vmspace->vm_map, trunc_page(va), prot) : EFAULT;
		if (tf->tf_vector == RVM_VECTOR_MMU_ENABLE &&
		    va >= VM_MIN_ADDRESS && va < VM_MAXUSER_ADDRESS && error != ENOMEM) {
			if (error != 0)
				error = uvm_fault(&l->l_proc->p_vmspace->vm_map, trunc_page(va), VM_PROT_WRITE);
			if (error != 0 && error != ENOMEM)
				error = uvm_fault(&l->l_proc->p_vmspace->vm_map, trunc_page(va), VM_PROT_EXECUTE);
		}
		if (error != 0) {
#ifdef RVM_USER_FAULT_TRACE
			printf("RVM user fault: pid=%d vector=%u pc=%#lx va=%#lx sp=%#lx\n",
			    (int)l->l_proc->p_pid, (unsigned)tf->tf_vector,
			    (unsigned long)tf->tf_pc, (unsigned long)va,
			    (unsigned long)tf->tf_usp);
#endif
			struct vm_map *map = &l->l_proc->p_vmspace->vm_map;
			bool mapped = false;
			if (va >= VM_MIN_ADDRESS && va < VM_MAXUSER_ADDRESS) {
				vm_map_lock_read(map);
				mapped = uvm_map_checkprot(map, trunc_page(va),
				    trunc_page(va) + PAGE_SIZE, VM_PROT_NONE);
				vm_map_unlock_read(map);
			}
			KSI_INIT_TRAP(&ksi);
			ksi.ksi_signo = SIGSEGV;
			ksi.ksi_code = mapped ? SEGV_ACCERR : SEGV_MAPERR;
			ksi.ksi_addr = (void *)va;
			ksi.ksi_trap = tf->tf_vector;
			trapsignal(l, &ksi);
		}
	} else {
		KSI_INIT_TRAP(&ksi);
		ksi.ksi_signo = tf->tf_vector == RVM_VECTOR_DIVIDE ? SIGFPE : SIGILL;
		ksi.ksi_code = ksi.ksi_signo == SIGFPE ? FPE_INTDIV :
		    tf->tf_vector == RVM_VECTOR_PRIVILEGE ? ILL_PRVOPC : ILL_ILLOPC;
		ksi.ksi_addr = (void *)tf->tf_pc;
		ksi.ksi_trap = tf->tf_vector;
		trapsignal(l, &ksi);
	}
	rvm_user_return(l);
	rvm_intr_disable();
	export_frame(raw, tf);
	rvm_irq_set_cr(cr);
}

void
rvm_user_irq_return(struct rvm_exceptionframe *raw)
{
	if (!USERMODE(raw->header)) return;
	user_interrupts++;
	struct trapframe *tf = import_frame(raw);
	/* Clock bookkeeping is finished. Do not carry the device's hardware
	 * priority into another LWP if mi_userret schedules before the RTE. */
	rvm_irq_set_sr(PSL_S);
	KASSERT((rvm_boot_get_sr() & PSL_IPL_MASK) == 0);
	rvm_intr_apply();
	rvm_user_return(curlwp);
	rvm_intr_disable();
	export_frame(raw, tf);
}
