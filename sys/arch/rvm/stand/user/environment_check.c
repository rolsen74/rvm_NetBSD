#include "rvm_user.h"
#include <sys/errno.h>

int environment_check(void);
static int callback_fd, dso_a, dso_b, calls[4], count;
static void record(void *value) { calls[count++] = (int)(intptr_t)value; }
static void mark_c(void) { write(callback_fd, "C", 1); }
static void mark_a(void) { write(callback_fd, "A", 1); }
static void mark_b(void) { write(callback_fd, "B", 1); if (atexit(mark_c)) _exit(1); }
static void mark_z(void) { write(callback_fd, "Z", 1); }
extern int __cxa_atexit(void (*)(void *), void *, void *);
extern void __cxa_finalize(void *);
static char by_reference[] = "RVM_REFERENCE=original";

int
environment_check(void)
{
	if (!getenv("RVM_RUNTIME") || strcmp(getenv("RVM_RUNTIME"), "1") ||
	    setenv("RVM_VALUE", "first", 1) || setenv("RVM_VALUE", "second", 0) ||
	    strcmp(getenv("RVM_VALUE"), "first") || setenv("RVM_VALUE", "longer replacement", 1) ||
	    strcmp(getenv("RVM_VALUE"), "longer replacement") ||
	    unsetenv("RVM_VALUE") || getenv("RVM_VALUE") != 0 ||
	    setenv("BAD=NAME", "x", 1) != -1 || errno != EINVAL)
		return 150;
	if (putenv(by_reference) || strcmp(getenv("RVM_REFERENCE"), "original")) return 150;
	by_reference[14] = 'O';
	if (strcmp(getenv("RVM_REFERENCE"), "Original") || unsetenv("RVM_REFERENCE")) return 150;
	for (unsigned i = 0; i < 64; i++) {
		char name[8] = "RVM_AA";
		name[4] = 'A' + i / 26; name[5] = 'A' + i % 26;
		if (setenv(name, "value", 1) || strcmp(getenv(name), "value")) return 151;
	}
	for (unsigned i = 0; i < 64; i++) {
		char name[8] = "RVM_AA";
		name[4] = 'A' + i / 26; name[5] = 'A' + i % 26;
		if (unsetenv(name) || getenv(name)) return 151;
	}
	if (__cxa_atexit(record, (void *)1, &dso_a) ||
	    __cxa_atexit(record, (void *)2, &dso_b) ||
	    __cxa_atexit(record, (void *)3, &dso_a)) return 152;
	__cxa_finalize(&dso_a);
	if (count != 2 || calls[0] != 3 || calls[1] != 1) return 152;
	__cxa_finalize(0); __cxa_finalize(0);
	if (count != 3 || calls[2] != 2) return 152;
	int fds[2], status;
	if (pipe(fds)) return 153;
	pid_t pid = fork();
	if (pid == 0) {
		close(fds[0]); callback_fd = fds[1];
		for (unsigned i = 0; i < 40; i++) if (atexit(mark_z)) _exit(1);
		if (atexit(mark_a) || atexit(mark_b)) _exit(1);
		exit(23);
	}
	close(fds[1]);
	char data[43]; size_t done = 0;
	while (done < sizeof(data)) {
		ssize_t n = read(fds[0], data + done, sizeof(data) - done);
		if (n <= 0) return 153;
		done += n;
	}
	close(fds[0]);
	if (pid < 0 || waitpid(pid, &status, 0) != pid || status != (23 << 8) ||
	    data[0] != 'B' || data[1] != 'C' || data[2] != 'A') return 153;
	for (unsigned i = 3; i < sizeof(data); i++) if (data[i] != 'Z') return 153;
	puts("NetBSD/RVM: libc environment, DSO finalization and dynamic/reentrant atexit order verified");
	return 0;
}
