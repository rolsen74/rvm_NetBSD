/* Repeated real IRQs, idle deadlines, sleeping worker and restart fencing. */
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/switch.h>
#include <machine/bootsched.h>
#include <machine/irq.h>
const char clock_ok[] = "RVM repeated timer IRQs, idle deadlines and restart verified.\n";
const char clock_bad[] = "Repeated timer regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(clock_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp child __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static struct lwp *parent __attribute__((section(".data"))) = NULL;
static uint64_t parent_deadline __attribute__((section(".data"))) = 0;
static uint64_t child_deadline __attribute__((section(".data"))) = 0;
static unsigned int progress __attribute__((section(".data"))) = 0;
static void
poll_deadlines(void *arg)
{
	REQUIRE(arg == parent && CURCPU_IDLE_P());
	REQUIRE(curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_intr_depth == 0);
	REQUIRE(rvm_boot_get_cr() == RVM_CR_MMU && rvm_irq_get_inte() == 0);
	if (child.l_stat == LSSLEEP && rvm_clock_ticks() >= child_deadline)
		REQUIRE(rvm_bootsched_ready(&child));
	if (parent->l_stat == LSSLEEP && rvm_clock_ticks() >= parent_deadline)
		REQUIRE(rvm_bootsched_ready(parent));
}
static void
worker(void *arg)
{
	unsigned int n;
	unsigned char *p = kmem_zalloc(73, KM_NOSLEEP);
	volatile uint64_t canary = 0x434c4f434b535441UL;
	REQUIRE(arg == &child && p != NULL && p[0] == 0 && p[72] == 0);
	p[0] = 0x42; p[72] = 0xa7;
	for (n = 0; n < 8; n++) {
		child_deadline = rvm_clock_ticks() + 1;
		rvm_bootsched_sleep();
		REQUIRE(rvm_clock_ticks() == child_deadline && curlwp == &child);
		REQUIRE(curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_intr_depth == 0);
		REQUIRE(canary == 0x434c4f434b535441UL && p[0] == 0x42 && p[72] == 0xa7);
		progress++;
	}
	kmem_free(p, 73); rvm_bootsched_finish();
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	vaddr_t stack;
	unsigned int n, round;
	uint64_t initial, lines[2], tick;
	int64_t before;
	long resident, wired;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 26);
	parent = curlwp;
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel()); wired = pmap_wired_count(pmap_kernel());
	initial = rvm_clock_ticks(); lines[0] = rvm_clock_events(0); lines[1] = rvm_clock_events(1);
	REQUIRE(!rvm_clock_wait() && !rvm_clock_stop());
	REQUIRE(rvm_bootsched_init() && rvm_bootsched_idle_init(poll_deadlines, parent));
	REQUIRE(rvm_clock_start() && !rvm_clock_start() && !rvm_bootsched_fini());
	stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
	REQUIRE(stack != 0);
	child.l_addr = (void *)stack; child.l_cpu = curcpu();
	child.l_flag = LW_SYSTEM; child.l_pflag = LP_MPSAFE;
	child.l_proc = &process; child.l_stat = LSIDL;
	cpu_lwp_fork(parent, &child, NULL, 0, worker, &child);
	REQUIRE(rvm_bootsched_ready(&child));
	parent_deadline = initial + 20;
	rvm_bootsched_sleep();
	REQUIRE(progress == 8 && child.l_stat == LSZOMB && rvm_clock_ticks() == parent_deadline);
	REQUIRE(rvm_clock_stop() && !rvm_clock_stop() && !rvm_clock_wait());
	REQUIRE(rvm_bootsched_reap(&child) && rvm_bootsched_fini());
	/* Odd-length sessions preserve the next channel across stop/start. */
	for (round = 0; round < 2; round++) {
		REQUIRE(rvm_clock_start());
		for (n = 0; n < 3; n++) {
			tick = rvm_clock_ticks();
			REQUIRE(rvm_clock_wait() && rvm_clock_ticks() == tick + 1);
		}
		REQUIRE(rvm_clock_stop());
	}
	REQUIRE(rvm_clock_ticks() == initial + 26);
	REQUIRE(rvm_clock_events(0) == lines[0] + 13 && rvm_clock_events(1) == lines[1] + 13);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident && pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(kernel_map->nentries == 0 && rvm_boot_get_cr() == RVM_CR_MMU && rvm_irq_get_inte() == 0);
	rvm_early_puts(clock_ok);
}
