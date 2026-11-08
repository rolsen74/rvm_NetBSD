/* Bootstrap C utility exercising arguments, write and normal main return. */
#include "rvm_user.h"
int main(int, char **, char **);
int
main(int argc, char **argv, char **envp __attribute__((unused)))
{
	for (int i = 1; i < argc; i++) {
		if (i > 1 && write(1, " ", 1) != 1) return 1;
		size_t size = strlen(argv[i]);
		if (write(1, argv[i], size) != (ssize_t)size) return 1;
	}
	return write(1, "\n", 1) == 1 ? 0 : 1;
}
