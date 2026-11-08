/* Nonlocal jump and user-supplied signal trampoline integration. */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
static sigjmp_buf saved;
static volatile sig_atomic_t caught;
static volatile long shared;
static char *const exec_args[] = { "/bin/echo", "vfork exec verified", NULL };
extern char **environ;
static void handler(int sig) { caught = sig; }
extern int rvm_jump_register_check(jmp_buf);
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "context failure at line %d errno=%d\n", __LINE__, errno); return 160; } } while (0)
int context_check(void);
int context_check(void) {
    jmp_buf registers;
    CHECK(rvm_jump_register_check(registers) == 1);
    sigset_t original, mask, current;
    CHECK(sigemptyset(&mask) == 0 && sigaddset(&mask, SIGUSR1) == 0);
    CHECK(sigprocmask(SIG_BLOCK, &mask, &original) == 0);
    volatile int visits = 0;
    int value = sigsetjmp(saved, 1);
    if (!value) {
        visits++;
        CHECK(sigprocmask(SIG_UNBLOCK, &mask, NULL) == 0);
        siglongjmp(saved, 37);
    }
    CHECK(value == 37 && visits == 1);
    CHECK(sigprocmask(SIG_SETMASK, NULL, &current) == 0 && sigismember(&current, SIGUSR1) == 1);
    value = sigsetjmp(saved, 0);
    if (!value) {
        CHECK(sigprocmask(SIG_UNBLOCK, &mask, NULL) == 0);
        siglongjmp(saved, 0);
    }
    CHECK(value == 1 && sigprocmask(SIG_SETMASK, NULL, &current) == 0 && sigismember(&current, SIGUSR1) == 0);
    struct sigaction action, old;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    CHECK(sigemptyset(&action.sa_mask) == 0 && sigaction(SIGUSR1, &action, &old) == 0);
    CHECK(raise(SIGUSR1) == 0 && caught == SIGUSR1);
    CHECK(sigaction(SIGUSR1, &old, NULL) == 0 && sigprocmask(SIG_SETMASK, &original, NULL) == 0);
    fflush(NULL);
    pid_t child = vfork();
    if (child == 0) {
        shared = 123;
        execve(exec_args[0], exec_args, environ);
        _exit(127);
    }
    int status;
    CHECK(child > 0 && shared == 123 && waitpid(child, &status, 0) == child && status == 0);
    struct timespec invalid = { 0, 1000000000 };
    errno = EDOM;
    CHECK(clock_nanosleep(CLOCK_MONOTONIC, 0, &invalid, NULL) == EINVAL && errno == EDOM);
    puts("NetBSD/RVM: setjmp register/stack restoration, signal masks and user signal trampoline verified");
    return 0;
}
