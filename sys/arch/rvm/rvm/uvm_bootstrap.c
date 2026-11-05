/*
 * Standalone UVM harness. Physical preload and raw-page initialization use
 * real MI code. Full kernel builds must use their MI globals and services
 * instead of this file. VM maps, objects and kernel main() remain pending.
 */
#include <sys/param.h>
#include <sys/extent.h>
#include <sys/systm.h>
#include <uvm/uvm.h>
#include <machine/boot.h>
#include <machine/bootmem.h>
#include <machine/mmu.h>
#include <machine/uvm_boot.h>

/* These match uvm_init.c's definitions; data works around FHR BSS exports. */
struct uvm uvm __attribute__((section(".data"))) = { 0 };
struct uvmexp uvmexp __attribute__((section(".data"))) = { 0 };
struct rvm_uvm_boot_state rvm_uvm_boot __attribute__((section(".data"))) = { 0 };

const char rvm_uvm_panic[] = "\nNetBSD/RVM UVM panic: ";
const char rvm_uvm_format_error[] = "Unsupported early printf format.\n";

/* Small early formatter for the formats used by MI physseg diagnostics.
 * Unsupported formats halt rather than misread the variadic arguments.
 */
struct early_args {
	const uint64_t *registers;
	unsigned int index;
};

static uint64_t
next_arg(struct early_args *args, unsigned int width)
{
	uint64_t value;

	if (args->index >= 7) {
		rvm_early_puts(rvm_uvm_format_error);
		rvm_boot_stop();
	}
	value = args->registers[args->index++];
	return width == 4 ? (uint32_t)value : value;
}

static void
early_vprintf(const char *fmt, struct early_args *args)
{
	char digits[24];
	uint64_t number;
	int length, base, count, negative;
	const char *s;

	while (*fmt != '\0') {
		if (*fmt != '%') {
			rvm_early_putchar(*fmt++);
			continue;
		}
		fmt++;
		length = 0;
		if (*fmt == 'l') {
			length = 1;
			if (*++fmt == 'l') {
				length = 2;
				fmt++;
			}
		}
		if (length == 0 && *fmt == '%') {
			rvm_early_putchar('%');
			fmt++;
			continue;
		}
		if (length == 0 && *fmt == 's') {
			s = (const char *)(uintptr_t)next_arg(args, sizeof(void *));
			if (s != NULL)
				rvm_early_puts(s);
			fmt++;
			continue;
		}
		negative = 0;
		if (*fmt == 'd') {
			int64_t value = length != 0 ? (int64_t)next_arg(args, 8) :
			    (int32_t)next_arg(args, 4);
			negative = value < 0;
			number = negative ? 0 - (uint64_t)value : (uint64_t)value;
			base = 10;
		} else if (*fmt == 'u' || *fmt == 'x') {
			number = next_arg(args, length != 0 ? 8 : 4);
			base = *fmt == 'x' ? 16 : 10;
		} else {
			rvm_early_puts(rvm_uvm_format_error);
			rvm_boot_stop();
		}
		fmt++;
		count = 0;
		do {
			unsigned int digit = number % base;
			digits[count++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
			number /= base;
		} while (number != 0);
		if (negative)
			rvm_early_putchar('-');
		while (count != 0)
			rvm_early_putchar(digits[--count]);
	}
}

/* VMAsm entry points capture R3-R9. GCC's incoming variadic register spilling
 * and outgoing stacked-argument frames are incomplete. MI physseg diagnostics
 * fit in these seven registers; stacked arguments are deliberately unsupported.
 */
void
rvm_uvm_diagnostic(const char *fmt, const uint64_t *registers, unsigned int fatal)
{
	struct early_args args = { registers, 0 };

	if (fatal)
		rvm_early_puts(rvm_uvm_panic);
	early_vprintf(fmt, &args);
	if (fatal) {
		rvm_early_putchar('\n');
		rvm_boot_stop();
	}
}

int
rvm_uvm_bootstrap(void)
{
	uvm_physseg_t seg;
	paddr_t pa;
	vaddr_t start, end;
	volatile uint64_t *probe;
	unsigned int i;

	if (!rvm_bootmem.sealed || rvm_uvm_boot.registered ||
	    uvm.page_init_done || rvm_bootmem.free_start >= rvm_bootmem.free_end ||
	    ((rvm_bootmem.free_start | rvm_bootmem.free_end) & PAGE_MASK) != 0)
		return 0;
	rvm_uvm_cpuinit();
	uvm_setpagesize();
	uvm_physseg_init();
	rvm_uvm_boot.first = atop(rvm_bootmem.free_start);
	rvm_uvm_boot.end = atop(rvm_bootmem.free_end);
	seg = uvm_page_physload(rvm_uvm_boot.first, rvm_uvm_boot.end,
	    rvm_uvm_boot.first, rvm_uvm_boot.end, VM_FREELIST_DEFAULT);
	if (!uvm_physseg_valid_p(seg) ||
	    uvm_physseg_get_start(seg) != rvm_uvm_boot.first ||
	    uvm_physseg_get_end(seg) != rvm_uvm_boot.end ||
	    uvm_physseg_find(rvm_uvm_boot.first - 1, NULL) != UVM_PHYSSEG_TYPE_INVALID ||
	    uvm_physseg_find(rvm_uvm_boot.end, NULL) != UVM_PHYSSEG_TYPE_INVALID)
		return 0;
	rvm_uvm_boot.registered = 1;
	pmap_virtual_space(&start, &end);
	pa = pmap_steal_memory(PAGE_SIZE, &start, &end);
	uvmexp.bootpages++;
	rvm_uvm_boot.probe = pa;
	probe = (volatile uint64_t *)(uintptr_t)pa;
	for (i = 0; i < PAGE_SIZE / sizeof(*probe); i++) {
		if (probe[i] != 0)
			return 0;
	}
	probe[0] = 0x55564d424f4f5431UL;
	probe[PAGE_SIZE / sizeof(*probe) - 1] = ~probe[0];
	if (probe[0] != 0x55564d424f4f5431UL ||
	    probe[PAGE_SIZE / sizeof(*probe) - 1] != ~probe[0] ||
	    uvm_physseg_get_avail_start(seg) != rvm_uvm_boot.first + 1 ||
	    uvm_physseg_find(atop(pa), NULL) != UVM_PHYSSEG_TYPE_INVALID)
		return 0;
	return 1;
}

int
rvm_uvm_pageinit(void)
{
	struct vm_page *page;
	paddr_t pa, extracted;
	volatile uint64_t *mapping;
	unsigned int i;
	int64_t before;

	if (!rvm_uvm_boot.registered || rvm_uvm_boot.initialized ||
	    uvm.page_init_done)
		return 0;
	physmem = atop(rvm_bootmem.ram_end);
	uvm_page_init(&rvm_uvm_boot.vm_start, &rvm_uvm_boot.vm_end);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!uvm.page_init_done || uvmexp.npages <= 0 ||
	    before != uvmexp.npages ||
	    rvm_uvm_boot.vm_start != VM_KERNEL_VM_BASE ||
	    rvm_uvm_boot.vm_end != VM_KERNEL_VM_BASE + VM_KERNEL_VM_SIZE)
		return 0;
	rvm_uvm_boot.managed_pages = uvmexp.npages;
	page = uvm_pagealloc_strat(NULL, 0, NULL, UVM_PGA_ZERO | UVM_PGA_USERESERVE,
	    UVM_PGA_STRAT_NORMAL, VM_FREELIST_DEFAULT);
	if (page == NULL || curcpu()->ci_counts[CPU_COUNT_FREEPAGES] != before - 1)
		return 0;
	pa = VM_PAGE_TO_PHYS(page);
	if (PHYS_TO_VM_PAGE(pa) != page || pa < rvm_bootmem.free_start ||
	    pa >= rvm_bootmem.free_end || pa == rvm_uvm_boot.probe)
		return 0;
	pmap_kenter_pa(rvm_uvm_boot.vm_start, pa, VM_PROT_READ | VM_PROT_WRITE, 0);
	pmap_update(pmap_kernel());
	mapping = (volatile uint64_t *)rvm_uvm_boot.vm_start;
	for (i = 0; i < PAGE_SIZE / sizeof(*mapping); i++) {
		if (mapping[i] != 0)
			return 0;
	}
	mapping[0] = 0x55564d5041474531UL;
	mapping[PAGE_SIZE / sizeof(*mapping) - 1] = ~mapping[0];
	if (!pmap_extract(pmap_kernel(), rvm_uvm_boot.vm_start, &extracted) ||
	    extracted != pa || *(volatile uint64_t *)(uintptr_t)pa != mapping[0] ||
	    mapping[PAGE_SIZE / sizeof(*mapping) - 1] != ~mapping[0])
		return 0;
	pmap_kremove(rvm_uvm_boot.vm_start, PAGE_SIZE);
	pmap_update(pmap_kernel());
	uvm_pagefree(page);
	if (curcpu()->ci_counts[CPU_COUNT_FREEPAGES] != before ||
	    pmap_extract(pmap_kernel(), rvm_uvm_boot.vm_start, &extracted) ||
	    curcpu()->ci_cpl != IPL_NONE)
		return 0;
	rvm_uvm_boot.initialized = 1;
	return 1;
}
