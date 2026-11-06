/* Deterministic MI hardclock driving: cross wheel buckets without host sleeps. */
#include <sys/kernel.h>
#include <sys/callout.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <machine/bootclock.h>
const char wheel_bad[] = "MI callout wheel regression at line 0x";
const char wheel_ok[] = "RVM MI callout wheel boundaries and rescheduling verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(wheel_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static callout_t calls[5] __attribute__((section(".data"))) = { 0 };
static int expected[5] __attribute__((section(".data"))) = { 150, 800, 256, 257, 512 };
static unsigned int seen[5] __attribute__((section(".data"))) = { 0 };
static int initial __attribute__((section(".data"))) = 0;
static void
callback(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	REQUIRE(id < 5 && !cpu_intr_p() && curcpu()->ci_cpl == IPL_NONE);
	REQUIRE(getticks() == initial + expected[id] && seen[id]++ == 0);
	callout_ack(&calls[id]);
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int i, n;
	struct clockframe cf = { 0, 0, 1 };
	struct bintime bt;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 27 && rvm_bootclock_init());
	initial = getticks();
	for (i = 0; i < 5; i++) {
		callout_init(&calls[i], CALLOUT_MPSAFE);
		callout_reset(&calls[i], i == 0 ? 400 : i == 1 ? 500 : expected[i], callback, (void *)(uintptr_t)i);
	}
	for (n = 1; n <= 1000; n++) {
		/* Test-only context emulation; real IRQ delivery has separate cases. */
		curcpu()->ci_intr_depth = 1; splhigh(); hardclock(&cf);
		curcpu()->ci_intr_depth = 0; spl0(); rvm_bootclock_poll();
		if (n == 100) {
			callout_schedule(&calls[0], 50);
			callout_schedule(&calls[1], 700);
		}
	}
	REQUIRE(getticks() == initial + 1000);
	rvm_bootclock_binuptime(&bt); REQUIRE(bt.sec == 20 && bt.frac == 0);
	for (i = 0; i < 5; i++) {
		REQUIRE(seen[i] == 1 && !callout_pending(&calls[i]) && callout_expired(&calls[i]));
		callout_stop(&calls[i]); callout_destroy(&calls[i]);
	}
	rvm_early_puts(wheel_ok);
}
