/* Optional MD operations: a core-file format and sysarch commands have not
 * been assigned. Unsupported requests return errors in every configuration.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/exec.h>
#include <sys/syscallargs.h>

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
