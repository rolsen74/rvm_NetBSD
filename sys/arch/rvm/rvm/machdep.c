/* NetBSD/RVM startup with managed pages and a VA map; full UVM/main pending. */
#include <machine/boot.h>
#include <machine/psl.h>
#include <machine/bootmem.h>
#include <machine/mmu.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>
#include <machine/uvm_boot.h>
#include <machine/bootalloc.h>
#include <machine/bootheap.h>
#include <machine/bootpool.h>
#include <machine/bootcache.h>
#include <machine/bootanon.h>
#include <machine/bootamap.h>
#include <machine/bootanonmap.h>
#include <machine/switch.h>
#include <machine/generic.h>

/* 1: C, 2: console, 3: memory, 4: pmap, 5: MMU, 6/7: verified, 8/9: UVM,
 * 10: wired buffers, 11: MI map, 12: objects, 13: MI km, 14: heap, 15: pools, 16: caches, 17: anons, 18: amaps, 19: anon VM entries, 20: kernel contexts. */
volatile unsigned long rvm_boot_stage
    __attribute__((section(".data"))) = 0;
static volatile unsigned int rvm_boot_fault_active
    __attribute__((section(".data"))) = 0;
/* Current GNU FHR objects cannot export BSS symbols across object files. */
char rvm_boot_stack[RVM_BOOT_STACK_SIZE] __aligned(16)
    __attribute__((section(".data"))) = { 0 };
void (*rvm_boot_vectors[RVM_BOOT_NVECTORS])(void) __aligned(16)
    __attribute__((section(".data"))) = { 0 };
const char rvm_boot_banner[] = "NetBSD/RVM early bootstrap";

/* Named strings avoid temporary .LC symbols lost by the current FHR tools. */
const char rvm_msg_sr[] = "\nSupervisor SR: 0x";
const char rvm_msg_stack[] = "\nKernel stack: 0x";
const char rvm_msg_range[] = "..0x";
const char rvm_msg_vectors[] = "\nException vectors installed; MMU and interrupts disabled.\n";
const char rvm_msg_ready[] = "Kernel pmap ready; bootstrap allocator sealed for UVM handoff.\n";
const char rvm_msg_badboot[] = "Invalid loader memory handoff.\n";
const char rvm_msg_badpmap[] = "Cannot build or verify bootstrap page tables.\n";
const char rvm_msg_badalias[] = "MMU alias test failed.\n";
const char rvm_msg_badkernel[] = "Kernel pmap mapping test failed.\n";
const char rvm_msg_baduvm[] = "UVM physical memory handoff failed.\n";
const char rvm_msg_uvmrange[] = "UVM registered PFN range: 0x";
const char rvm_msg_uvmprobe[] = "\nUVM bootstrap allocation: 0x";
const char rvm_msg_uvmready[] =
    "\nUVM physical memory registered; bootstrap allocation verified.\n";
const char rvm_msg_managed[] = "UVM managed pages: 0x";
const char rvm_msg_pageinit[] =
    "\nUVM page allocator ready; allocation/free verified.\n";
const char rvm_msg_wired[] =
    "Early wired allocator ready; zeroed allocation/free verified.\n";
const char rvm_msg_map[] =
    "UVM kernel VA map ready; reservations and wired buffers verified.\n";
const char rvm_msg_objects[] =
    "UVM resident objects ready; page ownership and wired allocation verified.\n";
const char rvm_msg_km[] =
    "UVM kernel allocator ready; wired and VA-only allocation/free verified.\n";
const char rvm_msg_badkm[] = "UVM kernel allocation verification failed.\n";
const char rvm_msg_heap[] =
    "Bootstrap kmem heap ready; subpage allocation and span release verified.\n";
const char rvm_msg_badheap[] = "Bootstrap heap verification failed.\n";
const char rvm_msg_pool[] =
    "Bootstrap MI pools ready; item allocation and page release verified.\n";
const char rvm_msg_cache[] =
    "Bootstrap MI pool caches ready; constructors and magazine reuse verified.\n";
const char rvm_msg_anon[] =
    "UVM anonymous pages ready; references, zeroing and release verified.\n";
const char rvm_msg_amap[] =
    "UVM anonymous maps ready; sparse slots, sharing and release verified.\n";
const char rvm_msg_anonmap[] =
    "UVM anonymous mappings ready; VM entries, shared aliases and detach verified.\n";
const char rvm_msg_switch[] =
    "RVM kernel context switching ready; separate LWP stacks and resume verified.\n";
const char rvm_msg_badswitch[] = "RVM kernel context switch verification failed.\n";
const char rvm_msg_badanonmap[] = "UVM anonymous mapping verification failed.\n";
const char rvm_msg_badamap[] = "UVM anonymous map verification failed.\n";
const char rvm_msg_badanon[] = "UVM anonymous page verification failed.\n";
const char rvm_msg_badcache[] = "Bootstrap pool cache verification failed.\n";
const char rvm_msg_badpool[] = "Bootstrap pool verification failed.\n";
const char rvm_msg_badwired[] = "Early wired allocator verification failed.\n";
const char rvm_msg_ram[] = "RAM end: 0x";
const char rvm_msg_reserved[] = "\nReserved prefix end: 0x";
const char rvm_msg_root[] = "\nSupervisor MMU root: 0x";
const char rvm_msg_free[] = "\nFree physical range for UVM: 0x";
const char rvm_msg_mmu[] = "\nMMU enabled; non-identity mapping verified.\n";
const char rvm_msg_trap[] = "\nNetBSD/RVM fatal startup exception: vector 0x";
const char rvm_msg_version[] = " version 0x";
const char rvm_msg_pc[] = " PC 0x";
const char rvm_msg_saved_sr[] = " SR 0x";
const char rvm_msg_stopped[] = "\nCPU stopped.\n";

static int
rvm_verify_boot_pmap(void)
{
	paddr_t pa;
	unsigned long flags;

	if (rvm_boot_pmap_lookup(0, &pa, &flags) ||
	    rvm_boot_pmap_lookup(RVM_BOOT_TEST_VA + RVM_MMU_PAGE_SIZE, &pa, &flags))
		return 0;
	if (!rvm_boot_pmap_lookup((uintptr_t)rvm_early_init, &pa, &flags) ||
	    pa != (uintptr_t)rvm_early_init ||
	    (flags & (RVM_PTE_READ | RVM_PTE_WRITE | RVM_PTE_EXEC)) !=
	    (RVM_PTE_READ | RVM_PTE_EXEC))
		return 0;
	if (!rvm_boot_pmap_lookup((uintptr_t)rvm_boot_stack, &pa, &flags) ||
	    pa != (uintptr_t)rvm_boot_stack ||
	    (flags & (RVM_PTE_READ | RVM_PTE_WRITE | RVM_PTE_EXEC)) !=
	    (RVM_PTE_READ | RVM_PTE_WRITE))
		return 0;
	if (!rvm_boot_pmap_lookup((uintptr_t)&kernel_pmap_ptr, &pa, &flags) ||
	    pa != (uintptr_t)&kernel_pmap_ptr ||
	    (flags & (RVM_PTE_READ | RVM_PTE_WRITE | RVM_PTE_EXEC)) != RVM_PTE_READ)
		return 0;
	if (!rvm_boot_pmap_lookup(RVM_GENERIC_ADDR, &pa, &flags) ||
	    pa != RVM_GENERIC_ADDR || (flags & RVM_PTE_EXEC) != 0)
		return 0;
	if (!rvm_boot_pmap_lookup(RVM_BOOT_TEST_VA, &pa, &flags) ||
	    pa != rvm_boot_pmap.test_page || (flags & RVM_PTE_EXEC) != 0)
		return 0;
	return 1;
}

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	volatile uint64_t *alias;
	volatile uint64_t *physical;
	paddr_t pa;
	vaddr_t buffer;

	rvm_boot_stage = 1;
	rvm_early_consinit();
	rvm_boot_stage = 2;
	rvm_early_puts(rvm_boot_banner);
	rvm_early_puts(rvm_msg_sr);
	rvm_early_puthex(rvm_boot_get_sr());
	rvm_early_puts(rvm_msg_stack);
	rvm_early_puthex((uintptr_t)rvm_boot_stack);
	rvm_early_puts(rvm_msg_range);
	rvm_early_puthex((uintptr_t)rvm_boot_stack_top);
	rvm_early_puts(rvm_msg_vectors);
	if (!rvm_bootmem_init(bi)) {
		rvm_early_puts(rvm_msg_badboot);
		rvm_boot_stop();
	}
	rvm_boot_stage = 3;
	rvm_early_puts(rvm_msg_ram);
	rvm_early_puthex(rvm_bootmem.ram_end);
	rvm_early_puts(rvm_msg_reserved);
	rvm_early_puthex(rvm_bootmem.loaded_end);
	if (!pmap_bootstrap() || !rvm_verify_boot_pmap() || !rvm_bootmem_seal()) {
		rvm_early_puts(rvm_msg_badpmap);
		rvm_boot_stop();
	}
	rvm_boot_stage = 4;
	rvm_early_puts(rvm_msg_root);
	rvm_early_puthex(rvm_boot_pmap.root);
	rvm_early_puts(rvm_msg_free);
	rvm_early_puthex(rvm_bootmem.free_start);
	rvm_early_puts(rvm_msg_range);
	rvm_early_puthex(rvm_bootmem.free_end);
	rvm_boot_enable_mmu(rvm_boot_pmap.root);
	rvm_boot_stage = 5;
	alias = (volatile uint64_t *)RVM_BOOT_TEST_VA;
	physical = (volatile uint64_t *)(uintptr_t)rvm_boot_pmap.test_page;
	alias[0] = 0x52564d5041474531UL;
	physical[RVM_MMU_PAGE_SIZE / sizeof(uint64_t) - 1] = ~alias[0];
	if (rvm_boot_get_cr() != RVM_CR_MMU ||
	    physical[0] != 0x52564d5041474531UL ||
	    alias[RVM_MMU_PAGE_SIZE / sizeof(uint64_t) - 1] != ~physical[0]) {
		rvm_early_puts(rvm_msg_badalias);
		rvm_boot_stop();
	}
	rvm_boot_stage = 6;
	rvm_early_puts(rvm_msg_mmu);
	/* Exercise a wired mapping in the actual kernel VM window after sealing. */
	pmap_kenter_pa(VM_KERNEL_VM_BASE, rvm_boot_pmap.test_page,
	    VM_PROT_READ | VM_PROT_WRITE, PMAP_NOCACHE);
	pmap_update(pmap_kernel());
	alias = (volatile uint64_t *)VM_KERNEL_VM_BASE;
	alias[0] = 0x52564d4b50414745UL;
	if (!pmap_extract(pmap_kernel(), VM_KERNEL_VM_BASE + 8, &pa) ||
	    pa != rvm_boot_pmap.test_page + 8 || physical[0] != 0x52564d4b50414745UL) {
		rvm_early_puts(rvm_msg_badkernel);
		rvm_boot_stop();
	}
	pmap_kremove(VM_KERNEL_VM_BASE, RVM_MMU_PAGE_SIZE);
	pmap_update(pmap_kernel());
	if (pmap_extract(pmap_kernel(), VM_KERNEL_VM_BASE, &pa)) {
		rvm_early_puts(rvm_msg_badkernel);
		rvm_boot_stop();
	}
	rvm_boot_stage = 7;
	rvm_early_puts(rvm_msg_ready);
	if (!rvm_uvm_bootstrap()) {
		rvm_early_puts(rvm_msg_baduvm);
		rvm_boot_stop();
	}
	rvm_boot_stage = 8;
	rvm_early_puts(rvm_msg_uvmrange);
	rvm_early_puthex(rvm_uvm_boot.first);
	rvm_early_puts(rvm_msg_range);
	rvm_early_puthex(rvm_uvm_boot.end);
	rvm_early_puts(rvm_msg_uvmprobe);
	rvm_early_puthex(rvm_uvm_boot.probe);
	rvm_early_puts(rvm_msg_uvmready);
#ifdef RVM_TEST_PRELOAD_ONLY
	/* Regression probes can exercise stealing before uvm_page_init closes it. */
	return;
#endif
	if (!rvm_uvm_pageinit()) {
		rvm_early_puts(rvm_msg_baduvm);
		rvm_boot_stop();
	}
	rvm_boot_stage = 9;
	rvm_early_puts(rvm_msg_managed);
	rvm_early_puthex(rvm_uvm_boot.managed_pages);
	rvm_early_puts(rvm_msg_pageinit);
	/* Two pages exercise the object's radix-tree node allocation too. */
	if (!rvm_bootalloc_init()) {
		rvm_early_puts(rvm_msg_badwired);
		rvm_boot_stop();
	}
	buffer = rvm_bootalloc(RVM_MMU_PAGE_SIZE + 1);
	if (buffer == 0) {
		rvm_early_puts(rvm_msg_badwired);
		rvm_boot_stop();
	}
	alias = (volatile uint64_t *)buffer;
	if (alias[0] != 0 || alias[2 * RVM_MMU_PAGE_SIZE / sizeof(*alias) - 1] != 0) {
		rvm_early_puts(rvm_msg_badwired);
		rvm_boot_stop();
	}
	alias[0] = 0x52564d5749524544UL;
	alias[2 * RVM_MMU_PAGE_SIZE / sizeof(*alias) - 1] = ~alias[0];
	if (alias[0] != 0x52564d5749524544UL ||
	    alias[2 * RVM_MMU_PAGE_SIZE / sizeof(*alias) - 1] != ~alias[0] ||
	    !rvm_bootfree(buffer)) {
		rvm_early_puts(rvm_msg_badwired);
		rvm_boot_stop();
	}
	rvm_boot_stage = 10;
	rvm_early_puts(rvm_msg_wired);
	rvm_boot_stage = 11;
	rvm_early_puts(rvm_msg_map);
	rvm_boot_stage = 12;
	rvm_early_puts(rvm_msg_objects);
	if (!rvm_uvm_km_verify()) {
		rvm_early_puts(rvm_msg_badkm);
		rvm_boot_stop();
	}
	rvm_boot_stage = 13;
	rvm_early_puts(rvm_msg_km);
	if (!rvm_bootheap_verify()) {
		rvm_early_puts(rvm_msg_badheap);
		rvm_boot_stop();
	}
	rvm_boot_stage = 14;
	rvm_early_puts(rvm_msg_heap);
	if (!rvm_bootpool_verify()) {
		rvm_early_puts(rvm_msg_badpool);
		rvm_boot_stop();
	}
	rvm_boot_stage = 15;
	rvm_early_puts(rvm_msg_pool);
	if (!rvm_bootcache_verify()) {
		rvm_early_puts(rvm_msg_badcache);
		rvm_boot_stop();
	}
	rvm_boot_stage = 16;
	rvm_early_puts(rvm_msg_cache);
	if (!rvm_bootanon_verify()) {
		rvm_early_puts(rvm_msg_badanon);
		rvm_boot_stop();
	}
	rvm_boot_stage = 17;
	rvm_early_puts(rvm_msg_anon);
	if (!rvm_bootamap_verify()) {
		rvm_early_puts(rvm_msg_badamap);
		rvm_boot_stop();
	}
	rvm_boot_stage = 18;
	rvm_early_puts(rvm_msg_amap);
	if (!rvm_bootanonmap_verify()) {
		rvm_early_puts(rvm_msg_badanonmap);
		rvm_boot_stop();
	}
	rvm_boot_stage = 19;
	rvm_early_puts(rvm_msg_anonmap);
	if (!rvm_cpu_switch_verify()) {
		rvm_early_puts(rvm_msg_badswitch);
		rvm_boot_stop();
	}
	rvm_boot_stage = 20;
	rvm_early_puts(rvm_msg_switch);
}

void
rvm_boot_trap(const uint64_t *saved_gprs)
{
	const uint64_t *frame = saved_gprs + 63;

	/* A fault while reporting a fault must not exhaust the supervisor stack. */
	if (rvm_boot_fault_active != 0)
		rvm_boot_stop();
	rvm_boot_fault_active = 1;
	/* Common BE hardware frame prefix, as used by SnakeOS handlers. */
	rvm_boot_stage = 0xff;
	rvm_early_puts(rvm_msg_trap);
	rvm_early_puthex((frame[0] >> 32) & 0xffff);
	rvm_early_puts(rvm_msg_version);
	rvm_early_puthex(frame[0] >> 48);
	rvm_early_puts(rvm_msg_pc);
	rvm_early_puthex(frame[1]);
	rvm_early_puts(rvm_msg_saved_sr);
	rvm_early_puthex(frame[0] & 0xffff);
	rvm_early_puts(rvm_msg_stopped);
	rvm_boot_stop();
}
