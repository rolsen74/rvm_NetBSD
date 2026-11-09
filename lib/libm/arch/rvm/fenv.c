/* Floating-point environment for the RVM v0000 binary64 FPU.
 * The hardware accumulates exceptions but has no exception trap enables.
 */
#include <sys/cdefs.h>
#include "../../src/namespace.h"
#include <fenv.h>
#include <machine/fpu.h>

#ifdef __weak_alias
__weak_alias(feclearexcept, _feclearexcept)
__weak_alias(fedisableexcept, _fedisableexcept)
__weak_alias(feenableexcept, _feenableexcept)
__weak_alias(fegetenv, _fegetenv)
__weak_alias(fegetexcept, _fegetexcept)
__weak_alias(fegetexceptflag, _fegetexceptflag)
__weak_alias(fegetround, _fegetround)
__weak_alias(feholdexcept, _feholdexcept)
__weak_alias(feraiseexcept, _feraiseexcept)
__weak_alias(fesetenv, _fesetenv)
__weak_alias(fesetexceptflag, _fesetexceptflag)
__weak_alias(fesetround, _fesetround)
__weak_alias(fetestexcept, _fetestexcept)
__weak_alias(feupdateenv, _feupdateenv)
#endif

uint64_t __rvm_fpcr_read(void);
uint64_t __rvm_fpsr_read(void);
void __rvm_fpcr_write(uint64_t);
void __rvm_fpsr_write(uint64_t);

const fenv_t __fe_dfl_env = { 0, 0 };

int
feclearexcept(int excepts)
{
	__rvm_fpsr_write(__rvm_fpsr_read() &
	    ~(uint64_t)(excepts & FE_ALL_EXCEPT));
	return 0;
}

int
fegetexceptflag(fexcept_t *flagp, int excepts)
{
	*flagp = __rvm_fpsr_read() & (excepts & FE_ALL_EXCEPT);
	return 0;
}

int
feraiseexcept(int excepts)
{
	__rvm_fpsr_write(__rvm_fpsr_read() | (excepts & FE_ALL_EXCEPT));
	return 0;
}

int
fesetexceptflag(const fexcept_t *flagp, int excepts)
{
	uint64_t mask = excepts & FE_ALL_EXCEPT;
	__rvm_fpsr_write((__rvm_fpsr_read() & ~mask) | (*flagp & mask));
	return 0;
}

int
fetestexcept(int excepts)
{
	return __rvm_fpsr_read() & (excepts & FE_ALL_EXCEPT);
}

int
fegetround(void)
{
	return __rvm_fpcr_read();
}

int
fesetround(int round)
{
	if ((unsigned int)round > RVM_FPCR_MASK)
		return -1;
	__rvm_fpcr_write(round);
	return 0;
}

int
fegetenv(fenv_t *envp)
{
	envp->__fpcr = __rvm_fpcr_read();
	envp->__fpsr = __rvm_fpsr_read();
	return 0;
}

int
feholdexcept(fenv_t *envp)
{
	fegetenv(envp);
	feclearexcept(FE_ALL_EXCEPT);
	return 0;
}

int
fesetenv(const fenv_t *envp)
{
	/* Validate both words before changing either hardware register. */
	if ((envp->__fpcr & ~(uint64_t)RVM_FPCR_MASK) ||
	    (envp->__fpsr & ~(uint64_t)RVM_FPSR_MASK))
		return -1;
	__rvm_fpcr_write(envp->__fpcr);
	__rvm_fpsr_write(envp->__fpsr);
	return 0;
}

int
feupdateenv(const fenv_t *envp)
{
	int flags = fetestexcept(FE_ALL_EXCEPT);
	if (fesetenv(envp))
		return -1;
	return feraiseexcept(flags);
}

int
feenableexcept(int mask)
{
	/* Do not claim to enable traps that the CPU cannot deliver. */
	return (mask & FE_ALL_EXCEPT) ? -1 : 0;
}

int
fedisableexcept(int mask)
{
	(void)mask;
	return 0;
}

int
fegetexcept(void)
{
	return 0;
}
