/* Native RVM LP64: number R0, arguments R2..R9 (8-byte slots), results R0/R1.
 * Carry clear means success, carry set means positive errno in R0.
 * Indirect syscall numbers consume R2; a possible eighth argument is at USP.
 * SYSCALL is two bytes and hardware saves the PC following the instruction.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/syscall.h>
#include <sys/syscallargs.h>
#include <sys/syscallvar.h>
#include <machine/frame.h>
#include <machine/user.h>
static unsigned long syscall_restarts;
unsigned long rvm_user_restarts(void) { return syscall_restarts; }

void
syscall_intern(struct proc *p)
{
	p->p_md.md_syscall = rvm_syscall;
}

void
rvm_syscall(struct trapframe *tf)
{
	struct lwp *l = curlwp;
	struct proc *p = l->l_proc;
	const struct sysent *callp;
	register_t args[SYS_MAXSYSARGS] = { 0 }, retval[2] = { 0, 0 };
	uint64_t number = tf->tf_r[0];
	size_t first = 2, count;
	int error;
	static const struct sysent nosys_entry = { .sy_call = sys_nosys };
	__CTASSERT(SYS_MAXSYSARGS == 8);
	curcpu()->ci_data.cpu_nsyscall++;
	if (number == SYS_syscall || number == SYS___syscall) {
		number = tf->tf_r[first++];
	}
	callp = number < p->p_emul->e_nsysent ? &p->p_emul->e_sysent[number] : &nosys_entry;
	/* Entry zero is an MI indirect wrapper, not a zero-argument nosys slot.
	 * Reject recursive indirection and unknown numbers without reading 9 args. */
	if (callp->sy_flags & SYCALL_INDIRECT) callp = &nosys_entry;
	if (callp == &nosys_entry) number = p->p_emul->e_nosys;
	count = callp->sy_argsize / sizeof(register_t);
	if (callp->sy_argsize < 0 || callp->sy_argsize % sizeof(register_t) ||
	    count > SYS_MAXSYSARGS) panic("RVM invalid syscall argument layout");
	for (size_t i = 0; i < MIN(count, 10 - first); i++) args[i] = tf->tf_r[first + i];
	if (count > 10 - first &&
	    (error = copyin((void *)tf->tf_usp, &args[10 - first], sizeof(register_t))) != 0)
		goto done;
	error = sy_invoke(callp, l, args, retval, (int)number);
done:
	switch (error) {
	case 0:
		tf->tf_r[0] = retval[0]; tf->tf_r[1] = retval[1];
		tf->tf_sr &= ~PSL_C;
		break;
	case ERESTART:
		syscall_restarts++;
		tf->tf_pc -= RVM_SYSCALL_SIZE;
		break;
	case EJUSTRETURN:
		break;
	default:
		if (p->p_emul->e_errno != NULL) error = p->p_emul->e_errno[error];
		tf->tf_r[0] = error;
		tf->tf_sr |= PSL_C;
		break;
	}
}
