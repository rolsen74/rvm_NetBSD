/* RVM's 64-bit C11 libcall uses the same RAS CAS as public atomics. */
#include <sys/types.h>
#include <sys/atomic.h>
#include <stdbool.h>

bool __atomic_compare_exchange_8(volatile void *, void *, uint64_t,
    bool, int, int);

bool
__atomic_compare_exchange_8(volatile void *memory, void *expected,
    uint64_t desired, bool weak, int success, int failure)
{
	uint64_t *previous = expected;
	uint64_t old = *previous;
	uint64_t actual;

	(void)weak;
	(void)success;
	(void)failure;
	actual = atomic_cas_64(memory, old, desired);
	if (actual == old)
		return true;
	*previous = actual;
	return false;
}
