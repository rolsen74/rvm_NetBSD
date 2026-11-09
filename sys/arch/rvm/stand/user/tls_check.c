/* Compiler-generated emulated TLS in the static single-LWP runtime. */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <lwp.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

__thread int tls_number = 7;
static __thread uint64_t private_word = UINT64_C(0x0fedcba987654321);
static _Thread_local unsigned char zeroes[65553];
static __thread unsigned char aligned[37] __attribute__((aligned(65536))) = { 0x42, 0x19 };
static __thread struct { int n; char text[12]; } aggregate = { 41, "tls-data" };
static __thread int lazy_number = 23;
static __thread const char *message = "tls-pointer";
static volatile sig_atomic_t caught;
static int constructor_ok;
static int run_kind, exit_seen;
static int *saved_address;
static jmp_buf jump;
extern char **environ;
extern int *tls_other_number(void);
extern uint64_t *tls_other_word(void);

static int *local_number(void) {
    static _Thread_local int value = 11;
    return &value;
}

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "TLS failure at line %d errno=%d\n", __LINE__, errno); return 167; } } while (0)

static void check_at_exit(void) {
    if ((run_kind == 1 && (tls_number != 19 || lazy_number != 23)) ||
        (run_kind == 2 && (tls_number != 37 || lazy_number != 47 || &tls_number != saved_address)))
        _exit(167);
    exit_seen = 1;
}

__attribute__((constructor(200))) static void initialize_tls(void) {
    constructor_ok = tls_number == 7 && aggregate.n == 41 &&
        !strcmp(aggregate.text, "tls-data") && !strcmp(message, "tls-pointer");
    tls_number = 19;
    saved_address = &tls_number;
    if (atexit(check_at_exit)) constructor_ok = 0;
}

__attribute__((destructor(200))) static void finalize_tls(void) {
    if (run_kind == 1 && !exit_seen) _exit(167);
    if (run_kind == 2) {
        if (!exit_seen || tls_number != 37 || lazy_number != 47 || &tls_number != saved_address)
            _exit(167);
        puts("NetBSD/RVM: emulated TLS initialization, alignment, constructors, signals, fork/COW and exec verified");
    }
}

static void handler(int sig) {
    if (sig == SIGUSR1 && &tls_number == saved_address && tls_number == 29) {
        tls_number = 31;
        caught = 1;
    }
}

static int child_exec(void) {
    if (!constructor_ok || tls_number != 19 || lazy_number != 23 ||
        _lwp_getprivate() != NULL) return 167;
    puts("NetBSD/RVM: exec reset of TLS descriptors and LWP private pointer verified");
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--exec")) { run_kind = 1; return child_exec(); }
    CHECK(constructor_ok && tls_number == 19 && saved_address == &tls_number);
    int *local = local_number();
    CHECK(*local == 11 && local_number() == local);
    *local = 13;
    CHECK(*local_number() == 13);
    CHECK(tls_other_number() == &tls_number &&
        private_word == UINT64_C(0x0fedcba987654321) &&
        *tls_other_word() == UINT64_C(0x123456789abcdef0) &&
        tls_other_word() != &private_word);
    CHECK(((uintptr_t)aligned & 65535) == 0 && aligned[0] == 0x42 && aligned[1] == 0x19);
    for (size_t i = 2; i < sizeof(aligned); i++) CHECK(aligned[i] == 0);
    for (size_t i = 0; i < sizeof(zeroes); i++) CHECK(zeroes[i] == 0);
    zeroes[0] = 0x12; zeroes[65536] = 0x34; zeroes[65552] = 0x56;
    tls_number = 29;
    struct sigaction action, old;
    memset(&action, 0, sizeof(action)); action.sa_handler = handler;
    CHECK(sigemptyset(&action.sa_mask) == 0 && sigaction(SIGUSR1, &action, &old) == 0);
    CHECK(raise(SIGUSR1) == 0 && caught && tls_number == 31);
    CHECK(sigaction(SIGUSR1, &old, NULL) == 0);
    volatile int returned = 0;
    if (!setjmp(jump)) { tls_number = 37; returned = 1; longjmp(jump, 9); }
    CHECK(returned == 1 && tls_number == 37 && saved_address == &tls_number);
    void *original = _lwp_getprivate();
    void *cookie = (void *)(uintptr_t)UINT64_C(0xfedcba9876543211);
    errno = EDOM;
    _lwp_setprivate(cookie);
    CHECK(_lwp_getprivate() == cookie && errno == EDOM);
    fflush(NULL);
    pid_t pid = fork(); int status;
    if (!pid) {
        if (tls_number != 37 || saved_address != &tls_number || _lwp_getprivate() != cookie ||
            zeroes[0] != 0x12 || zeroes[65536] != 0x34 || zeroes[65552] != 0x56 || lazy_number != 23)
            _exit(1);
        tls_number = 101; zeroes[65536] = 0x78; *tls_other_word() = 5;
        _lwp_setprivate(NULL);
        _exit(tls_number == 101 && _lwp_getprivate() == NULL ? 0 : 2);
    }
    CHECK(pid > 0 && waitpid(pid, &status, 0) == pid && status == 0);
    CHECK(tls_number == 37 && zeroes[65536] == 0x34 &&
        *tls_other_word() == UINT64_C(0x123456789abcdef0) && _lwp_getprivate() == cookie);
    CHECK(lazy_number == 23);
    lazy_number = 47;
    struct timespec pause = { 0, 20000000 };
    CHECK(nanosleep(&pause, NULL) == 0 && tls_number == 37 && saved_address == &tls_number &&
        _lwp_getprivate() == cookie);
    fflush(NULL);
    pid = fork();
    if (!pid) {
        char *args[] = { argv[0], "--exec", NULL };
        execve(args[0], args, environ);
        _exit(127);
    }
    CHECK(pid > 0 && waitpid(pid, &status, 0) == pid && status == 0);
    _lwp_setprivate(original);
    run_kind = 2;
    return 0;
}
