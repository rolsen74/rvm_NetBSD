/* Native siginfo v2: C handler arguments in R2/R3/R4; a return address and
 * eight argument homes on the user stack. R10 carries ucontext to the supplied
 * trampoline, which restores it with setcontext(2). No kernel trampoline.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/ucontext.h>
#include <uvm/uvm.h>
#include <machine/frame.h>

struct rvm_signal_frame {
	uint64_t return_pc;
	uint64_t argument_homes[8];
	siginfo_t info;
	ucontext_t context;
};

void
sendsig_siginfo(const struct ksiginfo *ksi, const sigset_t *mask)
{
	struct lwp *l = curlwp;
	struct proc *p = l->l_proc;
	struct trapframe *tf = l->l_md.md_tf;
	struct rvm_signal_frame frame;
	const int signo = ksi->ksi_signo;
	vaddr_t top, address, catcher, trampoline;
	bool alternate;
	int error;

	KASSERT(mutex_owned(p->p_lock));
	catcher = (vaddr_t)SIGACTION(p, signo).sa_handler;
	trampoline = (vaddr_t)p->p_sigacts->sa_sigdesc[signo].sd_tramp;
	alternate = (l->l_sigstk.ss_flags & (SS_DISABLE | SS_ONSTACK)) == 0 &&
	    (SIGACTION(p, signo).sa_flags & SA_ONSTACK) != 0;
	top = alternate ? (vaddr_t)l->l_sigstk.ss_sp + l->l_sigstk.ss_size : tf->tf_usp;
	if (top < VM_MIN_ADDRESS + sizeof(frame) || top > VM_MAXUSER_ADDRESS ||
	    (alternate && top < (vaddr_t)l->l_sigstk.ss_sp)) sigexit(l, SIGILL);
	address = (top - sizeof(frame)) & ~(vaddr_t)STACK_ALIGNBYTES;
	memset(&frame, 0, sizeof(frame));
	frame.return_pc = trampoline;
	frame.info._info = ksi->ksi_info;
	frame.context.uc_flags = _UC_SIGMASK |
	    ((l->l_sigstk.ss_flags & SS_ONSTACK) ? _UC_SETSTACK : _UC_CLRSTACK);
	frame.context.uc_sigmask = *mask;
	frame.context.uc_link = l->l_ctxlink;
	sendsig_reset(l, signo);
	mutex_exit(p->p_lock);
	cpu_getmcontext(l, &frame.context.uc_mcontext, &frame.context.uc_flags);
	error = copyout(&frame, (void *)address, sizeof(frame));
	mutex_enter(p->p_lock);
	if (error != 0) sigexit(l, SIGILL);
	tf->tf_pc = catcher;
	tf->tf_usp = address;
	tf->tf_sr = 0;
	tf->tf_r[2] = signo;
	tf->tf_r[3] = address + offsetof(struct rvm_signal_frame, info);
	tf->tf_r[4] = address + offsetof(struct rvm_signal_frame, context);
	tf->tf_r[10] = tf->tf_r[4];
	if (alternate) l->l_sigstk.ss_flags |= SS_ONSTACK;
}
