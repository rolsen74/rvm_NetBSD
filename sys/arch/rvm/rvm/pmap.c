/*
 * Supervisor pmap for the RVM six-level hardware walker. The initial wired
 * kernel window has preallocated tables: editing it never steals UVM memory.
 * Single CPU, interrupts and caches disabled; user pmaps/PV tracking pending.
 */
#include <sys/null.h>
#include <machine/boot.h>
#include <machine/bootmem.h>
#include <machine/generic.h>
#include <machine/mmu.h>
#include <machine/pmap.h>
#include <machine/psl.h>
#include <machine/vmparam.h>

struct rvm_boot_pmap rvm_boot_pmap __attribute__((section(".data"))) = { 0 };
struct pmap kernel_pmap_store __attribute__((section(".data"))) = { 0 };
/* kernel_pmap_ptr is emitted by locore: GCC cannot yet emit pointer constants. */
const char rvm_msg_pmap_error[] = "Invalid or unsupported kernel pmap operation.\n";

static void __dead
pmap_fail(void)
{
	rvm_early_puts(rvm_msg_pmap_error);
	rvm_boot_stop();
}

static paddr_t
new_table(void)
{
	paddr_t pa = rvm_bootmem_alloc(RVM_MMU_TABLE_SIZE, RVM_MMU_TABLE_SIZE);

	if (pa != 0)
		rvm_boot_pmap.tables++;
	return pa;
}

static volatile uint64_t *
pte_lookup(vaddr_t va, int create)
{
	volatile uint64_t *table;
	unsigned int shift, index;
	paddr_t child;

	if (rvm_boot_pmap.root == 0)
		return NULL;
	table = (volatile uint64_t *)(uintptr_t)rvm_boot_pmap.root;
	for (shift = 56; shift > RVM_MMU_PAGE_SHIFT; shift -= 8) {
		index = (va >> shift) & 255;
		if ((table[index] & RVM_PTE_VALID) == 0) {
			if (!create)
				return NULL;
			child = new_table();
			if (child == 0)
				return NULL;
			table[index] = child | RVM_PTE_VALID;
		}
		table = (volatile uint64_t *)(uintptr_t)
		    (table[index] & ~RVM_MMU_TABLE_MASK);
	}
	index = (va >> RVM_MMU_PAGE_SHIFT) & 255;
	return &table[index];
}

static int
map_page(vaddr_t va, paddr_t pa, unsigned long permissions)
{
	volatile uint64_t *pte = pte_lookup(va, 1);

	if (pte == NULL || (*pte & RVM_PTE_VALID) != 0)
		return 0;
	*pte = pa | permissions | RVM_PTE_VALID | RVM_PTE_SUPERVISOR;
	kernel_pmap_store.pm_stats.resident_count++;
	kernel_pmap_store.pm_stats.wired_count++;
	return 1;
}

int
rvm_boot_pmap_lookup(vaddr_t va, paddr_t *pa, unsigned long *flags)
{
	volatile uint64_t *pte;
	uint64_t entry;

	pte = pte_lookup(va, 0);
	if (pte == NULL)
		return 0;
	entry = *pte;
	if ((entry & RVM_PTE_VALID) == 0)
		return 0;
	*pa = (entry & ~RVM_MMU_PAGE_MASK) | (va & RVM_MMU_PAGE_MASK);
	*flags = entry & RVM_MMU_PAGE_MASK;
	return 1;
}

static unsigned long
ram_permissions(paddr_t pa)
{
	unsigned int i;

	for (i = 0; i < rvm_bootinfo.nsections; i++) {
		const struct rvm_boot_section *s = &rvm_bootinfo.section[i];
		unsigned long flags = 0;

		if (pa < s->start || pa >= s->start + s->size)
			continue;
		/* FHR code sections may specify EXEC without an explicit READ bit. */
		if (s->flags & (RVM_BOOT_READ | RVM_BOOT_EXEC))
			flags |= RVM_PTE_READ;
		if (s->flags & RVM_BOOT_WRITE)
			flags |= RVM_PTE_WRITE;
		if (s->flags & RVM_BOOT_EXEC)
			flags |= RVM_PTE_EXEC;
		return flags;
	}
	/* Unallocated RAM/page tables: supervisor R/W, never executable. */
	return RVM_PTE_READ | RVM_PTE_WRITE;
}

int
pmap_bootstrap(void)
{
	paddr_t pa;
	vaddr_t va;

	rvm_boot_pmap.root = new_table();
	if (rvm_boot_pmap.root == 0)
		return 0;
	kernel_pmap_store.pm_root = rvm_boot_pmap.root;
	/* Keep physical pointers valid during this initial bring-up stage. */
	for (pa = RVM_MMU_PAGE_SIZE; pa < rvm_bootmem.ram_end;
	    pa += RVM_MMU_PAGE_SIZE) {
		if (!map_page(pa, pa, ram_permissions(pa)))
			return 0;
	}
	/* GeneralDriver register window, uncached supervisor R/W, non-executable. */
	if (!map_page(RVM_GENERIC_ADDR, RVM_GENERIC_ADDR,
	    RVM_PTE_READ | RVM_PTE_WRITE))
		return 0;
	/* A separate VA proves translation, rather than identity-only execution. */
	rvm_boot_pmap.test_page =
	    rvm_bootmem_alloc(RVM_MMU_PAGE_SIZE, RVM_MMU_PAGE_SIZE);
	if (rvm_boot_pmap.test_page == 0 ||
	    !map_page(RVM_BOOT_TEST_VA, rvm_boot_pmap.test_page,
	    RVM_PTE_READ | RVM_PTE_WRITE))
		return 0;
	/* Each leaf table covers 256 x 64 KiB = 16 MiB. */
	for (va = VM_KERNEL_VM_BASE;
	    va < VM_KERNEL_VM_BASE + VM_KERNEL_VM_SIZE;
	    va += RVM_MMU_ENTRIES * RVM_MMU_PAGE_SIZE) {
		if (pte_lookup(va, 1) == NULL)
			return 0;
	}
	return 1;
}

static void
check_kernel_range(vaddr_t va, vsize_t size)
{
	vaddr_t end = VM_KERNEL_VM_BASE + VM_KERNEL_VM_SIZE;

	if (va < VM_KERNEL_VM_BASE || va > end || size > end - va ||
	    ((va | size) & RVM_MMU_PAGE_MASK) != 0)
		pmap_fail();
}

static unsigned long
pte_permissions(vm_prot_t prot)
{
	unsigned long flags = 0;

	if ((prot & ~VM_PROT_ALL) != 0)
		pmap_fail();
	if (prot & VM_PROT_READ)
		flags |= RVM_PTE_READ;
	if (prot & VM_PROT_WRITE)
		flags |= RVM_PTE_WRITE;
	if (prot & VM_PROT_EXECUTE)
		flags |= RVM_PTE_EXEC;
	return flags;
}

void
pmap_virtual_space(vaddr_t *start, vaddr_t *end)
{
	*start = VM_KERNEL_VM_BASE;
	*end = VM_KERNEL_VM_BASE + VM_KERNEL_VM_SIZE;
}

void
pmap_kenter_pa(vaddr_t va, paddr_t pa, vm_prot_t prot, u_int flags)
{
	volatile uint64_t *pte;
	unsigned long permissions = pte_permissions(prot);

	check_kernel_range(va, RVM_MMU_PAGE_SIZE);
	/* All current mappings are uncached; reject unsupported cache policies. */
	if ((pa & RVM_MMU_PAGE_MASK) != 0 || permissions == 0 ||
	    (flags != 0 && flags != PMAP_NOCACHE))
		pmap_fail();
	pte = pte_lookup(va, 0);
	if (pte == NULL)
		pmap_fail();
	if ((*pte & RVM_PTE_VALID) == 0) {
		kernel_pmap_store.pm_stats.resident_count++;
		kernel_pmap_store.pm_stats.wired_count++;
	}
	/* Replacement clears hardware access bits belonging to the old page. */
	*pte = pa | permissions | RVM_PTE_VALID | RVM_PTE_SUPERVISOR;
}

void
pmap_kremove(vaddr_t va, vsize_t size)
{
	volatile uint64_t *pte;
	vaddr_t end;

	check_kernel_range(va, size);
	end = va + size;
	for (; va < end; va += RVM_MMU_PAGE_SIZE) {
		pte = pte_lookup(va, 0);
		if (pte != NULL && (*pte & RVM_PTE_VALID) != 0) {
			*pte = 0;
			kernel_pmap_store.pm_stats.resident_count--;
			kernel_pmap_store.pm_stats.wired_count--;
		}
	}
}

void
pmap_remove(pmap_t pm, vaddr_t start, vaddr_t end)
{
	if (pm != pmap_kernel() || end < start)
		pmap_fail();
	pmap_kremove(start, end - start);
}

void
pmap_protect(pmap_t pm, vaddr_t start, vaddr_t end, vm_prot_t prot)
{
	volatile uint64_t *pte;
	unsigned long keep = pte_permissions(prot);
	unsigned long mask = RVM_PTE_READ | RVM_PTE_WRITE | RVM_PTE_EXEC;

	if (pm != pmap_kernel() || end < start)
		pmap_fail();
	check_kernel_range(start, end - start);
	if (prot == VM_PROT_NONE) {
		pmap_remove(pm, start, end);
		return;
	}
	for (; start < end; start += RVM_MMU_PAGE_SIZE) {
		pte = pte_lookup(start, 0);
		if (pte != NULL && (*pte & RVM_PTE_VALID) != 0)
			*pte &= ~mask | keep; /* Protection may only remove permissions. */
	}
}

bool
pmap_extract(pmap_t pm, vaddr_t va, paddr_t *pa)
{
	paddr_t physical;
	unsigned long flags;

	if (pm != pmap_kernel() ||
	    !rvm_boot_pmap_lookup(va, &physical, &flags))
		return false;
	if (pa != NULL)
		*pa = physical;
	return true;
}

void
pmap_update(pmap_t pm)
{
	if (pm != pmap_kernel() || rvm_boot_get_cr() != RVM_CR_MMU)
		pmap_fail();
	/* VMEmu walks tables on every access with I/D caches off (CR == 8).
	 * There is no separate TLB in this mode. Revisit before enabling caches.
	 */
	__asm volatile("" ::: "memory");
}

long
pmap_resident_count(pmap_t pm)
{
	if (pm != pmap_kernel())
		pmap_fail();
	return pm->pm_stats.resident_count;
}

long
pmap_wired_count(pmap_t pm)
{
	if (pm != pmap_kernel())
		pmap_fail();
	return pm->pm_stats.wired_count;
}

static void
check_ram_page(paddr_t pa, unsigned long permissions)
{
	paddr_t mapped;
	unsigned long flags;

	if ((pa & RVM_MMU_PAGE_MASK) != 0 || pa >= rvm_bootmem.ram_end ||
	    !rvm_boot_pmap_lookup(pa, &mapped, &flags) || mapped != pa ||
	    (flags & permissions) != permissions)
		pmap_fail();
}

void
pmap_zero_page(paddr_t pa)
{
	volatile uint64_t *dst = (volatile uint64_t *)(uintptr_t)pa;
	unsigned int i;

	check_ram_page(pa, RVM_PTE_WRITE);
	for (i = 0; i < RVM_MMU_PAGE_SIZE / sizeof(*dst); i++)
		dst[i] = 0;
}

void
pmap_copy_page(paddr_t srcpa, paddr_t dstpa)
{
	const volatile uint64_t *src = (const volatile uint64_t *)(uintptr_t)srcpa;
	volatile uint64_t *dst = (volatile uint64_t *)(uintptr_t)dstpa;
	unsigned int i;

	check_ram_page(srcpa, RVM_PTE_READ);
	check_ram_page(dstpa, RVM_PTE_WRITE);
	for (i = 0; i < RVM_MMU_PAGE_SIZE / sizeof(*dst); i++)
		dst[i] = src[i];
}

paddr_t
pmap_phys_address(paddr_t page)
{
	return page << RVM_MMU_PAGE_SHIFT;
}
