/* User instruction synchronization. The core-file format remains unsupported. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/exec.h>
#include <sys/syscallargs.h>
#include <machine/sysarch.h>
#include <machine/cache.h>
#include <machine/vmparam.h>

int
cpu_coredump(struct lwp *l __unused, struct coredump_iostate *io __unused,
    struct core *core __unused)
{
	return EOPNOTSUPP;
}
int
sys_sysarch(struct lwp *l __unused, const struct sys_sysarch_args *args,
    register_t *retval)
{
	struct rvm_sync_icache_args range;
	int error;
	if (SCARG(args, op) != RVM_SYNC_ICACHE) return ENOSYS;
	if ((error = copyin(SCARG(args, parms), &range, sizeof(range))) != 0) return error;
	if (range.address < VM_MIN_ADDRESS || range.address >= VM_MAXUSER_ADDRESS ||
	    range.length > VM_MAXUSER_ADDRESS - range.address) return EINVAL;
	rvm_cache_sync();
	*retval = 0;
	return 0;
}
