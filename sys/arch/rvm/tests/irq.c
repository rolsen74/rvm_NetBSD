/* A real hardware timer IRQ on a separately scheduled LWP stack. */
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/switch.h>
#include <machine/bootsched.h>
#include <machine/irq.h>
const char irq_ok[] = "RVM timer IRQ worker stack, registers and cleanup verified.\n";
const char irq_bad[] = "Timer IRQ regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(irq_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp worker __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static unsigned int completed __attribute__((section(".data"))) = 0;
static void
entry(void *arg)
{
	volatile uint64_t canary[8];
	unsigned char *p = kmem_zalloc(93, KM_NOSLEEP);
	unsigned int i;
	REQUIRE(arg == &worker && p != NULL && p[0] == 0 && p[92] == 0);
	for (i = 0; i < 8; i++) canary[i] = 0x495251535441434bUL + i;
	p[0] = 0x79; p[92] = 0xa3;
	REQUIRE(rvm_timer_irq_verify());
	REQUIRE(curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_intr_depth == 0);
	REQUIRE(rvm_boot_get_cr() == RVM_CR_MMU && rvm_irq_get_inte() == 0);
	REQUIRE(rvm_timer_irq_halted() + rvm_timer_irq_early() == 1);
#ifdef RVM_TEST_IRQ_EARLY
	REQUIRE(rvm_timer_irq_early() == 1 && rvm_timer_irq_halted() == 0);
#endif
	REQUIRE(rvm_timer_irq_verify()); /* Completed one-shot is idempotent. */
	REQUIRE(rvm_timer_irq_halted() + rvm_timer_irq_early() == 1);
	for (i = 0; i < 8; i++) REQUIRE(canary[i] == 0x495251535441434bUL + i);
	REQUIRE(p[0] == 0x79 && p[92] == 0xa3);
	kmem_free(p, 93); completed = 1;
	rvm_bootsched_finish();
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	vaddr_t stack;
	int64_t before;
	long resident, wired;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 25); /* Hardware probe was deferred to the worker. */
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel()); wired = pmap_wired_count(pmap_kernel());
	REQUIRE(rvm_bootsched_init());
	stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
	REQUIRE(stack != 0);
	worker.l_addr = (void *)stack; worker.l_cpu = curcpu();
	worker.l_flag = LW_SYSTEM; worker.l_pflag = LP_MPSAFE;
	worker.l_proc = &process; worker.l_stat = LSIDL;
	cpu_lwp_fork(curlwp, &worker, NULL, 0, entry, &worker);
	REQUIRE(rvm_bootsched_ready(&worker));
	while (worker.l_stat != LSZOMB) rvm_bootsched_yield();
	REQUIRE(completed && rvm_bootsched_reap(&worker) && rvm_bootsched_fini());
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident && pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(kernel_map->nentries == 0 && rvm_boot_get_cr() == RVM_CR_MMU && rvm_irq_get_inte() == 0);
	rvm_early_puts(irq_ok);
}
