/* Supervisor-only SYSCALL/RTE across every logical IPL and a switched stack. */
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <uvm/uvm.h>
#include <machine/switch.h>
#include <machine/trap.h>
#include <machine/bootsched.h>
const char trap_bad[] = "Supervisor trap regression at line 0x";
const char trap_ok[] = "RVM supervisor trap registers, IPLs and worker stack verified.\n";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(trap_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
static struct lwp worker __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static void
entry(void *arg)
{
	unsigned int n;
	volatile uint64_t canary = 0x535643535441434bUL;
	unsigned char *p = kmem_zalloc(37, KM_NOSLEEP);
	REQUIRE(arg == &worker && p != NULL && p[0] == 0 && p[36] == 0);
	p[0] = 0x71; p[36] = 0x83;
	for (n = 0; n < 16; n++) {
		unsigned long count = rvm_trap_count();
		REQUIRE(rvm_svc_probe() && rvm_trap_count() == count + 1);
		REQUIRE(curcpu()->ci_cpl == IPL_NONE && curcpu()->ci_intr_depth == 0);
		REQUIRE(canary == 0x535643535441434bUL && p[0] == 0x71 && p[36] == 0x83);
		rvm_bootsched_yield();
	}
	kmem_free(p, 37);
	rvm_bootsched_finish();
}
void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	vaddr_t stack;
	unsigned int n;
	int64_t before;
	unsigned long count;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32 && rvm_trap_init());
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	count = rvm_trap_count();
	for (n = 0; n < 64; n++) {
		int ipl = n % _IPL_N;
		splraise(ipl);
		REQUIRE(rvm_svc_probe());
		REQUIRE(curcpu()->ci_cpl == ipl && curcpu()->ci_intr_depth == 0);
		spl0();
	}
	REQUIRE(rvm_bootsched_init());
	stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
	REQUIRE(stack != 0);
	worker.l_addr = (void *)stack; worker.l_cpu = curcpu();
	worker.l_flag = LW_SYSTEM; worker.l_pflag = LP_MPSAFE;
	worker.l_proc = &process; worker.l_stat = LSIDL;
	cpu_lwp_fork(curlwp, &worker, NULL, 0, entry, &worker);
	REQUIRE(rvm_bootsched_ready(&worker));
	while (worker.l_stat != LSZOMB) rvm_bootsched_yield();
	REQUIRE(rvm_bootsched_reap(&worker) && rvm_bootsched_fini());
	REQUIRE(rvm_trap_count() == count + 80);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	rvm_early_puts(trap_ok);
}
