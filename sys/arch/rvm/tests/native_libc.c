/* Exercise the normal BSD-make libc/csu artifacts in a real user process. */
#include <sys/atomic.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <errno.h>
#include <float.h>
#include <ieeefp.h>
#include <nlist.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ucontext.h>
#include <unistd.h>
#include <wchar.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "native libc failure at line %d errno=%d\n", __LINE__, errno); \
    return 1; } } while (0)
static _Thread_local int tls = 41;
static volatile sig_atomic_t caught, resumed;
long rvm_syscall_dirty_number(void), rvm_syscall_wide_number(void);
static void handler(int sig) { caught = sig; tls++; }
static void expected_fault(int sig) { _exit(sig); }
static void
fault(int sig, siginfo_t *info, void *raw)
{
    const ucontext_t *uc = raw;
    char message[] = "native libc fault pc=0000000000000000 addr=0000000000000000\n";
    const char hex[] = "0123456789abcdef";
    uint64_t pc = _UC_MACHINE_PC(uc), address = (uintptr_t)info->si_addr;
    for (int i = 0; i < 16; i++) {
        message[21+i] = hex[(pc >> (60-4*i)) & 15];
        message[43+i] = hex[(address >> (60-4*i)) & 15];
    }
    (void)write(STDERR_FILENO, message, sizeof(message)-1);
    _exit(128+sig);
}


int
main(void)
{
    struct sigaction diagnostic;
    memset(&diagnostic, 0, sizeof(diagnostic));
    diagnostic.sa_sigaction = fault;
    diagnostic.sa_flags = SA_SIGINFO;
    sigemptyset(&diagnostic.sa_mask);
    CHECK(sigaction(SIGSEGV, &diagnostic, NULL) == 0);
    CHECK(sigaction(SIGSYS, &diagnostic, NULL) == 0);
    CHECK(sigaction(SIGILL, &diagnostic, NULL) == 0);
    CHECK(sigaction(SIGBUS, &diagnostic, NULL) == 0);
    puts("native libc: entered main");
    CHECK(getprogname() != NULL && *getprogname() != '\0');
    CHECK(getenv("PATH") != NULL);
    CHECK(strcasecmp(getprogname(), "native-libc-check") == 0);
    CHECK(strncasecmp(getenv("PATH"), "/BIN", 4) == 0);
    errno = 0;
    CHECK(close(-1) == -1 && errno == EBADF);
    CHECK(syscall(SYS_getpid) == getpid());
    CHECK(__syscall(SYS_getpid) == getpid());
    CHECK(rvm_syscall_dirty_number() == getpid());
    struct sigaction ignored;
    memset(&ignored, 0, sizeof(ignored));
    ignored.sa_handler = SIG_IGN;
    sigemptyset(&ignored.sa_mask);
    CHECK(sigaction(SIGSYS, &ignored, NULL) == 0);
    errno = 0;
    CHECK(rvm_syscall_wide_number() == -1 && errno == ENOSYS);
    CHECK(sigaction(SIGSYS, &diagnostic, NULL) == 0);
    puts("native libc: indirect syscalls verified");
    struct nlist symbols[2];
    memset(symbols, 0, sizeof(symbols));
    symbols[0].n_name = "_start";
    errno = 0;
    CHECK(nlist("/bin/native-libc-check", symbols) == -1 && errno == ENOEXEC);
    char *p = malloc(8192);
    CHECK(p != NULL);
    memset(p, 0xa5, 8192);
    p = realloc(p, 32768);
    CHECK(p != NULL && (unsigned char)p[8191] == 0xa5);
    free(p);
    void *oldbrk = sbrk(0);
    CHECK(oldbrk != (void *)-1);
    CHECK(sbrk(16) == oldbrk && sbrk(-16) == (char *)oldbrk + 16);
    CHECK(sbrk(0) == oldbrk);

    puts("native libc: heap verified");
    char text[64], *end;
    CHECK(snprintf(text, sizeof(text), "%.3f/%g/%a", 1.25, -0.5, 2.0) > 0);
    CHECK(strcmp(text, "1.250/-0.5/0x1p+1") == 0);
    CHECK(strtod("-1.25tail", &end) == -1.25 && strcmp(end, "tail") == 0);
    CHECK(wcstoul(L"123!", NULL, 10) == 123);
    wchar_t wide[32];
    CHECK(swprintf(wide, 32, L"%.2f", 1.25) == 4 && wcscmp(wide, L"1.25") == 0);
    double scanned;
    CHECK(swscanf(L"-1.25", L"%lf", &scanned) == 1 && scanned == -1.25);
    fp_rnd rounding = fpgetround();
    CHECK(fpsetround(FP_RZ) == rounding && FLT_ROUNDS == 0);
    CHECK(fpsetround(FP_RM) == FP_RZ && FLT_ROUNDS == 3);
    CHECK(fpsetround(FP_RP) == FP_RM && FLT_ROUNDS == 2);
    CHECK(fpsetround(rounding) == FP_RP);

    puts("native libc: floating and wide I/O verified");
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    CHECK(sigemptyset(&action.sa_mask) == 0);
    CHECK(sigaction(SIGUSR1, &action, NULL) == 0 && raise(SIGUSR1) == 0);
    CHECK(caught == SIGUSR1 && tls == 42);
    sigjmp_buf jump;
    int result = sigsetjmp(jump, 1);
    if (result == 0)
        siglongjmp(jump, 37);
    CHECK(result == 37);
    ucontext_t context;
    CHECK(getcontext(&context) == 0);
    if (!resumed) {
        resumed = 1;
        CHECK(setcontext(&context) == 0);
        return 1;
    }

    puts("native libc: signal and context return verified");
    volatile uint8_t byte = 5;
    volatile uint16_t half = 7;
    volatile uint32_t word = 11;
    volatile uint64_t quad = UINT64_C(0x123456789abcdef0);
    CHECK(atomic_cas_8(&byte, 5, 9) == 5 && byte == 9);
    CHECK(atomic_cas_16(&half, 6, 9) == 7 && half == 7);
    CHECK(atomic_cas_16(&half, 7, 9) == 7 && half == 9);
    CHECK(atomic_cas_32(&word, 11, 13) == 11 && word == 13);
    CHECK(atomic_cas_64(&quad, UINT64_C(0x123456789abcdef0), 17) ==
        UINT64_C(0x123456789abcdef0) && quad == 17);
    CHECK(atomic_add_64_nv(&quad, -3) == 14);
    CHECK(atomic_swap_32(&word, 19) == 13 && word == 19);
    CHECK(atomic_inc_64_nv(&quad) == 15 && atomic_dec_64_nv(&quad) == 14);
    uint32_t expected = 19;
    CHECK(__atomic_compare_exchange_n(&word, &expected, 23, 0,
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) && word == 23);

    uint64_t expected64 = 14;
    CHECK(__atomic_compare_exchange_n(&quad, &expected64, 29, 0,
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) && quad == 29);
    expected64 = 14;
    CHECK(!__atomic_compare_exchange_n(&quad, &expected64, 31, 0,
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) && expected64 == 29 && quad == 29);
    puts("native libc: scalar atomics verified");
    int fds[2], status;
    /* uint64_t has four-byte ABI alignment and may straddle two pages.
     * A failed second-page store must leave the first fragment untouched;
     * COW faults must allow the RAS to retry the whole compare/exchange. */
    size_t pagesize = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char *boundary = mmap(NULL, 2 * pagesize, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANON, -1, 0);
    CHECK(boundary != MAP_FAILED);
    volatile uint64_t *cross = (volatile uint64_t *)(boundary + pagesize - 4);
    const uint64_t original = UINT64_C(0x123456789abcdef0);
    const uint64_t replacement = UINT64_C(0xfedcba9876543210);
    *cross = original;
    pid_t probe = fork();
    CHECK(probe >= 0);
    if (probe == 0) {
        if (signal(SIGSEGV, expected_fault) == SIG_ERR ||
            mprotect(boundary + pagesize, pagesize, PROT_READ) != 0)
            _exit(1);
        (void)atomic_cas_64(cross, original, replacement);
        _exit(2);
    }
    CHECK(waitpid(probe, &status, 0) == probe);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == SIGSEGV);
    CHECK(*cross == original);
    CHECK(munmap(boundary, 2 * pagesize) == 0);
    boundary = mmap(NULL, 2 * pagesize, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(boundary != MAP_FAILED);
    cross = (volatile uint64_t *)(boundary + pagesize - 4);
    *cross = original;
    probe = fork();
    CHECK(probe >= 0);
    if (probe == 0)
        _exit(atomic_cas_64(cross, original, replacement) == original &&
            *cross == replacement ? 0 : 3);
    CHECK(waitpid(probe, &status, 0) == probe && status == 0);
    CHECK(*cross == original && munmap(boundary, 2 * pagesize) == 0);
    puts("native libc: cross-page atomic protection and COW verified");
    CHECK(pipe(fds) == 0);
    CHECK(write(fds[1], "pipe", 4) == 4 && read(fds[0], text, 4) == 4);
    CHECK(memcmp(text, "pipe", 4) == 0);
    CHECK(close(fds[0]) == 0 && close(fds[1]) == 0);
    puts("native libc: pipe verified");
    volatile uint64_t *shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANON, -1, 0);
    CHECK(shared != MAP_FAILED);
    *shared = 0;
    pid_t child = fork();
    CHECK(child >= 0);
    for (unsigned i = 0; i < 100000; i++)
        atomic_inc_64(shared);
    if (child == 0)
        _exit(tls == 42 ? 0 : 1);
    CHECK(waitpid(child, &status, 0) == child && status == 0);
    CHECK(*shared == 200000 && munmap((void *)shared, 4096) == 0);
    child = vfork();
    if (child == 0)
        _exit(42);
    CHECK(child > 0 && waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 42);
    unsigned char random[32];
    arc4random_buf(random, sizeof(random));
    CHECK(arc4random_uniform(3) < 3);
    puts("RVM normal libc: startup, stdio, errno, heap, FP control, contexts, TLS and shared RAS atomics verified");
    return 0;
}
