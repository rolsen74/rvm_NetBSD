/* Real timer-driven MI uptime on controller/worker stacks and cached reads. */
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/kmem.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <uvm/uvm.h>
#include <machine/boottime.h>
#include <machine/bootsched.h>
#include <machine/irq.h>
const char tc_bad[] = "MI timecounter regression at line 0x";
const char tc_ok[] = "RVM MI uptime, cached timestamps and worker reads verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(tc_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp child __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static unsigned int reads __attribute__((section(".data"))) = 0;
static void
check_reads(void)
{
	struct bintime live, cached, wall, base;
	struct timespec ns, cns, wns;
	struct timeval us, cus, wus;
	int generation = curlwp->l_tcgen;
	curlwp->l_tcgen = 17; /* Nested readers must preserve the caller's generation. */
	binuptime(&live); getbinuptime(&cached); bintime(&wall); getbinboottime(&base);
	REQUIRE(live.sec == cached.sec && live.frac == cached.frac);
	REQUIRE(base.sec == 0 && base.frac == 0 && bintimecmp(&live, &wall, ==));
	nanouptime(&ns); getnanouptime(&cns); nanotime(&wns); getnanotime(&wns);
	microuptime(&us); getmicrouptime(&cus); microtime(&wus); getmicrotime(&wus);
	REQUIRE(ns.tv_sec == live.sec && ns.tv_sec == cns.tv_sec && ns.tv_nsec == cns.tv_nsec);
	REQUIRE(us.tv_sec == ns.tv_sec && us.tv_usec == ns.tv_nsec / 1000);
	REQUIRE(cus.tv_sec == us.tv_sec && cus.tv_usec == us.tv_usec);
	REQUIRE(wns.tv_sec == ns.tv_sec && wns.tv_nsec == ns.tv_nsec);
	REQUIRE(wus.tv_sec == us.tv_sec && wus.tv_usec == us.tv_usec);
	REQUIRE(time_uptime == live.sec && time_second == live.sec);
	REQUIRE(curlwp->l_tcgen == 17 && tc_getfrequency() == RVM_CLOCK_HZ);
	curlwp->l_tcgen = generation;
	reads++;
}
static void
worker(void *arg)
{
	unsigned int n;
	struct timespec before, after;
	int64_t delta;
	volatile uint64_t canary = 0x54494d4553544143UL;
	unsigned char *p = kmem_zalloc(75, KM_NOSLEEP);
	REQUIRE(arg == &child && p != NULL && p[0] == 0 && p[74] == 0);
	p[0] = 0x51; p[74] = 0x92;
	for (n = 0; n < 8; n++) {
		nanouptime(&before);
		REQUIRE(rvm_clock_wait());
		nanouptime(&after);
		delta = after.tv_nsec - before.tv_nsec;
		if (after.tv_sec == before.tv_sec + 1) delta += 1000000000;
		else REQUIRE(after.tv_sec == before.tv_sec);
		REQUIRE(delta >= 19999999 && delta <= 20000001);
		check_reads();
		REQUIRE(canary == 0x54494d4553544143UL && p[0] == 0x51 && p[74] == 0x92);
		rvm_bootsched_yield();
	}
	kmem_free(p, 75); rvm_bootsched_finish();
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	vaddr_t stack;
	int64_t before;
	struct bintime initial, final;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 28 && rvm_timecounter_init() && rvm_timecounter_init());
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	binuptime(&initial); REQUIRE(initial.sec == 1 && initial.frac == 0);
	REQUIRE(rvm_bootsched_init() && rvm_clock_start());
	stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
	REQUIRE(stack != 0);
	child.l_addr = (void *)stack; child.l_cpu = curcpu();
	child.l_flag = LW_SYSTEM; child.l_pflag = LP_MPSAFE;
	child.l_proc = &process; child.l_stat = LSIDL;
	cpu_lwp_fork(curlwp, &child, NULL, 0, worker, &child);
	REQUIRE(rvm_bootsched_ready(&child));
	while (child.l_stat != LSZOMB) { rvm_bootsched_yield(); check_reads(); }
	binuptime(&final);
	REQUIRE(reads >= 16 && bintimecmp(&final, &initial, >));
	REQUIRE(child.l_rticksum >= 8 && curlwp->l_tcgen == 0 && child.l_tcgen == 0);
	REQUIRE(rvm_clock_stop() && rvm_bootsched_reap(&child) && rvm_bootsched_fini());
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before && kernel_map->nentries == 0);
	rvm_early_puts(tc_ok);
}
