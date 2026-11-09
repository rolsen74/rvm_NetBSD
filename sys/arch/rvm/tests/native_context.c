/* Context entry/return with register and stack arguments, masks and FP state. */
#include <sys/types.h>
#include <ieeefp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

static ucontext_t caller, fiber;
static volatile int phase, failed;

static void
entry(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
    uint64_t f, uint64_t g, uint64_t h, uint64_t i, uint64_t j)
{
    sigset_t mask;
    if (a != UINT64_C(0x1234567800000001) || b != 2 || c != 3 || d != 4 ||
        e != 5 || f != 6 || g != 7 || h != 8 ||
        i != UINT64_C(0xabcdef1200000009) || j != 10 || fpgetround() != FP_RM)
        failed = 1;
    if (sigprocmask(SIG_SETMASK, NULL, &mask) || !sigismember(&mask, SIGUSR2))
        failed = 1;
    phase = 1;
    fpsetround(FP_RP);
    if (swapcontext(&fiber, &caller) != 0 || fpgetround() != FP_RP)
        failed = 1;
    phase = 2;
    /* Returning enters _resumecontext and follows fiber.uc_link. */
}

int
main(void)
{
    sigset_t mask, original, observed;
    fp_rnd rounding = fpgetround();
    void *stack = malloc(131072);
    if (stack == NULL || sigemptyset(&mask) || sigaddset(&mask, SIGUSR2) ||
        sigprocmask(SIG_BLOCK, &mask, &original) || getcontext(&fiber))
        return 1;
    fiber.uc_stack.ss_sp = stack;
    fiber.uc_stack.ss_size = 131072;
    fiber.uc_stack.ss_flags = 0;
    fiber.uc_link = &caller;
    fiber.uc_mcontext.__fpregs.f_fpcr = FP_RM;
    makecontext(&fiber, (void (*)(void))entry, 10,
        UINT64_C(0x1234567800000001), UINT64_C(2), UINT64_C(3), UINT64_C(4),
        UINT64_C(5), UINT64_C(6), UINT64_C(7), UINT64_C(8),
        UINT64_C(0xabcdef1200000009), UINT64_C(10));
    if (sigprocmask(SIG_SETMASK, &original, NULL))
        return 2;
    fpsetround(FP_RZ);
    if (swapcontext(&caller, &fiber) || phase != 1 || fpgetround() != FP_RZ ||
        sigprocmask(SIG_SETMASK, NULL, &observed) ||
        sigismember(&observed, SIGUSR2) != sigismember(&original, SIGUSR2))
        return 3;
    fpsetround(FP_RM);
    if (swapcontext(&caller, &fiber) || phase != 2 || failed || fpgetround() != FP_RM)
        return 4;
    fpsetround(rounding);
    free(stack);
    puts("RVM normal libc: makecontext/swapcontext arguments, return links, signal masks and FPU state verified");
    return 0;
}
