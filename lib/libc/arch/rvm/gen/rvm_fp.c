/* RVM legacy IEEE rounding and sticky-exception interfaces. */
#include <sys/cdefs.h>
#include "namespace.h"
#include <float.h>
#include <ieeefp.h>

__weak_alias(fpgetround, _fpgetround)
__weak_alias(fpsetround, _fpsetround)
__weak_alias(fpgetmask, _fpgetmask)
__weak_alias(fpsetmask, _fpsetmask)
__weak_alias(fpgetsticky, _fpgetsticky)
__weak_alias(fpsetsticky, _fpsetsticky)

extern unsigned long __rvm_libc_fpcr_read(void);
extern unsigned long __rvm_libc_fpsr_read(void);
extern void __rvm_libc_fpcr_write(unsigned long);
extern void __rvm_libc_fpsr_write(unsigned long);

fp_rnd
fpgetround(void)
{
	return __rvm_libc_fpcr_read() & 3;
}

fp_rnd
fpsetround(fp_rnd r)
{
	fp_rnd old = fpgetround();

	__rvm_libc_fpcr_write((unsigned)r & 3);
	return old;
}

/* The current FPU records exceptions but has no trap-enable mask. */
fp_except
fpgetmask(void)
{
	return 0;
}

fp_except
fpsetmask(fp_except mask)
{
	(void)mask;
	return 0;
}

fp_except
fpgetsticky(void)
{
	return __rvm_libc_fpsr_read() & FE_ALL_EXCEPT;
}

fp_except
fpsetsticky(fp_except flags)
{
	unsigned long old = __rvm_libc_fpsr_read();

	__rvm_libc_fpsr_write((old & ~(unsigned long)FE_ALL_EXCEPT) |
	    (flags & FE_ALL_EXCEPT));
	return old & FE_ALL_EXCEPT;
}

fp_except
fpresetsticky(fp_except flags)
{
	unsigned long old = __rvm_libc_fpsr_read();

	__rvm_libc_fpsr_write(old & ~(unsigned long)(flags & FE_ALL_EXCEPT));
	return old & FE_ALL_EXCEPT;
}

int
__flt_rounds(void)
{
	static const int map[4] = { 1, 0, 3, 2 };

	return map[fpgetround()];
}
