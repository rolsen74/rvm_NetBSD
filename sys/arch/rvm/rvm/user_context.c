/* Integer process contexts. Trap entry/return and signal delivery are separate. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/ucontext.h>
#include <sys/ras.h>
#include <uvm/uvm.h>
#include <machine/frame.h>
#include <machine/reg.h>

int
cpu_mcontext_validate(struct lwp *l __unused, const mcontext_t *mc)
{
	uint64_t pc = mc->__gregs[_REG_PC], sp = mc->__gregs[_REG_SP];
	/* RVM instructions have variable byte lengths, so PC need not be aligned. */
	if (pc < VM_MIN_ADDRESS || pc >= VM_MAXUSER_ADDRESS ||
	    sp < VM_MIN_ADDRESS || sp > VM_MAXUSER_ADDRESS ||
	    (sp & STACK_ALIGNBYTES) != 0 ||
	    (mc->__gregs[_REG_SR] & ~PSL_ALLCC) != 0) return EINVAL;
	return 0;
}

void
cpu_getmcontext(struct lwp *l, mcontext_t *mc, unsigned int *flags)
{
	const struct trapframe *tf = l->l_md.md_tf;
	void *restart;
	KASSERT(tf != NULL && USERMODE(tf->tf_sr));
	memcpy(mc->__gregs, tf->tf_r, sizeof(tf->tf_r));
	mc->__gregs[_REG_SP] = tf->tf_usp;
	mc->__gregs[_REG_PC] = tf->tf_pc;
	mc->__gregs[_REG_SR] = tf->tf_sr & PSL_ALLCC;
	if (l->l_proc->p_raslist != NULL &&
	    (restart = ras_lookup(l->l_proc, (void *)tf->tf_pc)) != (void *)-1)
		mc->__gregs[_REG_PC] = (uintptr_t)restart;
	*flags |= _UC_CPU;
}

int
cpu_setmcontext(struct lwp *l, const mcontext_t *mc, unsigned int flags)
{
	struct trapframe *tf = l->l_md.md_tf;
	if (tf == NULL || !USERMODE(tf->tf_sr)) return EINVAL;
	if (flags & (_UC_FPU | _UC_TLSBASE)) return EINVAL;
	if ((flags & (_UC_SETSTACK | _UC_CLRSTACK)) == (_UC_SETSTACK | _UC_CLRSTACK))
		return EINVAL;
	if (flags & _UC_CPU) {
		int error = cpu_mcontext_validate(l, mc);
		if (error != 0) return error;
		memcpy(tf->tf_r, mc->__gregs, sizeof(tf->tf_r));
		tf->tf_usp = mc->__gregs[_REG_SP];
		tf->tf_pc = mc->__gregs[_REG_PC];
		tf->tf_sr = mc->__gregs[_REG_SR];
	}
	if (flags & (_UC_SETSTACK | _UC_CLRSTACK)) {
		mutex_enter(l->l_proc->p_lock);
		if (flags & _UC_SETSTACK) l->l_sigstk.ss_flags |= SS_ONSTACK;
		else l->l_sigstk.ss_flags &= ~SS_ONSTACK;
		mutex_exit(l->l_proc->p_lock);
	}
	return 0;
}

int
process_read_regs(struct lwp *l, struct reg *regs)
{
	const struct trapframe *tf = l->l_md.md_tf;
	if (tf == NULL || !USERMODE(tf->tf_sr)) return EINVAL;
	memcpy(regs->r_regs, tf->tf_r, sizeof(tf->tf_r));
	regs->r_regs[RVM_REG_SP] = tf->tf_usp;
	regs->r_pc = tf->tf_pc;
	regs->r_sr = tf->tf_sr & PSL_ALLCC;
	return 0;
}
int
process_write_regs(struct lwp *l, const struct reg *regs)
{
	mcontext_t mc;
	memcpy(mc.__gregs, regs->r_regs, sizeof(regs->r_regs));
	mc.__gregs[_REG_PC] = regs->r_pc;
	mc.__gregs[_REG_SR] = regs->r_sr;
	return cpu_setmcontext(l, &mc, _UC_CPU);
}
int
process_set_pc(struct lwp *l, void *pc)
{
	struct trapframe *tf = l->l_md.md_tf;
	if (tf == NULL || !USERMODE(tf->tf_sr) || (vaddr_t)pc < VM_MIN_ADDRESS ||
	    (vaddr_t)pc >= VM_MAXUSER_ADDRESS) return EINVAL;
	tf->tf_pc = (vaddr_t)pc;
	return 0;
}
