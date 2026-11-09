/* Static FHR startup using the normal NetBSD libc initialization path. */
#include <sys/cdefs.h>
#include <sys/exec.h>
#include <stdlib.h>
#include <unistd.h>
#include "csu-common.h"

static char empty_string[] = "";

extern int main(int, char **, char **);
void __rvm_run_preinit(void);
void __rvm_run_constructors(void);
int __rvm_register_fini(void);
void __rvm_start(long, char **, char **, struct ps_strings *) __dead;

void
__rvm_start(long argc, char **argv, char **envp, struct ps_strings *ps)
{
	if (ps == NULL)
		_exit(127);
	__ps_strings = ps;
	environ = envp;
	__progname = argc > 0 && argv[0] != NULL ? argv[0] : empty_string;
	for (char *p = __progname; *p != '\0'; p++)
		if (*p == '/')
			__progname = p + 1;

	__rvm_run_preinit();
	_libc_init();
	if (__rvm_register_fini() != 0)
		_exit(127);
	__rvm_run_constructors();
	exit(main((int)argc, argv, envp));
}
