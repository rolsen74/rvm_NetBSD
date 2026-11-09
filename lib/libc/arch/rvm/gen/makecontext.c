/* RVM integer-register ABI: eight argument registers and 64 caller-home bytes. */
#include <sys/cdefs.h>
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <ucontext.h>
#include "extern.h"

__CTASSERT(__RVM_CALLER_ARG_HOME__ == 64);
__CTASSERT(sizeof(__greg_t) == 8);

void
makecontext(ucontext_t *uc, void (*function)(void), int argc, ...)
{
	uintptr_t base = (uintptr_t)uc->uc_stack.ss_sp;
	uintptr_t top, address;
	size_t frame;
	__greg_t *registers = uc->uc_mcontext.__gregs;
	__greg_t *stack;
	va_list args;
	int i;

	if (argc < 0 || function == NULL || base == 0 ||
	    uc->uc_stack.ss_size > UINTPTR_MAX - base) {
		errno = EINVAL;
		goto invalid;
	}
	frame = 8 + __RVM_CALLER_ARG_HOME__ +
	    (argc > 8 ? (size_t)(argc - 8) * 8 : 0);
	top = (base + uc->uc_stack.ss_size) & ~(uintptr_t)15;
	if (top < base || top - base < frame) {
		errno = ENOMEM;
		goto invalid;
	}
	address = (top - frame) & ~(uintptr_t)15;
	if (address < base) {
		errno = ENOMEM;
		goto invalid;
	}
	stack = (__greg_t *)address;
	/* The function starts as if called; its return enters the MI resumer. */
	stack[0] = (uintptr_t)_resumecontext;
	registers[_REG_SP] = address;
	registers[_REG_FP] = 0;
	registers[_REG_PC] = (uintptr_t)function;
	registers[_REG_RV] = 0;
	uc->uc_flags |= _UC_CPU;
	va_start(args, argc);
	for (i = 0; i < argc && i < 8; i++)
		registers[2+i] = va_arg(args, __greg_t);
	for (; i < argc; i++)
		stack[1 + __RVM_CALLER_ARG_HOME__/8 + i - 8] = va_arg(args, __greg_t);
	va_end(args);
	return;

invalid:
	/* makecontext has no return value. Reject a later setcontext safely. */
	registers[_REG_PC] = 0;
	uc->uc_flags |= _UC_CPU;
}
