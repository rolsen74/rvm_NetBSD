/* Initial register setup and MI fork/LWP/spawn return hooks. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/exec.h>
#include <sys/ucontext.h>
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/frame.h>
#include <machine/user.h>

void
setregs(struct lwp *l, struct exec_package *package, vaddr_t sp)
{
	struct trapframe *tf = l->l_md.md_tf;
	KASSERT(tf != NULL);
	memset(tf, 0, sizeof(*tf));
	tf->tf_usp = sp;
	/* FHR crt0 passes the MI argument descriptor to static libc/csu. */
	tf->tf_r[5] = l->l_proc->p_psstrp;
	tf->tf_pc = package->ep_entry;
	tf->tf_mmu_table = l->l_proc->p_vmspace->vm_map.pmap->pm_root;
	tf->tf_ssp = (uintptr_t)tf;
}
void
md_child_return(struct lwp *l)
{
	l->l_md.md_tf->tf_r[0] = 0;
	l->l_md.md_tf->tf_r[1] = 1;
	l->l_md.md_tf->tf_sr &= ~PSL_C;
}
void
cpu_spawn_return(struct lwp *l)
{
	rvm_user_return(l);
	rvm_user_resume(l->l_md.md_tf);
}
void
startlwp(void *arg)
{
	ucontext_t *uc = arg;
	int error = cpu_setmcontext(curlwp, &uc->uc_mcontext, uc->uc_flags);
	kmem_free(uc, sizeof(*uc));
	if (error != 0) {
		mutex_enter(curproc->p_lock);
		exit1(curlwp, 127, 0);
	}
	rvm_user_return(curlwp);
	rvm_user_resume(curlwp->l_md.md_tf);
}
