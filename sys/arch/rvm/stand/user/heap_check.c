/* Exercise NetBSD's allocator with the CPU's actual 64 KiB pages. */
#include "rvm_user.h"
#include <sys/errno.h>
#include <sys/stdint.h>

static void *chunks[1024];
static const size_t sizes[] = { 1, 16, 17, 4096, 32767, 32768,
    32769, 65535, 65536, 65537, 131089 };
static void *blocks[sizeof(sizes)/sizeof(sizes[0])];
int heap_check(void);

int
heap_check(void)
{
	if (getpagesize() != 65536) return 140;
	char *initial = sbrk(0);
	if (initial == (void *)-1 || (uintptr_t)initial <= (uintptr_t)chunks ||
	    ((uintptr_t)initial & 65535)) return 140;
	if (sbrk(131089) != initial || sbrk(0) != initial + 131089)
		return 141;
	for (size_t i = 0; i < 131089; i++) {
		if (initial[i] != 0) return 141;
		initial[i] = (char)(i ^ 0x5a);
	}
	if (brk(initial) != 0 || sbrk(0) != initial ||
	    brk(initial - 1) != -1 || errno != EINVAL ||
	    sbrk(INTPTR_MIN) != (void *)-1 || errno != ENOMEM ||
	    sbrk(INTPTR_MAX) != (void *)-1 || sbrk(0) != initial)
		return 142;
	for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
		unsigned char *p = blocks[i] = malloc(sizes[i]);
		if (p == 0 || ((uintptr_t)p & 15)) return 143;
		for (size_t j = 0; j < sizes[i]; j++) p[j] = (unsigned char)(i + j);
	}
	for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
		unsigned char *p = realloc(blocks[i], sizes[i] + 65537);
		if (p == 0) return 144;
		for (size_t j = 0; j < sizes[i]; j++)
			if (p[j] != (unsigned char)(i + j)) return 144;
		p = realloc(p, 16);
		if (p == 0 || p[0] != (unsigned char)i) return 144;
		free(p);
	}
	for (unsigned i = 0; i < 1024; i++) {
		unsigned char *p = chunks[i] = malloc(i % 192 + 1);
		if (p == 0 || ((uintptr_t)p & 15)) return 145;
		memset(p, i & 255, i % 192 + 1);
	}
	for (unsigned i = 0; i < 1024; i++) {
		unsigned char *p = chunks[i];
		for (unsigned j = 0; j < i % 192 + 1; j++)
			if (p[j] != (unsigned char)i) return 145;
		free(p);
	}
	unsigned char *zero = calloc(4097, 7);
	if (!zero) return 146;
	for (size_t i = 0; i < 4097 * 7; i++) if (zero[i]) return 146;
	free(zero);
	if (calloc(SIZE_MAX, 2) != 0 || errno != ENOMEM) return 146;
	void *aligned = (void *)0x1234;
	errno = EBADF;
	if (posix_memalign(&aligned, 3, 100) != EINVAL || aligned != (void *)0x1234 || errno != EBADF ||
	    posix_memalign(&aligned, 65536, 100) != 0 || ((uintptr_t)aligned & 65535))
		return 147;
	memset(aligned, 0x5a, 100);
	int status;
	pid_t pid = fork();
	if (pid == 0) {
		memset(aligned, 0xe7, 100);
		free(aligned);
		void *p = malloc(262144);
		if (!p) _exit(1);
		memset(p, 0x55, 262144);
		free(p);
		_exit(29);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid || status != (29 << 8)) return 148;
	for (size_t i = 0; i < 100; i++) if (((unsigned char *)aligned)[i] != 0x5a) return 148;
	free(aligned);
	free(0);
	void *empty = malloc(0);
	free(empty);
	puts("NetBSD/RVM: libc malloc/calloc/realloc/free, aligned allocations, 64 KiB heap and fork/COW verified");
	return 0;
}
