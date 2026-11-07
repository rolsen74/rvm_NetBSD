/* Explicit EARLY-only boundaries, not implementations of user-mode support.
 * User memory and pmaps are implemented separately; executable loading and
 * user transitions remain pending. Unimplemented execution stops visibly.
 * GENERIC does not link this file, so missing interfaces remain link errors.
 */
#include "opt_rvm.h"
#ifndef RVM_EARLY_KERNEL
#error "Pending user interfaces are only available in the EARLY configuration"
#endif
#define __UFETCHSTORE_PRIVATE
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/exec.h>
#include <sys/signalvar.h>
#include <sys/ucontext.h>
#include <sys/syscallargs.h>
#include <uvm/uvm.h>

void syscall(void);

void syscall(void) { panic("RVM EARLY: user syscall entry is not implemented"); }
void md_child_return(struct lwp *l __unused) { panic("RVM EARLY: child user return"); }
void cpu_spawn_return(struct lwp *l __unused) { panic("RVM EARLY: spawn user return"); }
void startlwp(void *arg __unused) { panic("RVM EARLY: user LWP entry"); }
void
setregs(struct lwp *l __unused, struct exec_package *e __unused,
    vaddr_t sp __unused)
{
	panic("RVM EARLY: executable user register setup");
}
void
sendsig_siginfo(const struct ksiginfo *ksi __unused, const sigset_t *mask __unused)
{
	panic("RVM EARLY: user signal delivery");
}
int
cpu_coredump(struct lwp *l __unused, struct coredump_iostate *io __unused,
    struct core *core __unused)
{
	return EOPNOTSUPP;
}
int
sys_sysarch(struct lwp *l __unused, const struct sys_sysarch_args *args __unused,
    register_t *retval __unused)
{
	return ENOSYS;
}
