/* Real LWP stacks, ABI preserved registers, allocations and prev-LWP returns. */
#include <sys/kmem.h>
#include <sys/lwp.h>
#include <sys/cpu.h>
#include <uvm/uvm.h>
#include <machine/pcb.h>
#include <machine/switch.h>
#include <machine/bootheap.h>
const char context_ok[] = "RVM LWP contexts, registers and stack ownership verified.\n";
const char context_bad[] = "Kernel context regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(context_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)
extern int rvm_switch_register_test(struct lwp *, struct lwp *, uint64_t);
static struct lwp child __attribute__((section(".data"))) = { 0 };
static struct lwp *main_lwp __attribute__((section(".data"))) = NULL;
static volatile unsigned int step __attribute__((section(".data"))) = 0;

static void
context_worker(void *arg)
{
	volatile uint64_t local[16];
	struct lwp *self = curlwp;
	unsigned char *memory;
	unsigned int i, n;
	REQUIRE(arg == &child && self == &child && curcpu()->ci_cpl == IPL_SCHED);
	spl0();
	memory = kmem_zalloc(37, KM_NOSLEEP);
	REQUIRE(memory != NULL && memory[0] == 0 && memory[36] == 0);
	memory[0] = 0x79; memory[36] = 0xa1;
	for (i = 0; i < 16; i++) local[i] = 0x1234567800000000UL + i;
	for (n = 0; n < 64; n++) {
		for (i = 0; i < 16; i++) REQUIRE(local[i] == 0x1234567800000000UL + i);
		REQUIRE(curlwp == self && curcpu()->ci_onproc == self);
		REQUIRE(memory[0] == 0x79 && memory[36] == 0xa1);
		kpreempt_disable(); kpreempt_enable();
		step = n + 1;
		if (n == 63) kmem_free(memory, 37);
		splsched();
		curcpu()->ci_onproc = main_lwp; /* MI scheduler selects onproc before MD switch. */
		REQUIRE(rvm_switch_register_test(self, main_lwp, 0x20000));
		spl0();
	}
	REQUIRE(0); /* The controller discards this suspended context after its last yield. */
}

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	vaddr_t stack;
	int64_t before;
	long resident, wired;
	unsigned int n;
	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 20);
	main_lwp = curlwp;
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	resident = pmap_resident_count(pmap_kernel()); wired = pmap_wired_count(pmap_kernel());
	stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
	REQUIRE(stack != 0);
	child.l_cpu = curcpu(); child.l_flag = LW_SYSTEM; child.l_stat = LSONPROC;
	REQUIRE(!rvm_cpu_lwp_prepare(&child, stack + 1, USPACE, context_worker, &child));
	REQUIRE(rvm_cpu_lwp_prepare(&child, stack, USPACE, context_worker, &child));
	REQUIRE(!rvm_cpu_lwp_prepare(&child, stack, USPACE, context_worker, &child));
	REQUIRE(!rvm_cpu_lwp_discard(main_lwp));
	splsched();
	REQUIRE(cpu_switchto(main_lwp, main_lwp, false) == main_lwp);
	for (n = 0; n < 64; n++) {
		curcpu()->ci_onproc = &child;
		REQUIRE(rvm_switch_register_test(main_lwp, &child, 0x10000));
		REQUIRE(curlwp == main_lwp && curcpu()->ci_onproc == main_lwp && step == n + 1);
		REQUIRE(curcpu()->ci_cpl == IPL_SCHED);
	}
	spl0();
	REQUIRE(rvm_cpu_lwp_discard(&child) && !rvm_cpu_lwp_discard(&child));
	uvm_km_free(kernel_map, stack, USPACE, UVM_KMF_WIRED);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(pmap_resident_count(pmap_kernel()) == resident && pmap_wired_count(pmap_kernel()) == wired);
	REQUIRE(rvm_bootheap_live() == 0 && kernel_map->nentries == 0 && curlwp->l_nopreempt == 0);
	rvm_early_puts(context_ok);
}
