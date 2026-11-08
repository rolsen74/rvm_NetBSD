/* FPU banks must be isolated across traps, scheduling, fork, exec and signals. */
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/ucontext.h>
#include <machine/fpu.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <setjmp.h>
extern long __rvm_rawcall(long,long,long,long,long,long,long,long,long);
extern char **environ;
static struct fpreg original, a, b, observed;
static volatile sig_atomic_t signal_seen;
static sigjmp_buf jump;
static void fill(struct fpreg *fp, unsigned seed) {
    for (unsigned i = 0; i < RVM_NFPREG; i++)
        fp->f_regs[i] = UINT64_C(0x7ff8000000000000) | ((uint64_t)seed << 16) | i;
    fp->f_regs[0] = 0; fp->f_regs[63] = UINT64_C(0x8000000000000000);
    fp->f_fpcr = seed & RVM_FPCR_MASK;
    fp->f_fpsr = (seed & RVM_FPSR_EXCEPT_MASK) | 0x800;
}
static int same(const struct fpreg *fp) {
    rvm_fpu_save(&observed);
    return !memcmp(fp, &observed, sizeof(observed));
}
static void fp_signal(int sig, siginfo_t *info, void *context) {
    ucontext_t *uc = context;
    (void)info;
    signal_seen = sig == SIGUSR2 && (uc->uc_flags & _UC_FPU) &&
        !memcmp(&uc->uc_mcontext.__fpregs, &a, sizeof(a));
    rvm_fpu_restore(&b);
}
#define CHECK(c) do { if (!(c)) { rvm_fpu_restore(&original); fprintf(stderr,"FPU context failure at line %d errno=%d\n",__LINE__,errno); return 162; } } while(0)
static long trace(int op, pid_t pid, void *data) {
    long r = __rvm_rawcall(SYS_ptrace,op,pid,(long)data,0,0,0,0,0);
    if (r < 0) { errno = -r; return -1; } return r;
}
int fpu_check(void);
int fpu_check(void) {
    rvm_fpu_save(&original); fill(&a, 1); fill(&b, 2);
    rvm_fpu_restore(&a);
    for (unsigned i = 0; i < 32; i++) { CHECK(getpid() > 0); CHECK(same(&a)); }
    struct timespec pause = {0, 20000000};
    fflush(NULL);
    pid_t child = fork();
    if (!child) {
        if (!same(&a)) _exit(1);
        rvm_fpu_restore(&b);
        for (unsigned i = 0; i < 4; i++) {
            if (nanosleep(&pause, NULL) || !same(&b)) _exit(2);
        }
        char *args[] = { "/sbin/fpu-exec-check", NULL };
        execve(args[0], args, environ); _exit(127);
    }
    CHECK(child > 0);
    for (unsigned i = 0; i < 4; i++) { CHECK(!nanosleep(&pause,NULL)); CHECK(same(&a)); }
    int status;
    CHECK(waitpid(child,&status,0) == child && status == 0 && same(&a));
    struct sigaction action = {0}, old;
    action.sa_sigaction = fp_signal; action.sa_flags = SA_SIGINFO;
    CHECK(!sigemptyset(&action.sa_mask) && !sigaction(SIGUSR2,&action,&old));
    CHECK(!raise(SIGUSR2) && signal_seen && same(&a));
    CHECK(!sigaction(SIGUSR2,&old,NULL));
    int jumped = sigsetjmp(jump, 1);
    if (!jumped) {
        rvm_fpu_restore(&b);
        siglongjmp(jump, 17);
    }
    CHECK(jumped == 17 && same(&a));
    child = fork();
    if (!child) {
        if (trace(PT_TRACE_ME,0,NULL) || raise(SIGSTOP)) _exit(3);
        _exit(same(&b) ? 0 : 4);
    }
    CHECK(child > 0 && waitpid(child,&status,0) == child && WIFSTOPPED(status));
    struct fpreg debug;
    CHECK(!trace(PT_GETFPREGS,child,&debug) && !memcmp(&debug,&a,sizeof(a)));
    debug = b; debug.f_fpcr = 4;
    CHECK(trace(PT_SETFPREGS,child,&debug) == -1 && errno == EINVAL);
    CHECK(!trace(PT_SETFPREGS,child,&b));
    CHECK(!trace(PT_CONTINUE,child,(void *)1));
    CHECK(waitpid(child,&status,0) == child && status == 0 && same(&a));
    fflush(NULL);
    child = fork();
    if (!child) {
        char *args[] = { "/sbin/fpu-native-check", NULL };
        execve(args[0], args, environ); _exit(127);
    }
    CHECK(child > 0 && waitpid(child,&status,0) == child && status == 0 && same(&a));
    rvm_fpu_restore(&original);
    puts("NetBSD/RVM: FPU register banks, FPCR/FPSR, scheduling, fork/exec, signals and ptrace verified");
    return 0;
}
