/* Normal NetBSD startup: loader -> physical memory -> MI kernel main(). */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/kernel.h>
#include <sys/msgbuf.h>
#include <sys/mount.h>
#include <sys/pmf.h>
#include <sys/reboot.h>
#include <dev/mm.h>
#include <uvm/uvm.h>
#include <machine/boot.h>
#include <machine/bootmem.h>
#include <machine/pmap.h>
#include <machine/pmap_private.h>
#include <machine/mmu.h>
#include <machine/pcb.h>
#include <machine/bootcopy.h>
#include <machine/psl.h>
#include <machine/user.h>
#include <machine/cache.h>

char machine[] = MACHINE;
char machine_arch[] = MACHINE_ARCH;
struct cpu_info cpu_info_primary;
#ifdef MULTIPROCESSOR
struct cpu_info cpu_info_secondary[MAXCPUS - 1];
unsigned int rvm_cpu_count = 1;
volatile uint64_t rvm_secondary_go;
volatile uint64_t rvm_secondary_started[MAXCPUS];
volatile uint64_t rvm_secondary_arrived[MAXCPUS];
char rvm_secondary_boot_stacks[MAXCPUS - 1][RVM_BOOT_STACK_SIZE]
    __aligned(PAGE_SIZE);
struct cpu_info *rvm_secondary_ci_ptrs[MAXCPUS] = {
	&cpu_info_primary, &cpu_info_secondary[0], &cpu_info_secondary[1],
	&cpu_info_secondary[2], &cpu_info_secondary[3],
	&cpu_info_secondary[4], &cpu_info_secondary[5],
	&cpu_info_secondary[6]
};
char *rvm_secondary_boot_stack_tops[MAXCPUS] = {
	rvm_boot_stack + RVM_BOOT_STACK_SIZE,
	rvm_secondary_boot_stacks[0] + RVM_BOOT_STACK_SIZE,
	rvm_secondary_boot_stacks[1] + RVM_BOOT_STACK_SIZE,
	rvm_secondary_boot_stacks[2] + RVM_BOOT_STACK_SIZE,
	rvm_secondary_boot_stacks[3] + RVM_BOOT_STACK_SIZE,
	rvm_secondary_boot_stacks[4] + RVM_BOOT_STACK_SIZE,
	rvm_secondary_boot_stacks[5] + RVM_BOOT_STACK_SIZE,
	rvm_secondary_boot_stacks[6] + RVM_BOOT_STACK_SIZE
};
void rvm_icnt_secondary_start(void);
vaddr_t rvm_secondary_hatch(void);
void
cpu_boot_secondary_processors(void)
{
	if (rvm_cpu_count < 2) return;
	for (unsigned int id = 1; id < rvm_cpu_count; id++)
		KASSERT(rvm_secondary_arrived[id] != 0);
	rvm_secondary_go = 1;
	for (unsigned int id = 1; id < rvm_cpu_count; id++) {
		for (unsigned int i = 0; i < 10000 &&
		    rvm_secondary_started[id] == 0; i++)
			delay(100);
		if (rvm_secondary_started[id] == 0)
			panic("RVM cpu%u failed to start", id);
	}
}

vaddr_t
rvm_secondary_hatch(void)
{
	struct cpu_info *ci = curcpu();
	unsigned int id = ci->ci_cpuid;
	KASSERT(id > 0 && id < rvm_cpu_count);
	struct lwp *idle = ci->ci_data.cpu_idlelwp;
	ci->ci_curlwp = idle;
	ci->ci_onproc = idle;
	ci->ci_cpl = IPL_HIGH;
	rvm_boot_enable_mmu(rvm_boot_pmap.root);
	rvm_set_user_root(0);
	rvm_icnt_secondary_start();
	rvm_secondary_started[id] = 1;
	return (vaddr_t)idle->l_addr + USPACE;
}
#endif
struct vm_map *phys_map;
char rvm_boot_stack[RVM_BOOT_STACK_SIZE] __aligned(PAGE_SIZE);
void (*rvm_boot_vectors[RVM_BOOT_NVECTORS])(void) __aligned(16);
volatile unsigned long rvm_boot_stage;

void main(void);
void abort(void) __dead;

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	paddr_t logbuf;
	struct pcb *pcb;

	consinit();
	rvm_early_puts("NetBSD/RVM normal kernel entry\n");
	if (!rvm_bootmem_init(bi)) {
		rvm_early_puts("Invalid loader handoff\n");
		rvm_boot_stop();
	}
	logbuf = rvm_bootmem_alloc(MSGBUFSIZE, PAGE_SIZE);
	if (logbuf == 0 || !pmap_bootstrap() || !rvm_bootmem_seal()) {
		rvm_early_puts("Cannot initialize kernel page tables\n");
		rvm_boot_stop();
	}
	rvm_boot_enable_mmu(rvm_boot_pmap.root);
#ifdef RVM_KERNEL_ICACHE
	rvm_early_puts((rvm_boot_get_cr() & RVM_CR_INSTCACHE) ?
	    "RVM kernel instruction cache enabled; data cache disabled\n" :
	    "RVM kernel instruction cache disabled by VMEmu\n");
#endif
#ifdef RVM_MMU_TLB
    if (!(rvm_boot_get_cr() & RVM_CR_TLB)) panic("VMEmu lacks RVM TLB support");
    rvm_early_puts("RVM TLB enabled (64 KiB pages, root/mode tags)\n");
#endif
#ifdef RVM_USER_ICACHE
    rvm_early_puts("RVM user instruction cache enabled (physical tags)\n");
#endif
	initmsgbuf((void *)logbuf, MSGBUFSIZE);
	cpu_info_primary.ci_curlwp = &lwp0;
	cpu_info_primary.ci_onproc = &lwp0;
	cpu_info_primary.ci_cpl = IPL_HIGH;
	lwp0.l_cpu = &cpu_info_primary;
	uvm_lwp_setuarea(&lwp0, (vaddr_t)rvm_boot_stack);
	pcb = lwp0.l_addr;
	memset(pcb, 0, sizeof(*pcb));
	pcb->pcb_mmu_table = rvm_boot_pmap.root;
	pcb->pcb_sr = PSL_S;
	lwp0.l_md.md_pcb = pcb;
	lwp0.l_md.md_kstack_base = (vaddr_t)rvm_boot_stack + roundup(sizeof(*pcb), 16);
	lwp0.l_md.md_kstack_size = USPACE - roundup(sizeof(*pcb), 16);
	if (!rvm_kcopy_init()) panic("RVM copy fault vectors unavailable");
	rvm_user_vectors_init();
	uvm_md_init();
	physmem = atop(rvm_bootmem.ram_end);
	uvm_page_physload(atop(rvm_bootmem.free_start), atop(rvm_bootmem.free_end),
	    atop(rvm_bootmem.free_start), atop(rvm_bootmem.free_end),
	    VM_FREELIST_DEFAULT);
	rvm_boot_stage = 1;
	rvm_early_puts("RVM memory handoff ready; entering NetBSD main()\n");
	main();
	panic("main returned");
}

void
cpu_startup(void)
{
	vaddr_t minaddr = 0, maxaddr;
	(void)cpu_setmodel("RVM 64-bit virtual CPU");
	banner();
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    VM_PHYS_SIZE, 0, false, NULL);
}

void
rvm_boot_trap(const uint64_t *saved)
{
	const uint64_t *frame = saved + 63;

	/* Use only the early console: faults can precede the MI panic machinery. */
	rvm_early_puts("\nNetBSD/RVM fatal kernel exception: vector ");
	rvm_early_puthex((frame[0] >> 32) & 0xffff);
	rvm_early_puts(" PC ");
	rvm_early_puthex(frame[1]);
	rvm_early_puts(" header ");
	rvm_early_puthex(frame[0]);
	rvm_early_puts(" root ");
	rvm_early_puthex(frame[2]);
	if ((frame[0] >> 48) == 3) {
		rvm_early_puts(" virtual ");
		rvm_early_puthex(frame[3]);
		rvm_early_puts(" physical ");
		rvm_early_puthex(frame[4]);
	}
	rvm_early_putchar('\n');
	rvm_boot_stop();
}

void rvm_boot_exit(void) __dead;
void rvm_boot_reset(void) __dead;

void
cpu_reboot(int howto, char *bootstr __unused)
{
	if (!cold && !(howto & RB_NOSYNC))
		vfs_shutdown();
	doshutdownhooks();
	pmf_system_shutdown(howto);
	if (howto & RB_HALT) {
		rvm_early_puts("NetBSD/RVM powering off\n");
		rvm_boot_exit();
	}
	rvm_early_puts("NetBSD/RVM rebooting\n");
	rvm_boot_reset();
}

void abort(void) { panic("RVM kernel abort"); }

int
mm_md_physacc(paddr_t pa, vm_prot_t prot __unused)
{
	return pa < rvm_bootmem.ram_end ? 0 : EFAULT;
}

int
mm_md_kernacc(void *ptr, vm_prot_t prot, bool *handled)
{
	vaddr_t va = (vaddr_t)ptr;
	paddr_t pa;
	unsigned long flags;

	/* The low identity map is outside kernel_map, so uvm_kernacc cannot
	 * validate it.  Check the actual PTE and leave kernel_map to UVM. */
	if (va >= rvm_bootmem.ram_end)
		return 0;
	*handled = true;
	if (!rvm_boot_pmap_lookup(va, &pa, &flags) || pa != va ||
	    (prot & VM_PROT_READ && !(flags & RVM_PTE_READ)) ||
	    (prot & VM_PROT_WRITE && !(flags & RVM_PTE_WRITE)))
		return EFAULT;
	return 0;
}
