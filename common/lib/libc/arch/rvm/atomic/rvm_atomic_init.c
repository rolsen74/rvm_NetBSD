/* Register RAS regions for the static, single-LWP RVM userland. */
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/ras.h>
#include <stdlib.h>
#include "extern.h"
RAS_DECL(_atomic_cas_8);
RAS_DECL(_atomic_cas_16);
RAS_DECL(_atomic_cas_32);
RAS_DECL(_atomic_cas_64);

void
__libc_atomic_init(void)
{
	if (rasctl(RAS_ADDR(_atomic_cas_8), RAS_SIZE(_atomic_cas_8),
	    RAS_INSTALL) == -1)
		abort();
	if (rasctl(RAS_ADDR(_atomic_cas_16), RAS_SIZE(_atomic_cas_16),
	    RAS_INSTALL) == -1)
		abort();
	if (rasctl(RAS_ADDR(_atomic_cas_32), RAS_SIZE(_atomic_cas_32),
	    RAS_INSTALL) == -1)
		abort();
	if (rasctl(RAS_ADDR(_atomic_cas_64), RAS_SIZE(_atomic_cas_64),
	    RAS_INSTALL) == -1)
		abort();
}
