/* Guest-visible entropy, executable mappings and physical I-cache coherence. */
#include "rvm_user.h"
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/signal.h>
#include <sys/syscall.h>
#include <machine/sysarch.h>
extern const char rvm_cache_code_a[], rvm_cache_code_a_end[];
extern const char rvm_cache_code_b[], rvm_cache_code_b_end[];
int platform_check(void);
static long sync_code(void *address, size_t length) {
	struct rvm_sync_icache_args range = { (uintptr_t)address, length };
	return __rvm_rawcall(SYS_sysarch, RVM_SYNC_ICACHE, (long)&range, 0, 0, 0, 0, 0, 0);
}
int
platform_check(void)
{
	unsigned char first[32], second[32];
	if (__rvm_rawcall(SYS_getrandom, (long)first, sizeof(first), 0, 0, 0, 0, 0, 0) != sizeof(first) ||
	    __rvm_rawcall(SYS_getrandom, (long)second, sizeof(second), 0, 0, 0, 0, 0, 0) != sizeof(second) ||
	    memcmp(first, second, sizeof(first)) == 0 ||
	    __rvm_rawcall(SYS_getrandom, 1, 32, 0, 0, 0, 0, 0, 0) != -EFAULT)
		return 154;
	size_t n = rvm_cache_code_a_end - rvm_cache_code_a;
	if (n == 0 || n != (size_t)(rvm_cache_code_b_end - rvm_cache_code_b)) return 155;
	char *code = mmap(0, 65536, PROT_READ | PROT_WRITE | PROT_EXEC,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	if (code == MAP_FAILED) return 155;
	long (*call)(void) = (void *)code;
	memcpy(code, rvm_cache_code_a, n);
	if (sync_code(code, n)) return 155;
	if (call() != 29) return 155;
	memcpy(code, rvm_cache_code_b, n);
	if (sync_code(code, n) || call() != 43) return 155;
	int status; pid_t pid = fork();
	if (pid == 0) {
		memcpy(code, rvm_cache_code_a, n);
		if (sync_code(code, n) || call() != 29) _exit(1);
		_exit(37);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid || status != (37 << 8) || call() != 43)
		return 156;
	if (__rvm_rawcall(SYS_mprotect, (long)code, 65536, PROT_READ | PROT_WRITE, 0, 0, 0, 0, 0))
		return 157;
	pid = fork();
	if (pid == 0) { (void)call(); _exit(1); }
	if (pid < 0 || waitpid(pid, &status, 0) != pid || (status & 127) != SIGSEGV)
		return 157;
	if (munmap(code, 65536) ||
	    mmap(code, 65536, PROT_READ | PROT_WRITE | PROT_EXEC,
	    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0) != code) return 158;
	memcpy(code, rvm_cache_code_a, n);
	if (sync_code(code, n) || call() != 29 || sync_code((void *)1, 65536) != -EINVAL || munmap(code, 65536))
		return 158;
	puts("NetBSD/RVM: host entropy/getrandom and executable cache sync, COW, warm NX and VA reuse verified");
	return 0;
}
