/* Hardware-driven MI callouts, cancellation, rearm and callback lifetime. */
#include <sys/kernel.h>
#include <sys/callout.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <uvm/uvm.h>
#include <machine/bootclock.h>
#include <machine/bootsched.h>
#include <machine/irq.h>
const char hc_bad[] = "MI hardclock regression at line 0x";
const char hc_ok[] = "RVM MI hardclock, callout lifetime and callback deadlines verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(hc_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static callout_t calls[5] __attribute__((section(".data"))) = { 0 };
static struct lwp *parent __attribute__((section(".data"))) = NULL;
static int initial __attribute__((section(".data"))) = 0;
static unsigned int counts[5] __attribute__((section(".data"))) = { 0 };
static void
callback(void *arg)
{
	unsigned int id = (uintptr_t)arg;
	REQUIRE(id < 5 && rvm_bootclock_busy() && !cpu_intr_p() && curcpu()->ci_cpl == IPL_NONE);
	REQUIRE(CURCPU_IDLE_P() && callout_invoking(&calls[id]) && !callout_pending(&calls[id]));
	REQUIRE(!rvm_clock_wait()); /* Callbacks may wake work, but cannot block. */
	counts[id]++;
	if (id == 0) {
		REQUIRE(getticks() == initial + 2 * (int)counts[id]);
		if (counts[id] < 3) callout_schedule(&calls[id], 2);
		else callout_destroy(&calls[id]); /* MI does not touch it after callback. */
	} else {
		REQUIRE(getticks() == initial + (id == 1 ? 3 : id == 2 ? 4 : 8));
		callout_ack(&calls[id]);
		if (id == 4) REQUIRE(rvm_bootsched_ready(parent));
	}
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned int i;
	int64_t before;
	uint64_t idle_before;
	struct bintime bt;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 27 && rvm_bootclock_init() && rvm_bootclock_init());
	REQUIRE(hz == RVM_CLOCK_HZ);
	parent = curlwp;
	initial = getticks(); before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	idle_before = curcpu()->ci_schedstate.spc_cp_time[CP_IDLE];
	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(NULL, NULL) && rvm_clock_start());
	for (i = 0; i < 5; i++) callout_init(&calls[i], CALLOUT_MPSAFE);
	callout_reset(&calls[0], 2, callback, (void *)0);
	callout_reset(&calls[1], 1, callback, (void *)1);
	callout_reset(&calls[1], 3, callback, (void *)1); /* Move later. */
	callout_setfunc(&calls[2], callback, (void *)2);
	callout_schedule(&calls[2], 9); callout_schedule(&calls[2], 4); /* Earlier. */
	callout_reset(&calls[3], 5, callback, (void *)3);
	REQUIRE(!callout_stop(&calls[3]) && !callout_active(&calls[3]));
	callout_reset(&calls[4], 8, callback, (void *)4);
	rvm_bootsched_sleep();
	REQUIRE(getticks() == initial + 8 && counts[0] == 3 && counts[1] == 1 && counts[2] == 1);
	REQUIRE(counts[3] == 0 && counts[4] == 1 && !rvm_bootclock_busy());
	REQUIRE(curcpu()->ci_schedstate.spc_cp_time[CP_IDLE] == idle_before + 8);
	rvm_bootclock_binuptime(&bt);
	REQUIRE(bt.sec == 0 && bt.frac == 8 * (UINT64_MAX / RVM_CLOCK_HZ + 1));
	for (i = 1; i < 5; i++) {
		REQUIRE(!callout_pending(&calls[i]));
		REQUIRE(callout_stop(&calls[i]) == (i != 3));
		callout_destroy(&calls[i]);
	}
	/* The self-destroyed address can be initialized and destroyed again. */
	callout_init(&calls[0], CALLOUT_MPSAFE); callout_destroy(&calls[0]);
	REQUIRE(rvm_clock_stop() && rvm_bootsched_fini());
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before && kernel_map->nentries == 0);
	rvm_early_puts(hc_ok);
}
