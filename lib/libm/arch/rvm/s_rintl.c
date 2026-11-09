/* RVM long double has the same binary64 format and calling convention. */
#include <float.h>
#include <math.h>

_Static_assert(LDBL_MANT_DIG == DBL_MANT_DIG, "RVM binary64 long double");

long double rintl(long double);

long double
rintl(long double x)
{
	return rint(x);
}
