/* Explicit EARLY-only boundaries, not implementations of user-mode support.
 * No user mapping or executable loader exists yet. User memory accesses fail
 * with EFAULT; transitions into unimplemented user execution stop visibly.
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

int
copyin(const void *src __unused, void *dst __unused, size_t len)
{
	return len == 0 ? 0 : EFAULT;
}
int
copyout(const void *src __unused, void *dst __unused, size_t len)
{
	return len == 0 ? 0 : EFAULT;
}
int
copyinstr(const void *src __unused, void *dst __unused, size_t len, size_t *done)
{
	if (done != NULL) *done = 0;
	return len == 0 ? ENAMETOOLONG : EFAULT;
}
int
copyoutstr(const void *src __unused, void *dst __unused, size_t len, size_t *done)
{
	if (done != NULL) *done = 0;
	return len == 0 ? ENAMETOOLONG : EFAULT;
}
#define USER_ACCESS(bits, type) \
int _ufetch_##bits(const type *p __unused, type *v __unused) { return EFAULT; } \
int _ustore_##bits(type *p __unused, type v __unused) { return EFAULT; }
USER_ACCESS(8, uint8_t)
USER_ACCESS(16, uint16_t)
USER_ACCESS(32, uint32_t)
USER_ACCESS(64, uint64_t)

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
void
cpu_getmcontext(struct lwp *l __unused, mcontext_t *mc __unused,
    unsigned int *flags __unused)
{
	panic("RVM EARLY: user context read");
}
int
cpu_setmcontext(struct lwp *l __unused, const mcontext_t *mc __unused,
    unsigned int flags __unused)
{
	return EINVAL;
}
int
cpu_mcontext_validate(struct lwp *l __unused, const mcontext_t *mc __unused)
{
	return EINVAL;
}
int process_read_regs(struct lwp *l __unused, struct reg *r __unused) { return EOPNOTSUPP; }
int process_write_regs(struct lwp *l __unused, const struct reg *r __unused) { return EOPNOTSUPP; }
int process_set_pc(struct lwp *l __unused, void *pc __unused) { return EOPNOTSUPP; }
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
int vmapbuf(struct buf *buf __unused, vsize_t len __unused) { return EFAULT; }
void vunmapbuf(struct buf *buf __unused, vsize_t len __unused)
{
	panic("RVM EARLY: unmapping a user I/O buffer");
}
