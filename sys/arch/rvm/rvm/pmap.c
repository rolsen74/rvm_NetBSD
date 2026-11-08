/*
 * Supervisor pmap for the RVM six-level hardware walker. The initial wired
 * kernel window has preallocated tables: editing it never steals UVM memory.
 * Single CPU with IRQ-guarded transactions. Normal-kernel user translations
 * live in kernel_pmap.c. Optional I-cache covers immutable kernel text only.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#ifdef RVM_KERNEL
#include <sys/proc.h>
#endif
#include <uvm/uvm.h>
#include <machine/asyncirq.h>
#include <machine/boot.h>
#include <machine/bootmem.h>
#include <machine/generic.h>
#include <machine/mmu.h>
#include <machine/pmap.h>
#include <machine/pmap_private.h>
#include <machine/psl.h>
#include <machine/cache.h>
#include <machine/vmparam.h>

struct rvm_boot_pmap rvm_boot_pmap __attribute__((section(".data"))) = { 0 };
struct pmap kernel_pmap_store __attribute__((section(".data"))) = { 0 };
/* kernel_pmap_ptr is emitted by locore: GCC cannot yet emit pointer constants. */
const char rvm_msg_pmap_error[] = "Invalid or unsupported kernel pmap operation.\n";

/* The preallocated kernel VM window supplies one PV slot per possible VA.
 * Entries also remember wiring for unmanaged mappings. No dynamic allocation
 * or bootstrap-memory use is needed after the UVM handoff. */
#ifndef RVM_KERNEL
__CTASSERT(sizeof(struct rvm_pv_entry) == 32);
#endif
#define RVM_PV_SLOTS (VM_KERNEL_VM_SIZE / PAGE_SIZE)
static struct rvm_pv_entry pv_entries[RVM_PV_SLOTS]
	__attribute__((section(".data"))) = { 0 };
#define RVM_PTE_ACCESS (RVM_PTE_USED_READ | RVM_PTE_USED_WRITE)

static void __dead
pmap_fail(void)
{
	rvm_early_puts(rvm_msg_pmap_error);
	rvm_boot_stop();
}

static unsigned int pmap_guard(void)
{
	if (curlwp != NULL) return rvm_intr_suspend();
	rvm_intr_disable();
	if ((rvm_boot_get_cr() & ~RVM_CR_INSTCACHE) != RVM_CR_MMU) pmap_fail();
	return UINT_MAX;
}
static void pmap_unguard(unsigned int guard)
{
	if (guard != UINT_MAX) rvm_intr_resume(guard);
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
#ifdef RVM_KERNEL_ICACHE
		/* Only the fixed loader text is cached. User mappings and dynamic
		 * kernel mappings stay uncached; the data-cache CR bit stays clear. */
		if ((flags & (RVM_PTE_EXEC | RVM_PTE_WRITE)) == RVM_PTE_EXEC)
			flags |= RVM_PTE_CACHE_WRITETHROUGH;
#endif
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

static struct rvm_pv_entry *pv_slot(vaddr_t va)
{
	return &pv_entries[(va - VM_KERNEL_VM_BASE) >> PAGE_SHIFT];
}
static vaddr_t pv_address(struct rvm_pv_entry *pv)
{
#ifdef RVM_KERNEL
	if (pv->pmap != NULL) return pv->va;
#endif
	return VM_KERNEL_VM_BASE + (pv - pv_entries) * PAGE_SIZE;
}
static pmap_t pv_pmap(struct rvm_pv_entry *pv)
{
#ifdef RVM_KERNEL
	if (pv->pmap != NULL) return pv->pmap;
#endif
	return pmap_kernel();
}
static volatile uint64_t *pv_pte(struct rvm_pv_entry *pv)
{
#ifdef RVM_KERNEL
	if (pv->pmap != NULL) return rvm_user_pte(pv->pmap, pv->va);
#endif
	return pte_lookup(pv_address(pv), 0);
}
static void pv_detach(vaddr_t va, uint64_t oldpte)
{
	struct rvm_pv_entry *pv = pv_slot(va);
	struct vm_page *pg = pv->page;
	if (pg != NULL) {
		pg->mdpage.attrs |= oldpte & RVM_PTE_ACCESS;
		if (oldpte & RVM_PTE_USED_WRITE) pg->mdpage.attrs |= RVM_PTE_USED_READ;
		if (pv->previous != NULL) {
			if (pv->previous->next != pv) pmap_fail();
			pv->previous->next = pv->next;
		} else {
			if (pg->mdpage.pv_head != pv) pmap_fail();
			pg->mdpage.pv_head = pv->next;
		}
		if (pv->next != NULL) pv->next->previous = pv->previous;
	}
	pv->page = NULL; pv->next = pv->previous = NULL;
}
static void pv_attach(vaddr_t va, paddr_t pa)
{
	struct rvm_pv_entry *pv = pv_slot(va);
	struct vm_page *pg = uvm.page_init_done ? PHYS_TO_VM_PAGE(pa) : NULL;
	if (pg != NULL) {
		if (pg->flags & PG_FREE) pmap_fail();
		pv->page = pg; pv->next = pg->mdpage.pv_head; pv->previous = NULL;
		if (pv->next != NULL) pv->next->previous = pv;
		pg->mdpage.pv_head = pv;
	}
}
/* Caller has IRQs masked across the PTE/PV/statistics transaction. */
static void enter_kernel(vaddr_t va, paddr_t pa, unsigned long permissions,
	unsigned int wired)
{
	volatile uint64_t *pte = pte_lookup(va, 0);
	struct rvm_pv_entry *pv = pv_slot(va);
	if (pte == NULL) pmap_fail();
	if (*pte & RVM_PTE_VALID) {
		if (pv->wired) kernel_pmap_store.pm_stats.wired_count--;
		pv_detach(va, *pte);
	} else kernel_pmap_store.pm_stats.resident_count++;
	pv->wired = wired;
	if (wired) kernel_pmap_store.pm_stats.wired_count++;
	pv_attach(va, pa);
	*pte = pa | permissions | RVM_PTE_VALID | RVM_PTE_SUPERVISOR;
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
	unsigned int guard = pmap_guard();
	unsigned long permissions = pte_permissions(prot);
	check_kernel_range(va, PAGE_SIZE);
	if ((pa & PAGE_MASK) || permissions == 0 ||
		(flags != 0 && flags != PMAP_NOCACHE)) pmap_fail();
	enter_kernel(va, pa, permissions, 1);
	pmap_unguard(guard);
}

int
pmap_enter(pmap_t pm, vaddr_t va, paddr_t pa, vm_prot_t prot, u_int flags)
{
#ifdef RVM_KERNEL
	if (pm != pmap_kernel()) return rvm_user_enter(pm, va, pa, prot, flags);
#endif
	unsigned int guard = pmap_guard();
	unsigned long permissions = pte_permissions(prot);
	struct vm_page *pg;
	if (pm != pmap_kernel()) pmap_fail();
	check_kernel_range(va, PAGE_SIZE);
	if ((pa & PAGE_MASK) || permissions == 0 ||
		(flags & ~(PMAP_WIRED | PMAP_CANFAIL | PMAP_NOCACHE | VM_PROT_ALL)) ||
		((flags & VM_PROT_ALL) & ~prot)) pmap_fail();
	pg = uvm.page_init_done ? PHYS_TO_VM_PAGE(pa) : NULL;
	if (pg == NULL || (pg->flags & PG_FREE)) pmap_fail();
	enter_kernel(va, pa, permissions, (flags & PMAP_WIRED) != 0);
	if (flags & (VM_PROT_READ | VM_PROT_EXECUTE)) pg->mdpage.attrs |= RVM_PTE_USED_READ;
	if (flags & VM_PROT_WRITE) pg->mdpage.attrs |= RVM_PTE_ACCESS;
	pmap_unguard(guard);
	return 0;
}

void
pmap_unwire(pmap_t pm, vaddr_t va)
{
#ifdef RVM_KERNEL
	if (pm != pmap_kernel()) { rvm_user_unwire(pm, va); return; }
#endif
	unsigned int guard = pmap_guard();
	volatile uint64_t *pte;
	if (pm != pmap_kernel()) pmap_fail();
	check_kernel_range(va, PAGE_SIZE); pte = pte_lookup(va, 0);
	if (pte == NULL || !(*pte & RVM_PTE_VALID) || !pv_slot(va)->wired) pmap_fail();
	pv_slot(va)->wired = 0; kernel_pmap_store.pm_stats.wired_count--;
	pmap_unguard(guard);
}

void
pmap_kremove(vaddr_t va, vsize_t size)
{
	unsigned int guard = pmap_guard();
	volatile uint64_t *pte;
	vaddr_t end;
	check_kernel_range(va, size); end = va + size;
	for (; va < end; va += PAGE_SIZE) {
		pte = pte_lookup(va, 0);
		if (pte != NULL && (*pte & RVM_PTE_VALID)) {
			if (pv_slot(va)->wired) kernel_pmap_store.pm_stats.wired_count--;
			pv_detach(va, *pte); pv_slot(va)->wired = 0;
			*pte = 0; kernel_pmap_store.pm_stats.resident_count--;
		}
	}
	pmap_unguard(guard);
}

void
pmap_remove(pmap_t pm, vaddr_t start, vaddr_t end)
{
#ifdef RVM_KERNEL
	if (pm != pmap_kernel()) { rvm_user_remove(pm, start, end); return; }
#endif
	if (pm != pmap_kernel() || end < start)
		pmap_fail();
	pmap_kremove(start, end - start);
}

void
pmap_protect(pmap_t pm, vaddr_t start, vaddr_t end, vm_prot_t prot)
{
#ifdef RVM_KERNEL
	if (pm != pmap_kernel()) { rvm_user_protect(pm, start, end, prot); return; }
#endif
	unsigned int guard = pmap_guard();
	volatile uint64_t *pte;
	unsigned long keep = pte_permissions(prot);
	unsigned long mask = RVM_PTE_READ | RVM_PTE_WRITE | RVM_PTE_EXEC;

	if (pm != pmap_kernel() || end < start)
		pmap_fail();
	check_kernel_range(start, end - start);
	if (prot == VM_PROT_NONE) {
		pmap_remove(pm, start, end);
		pmap_unguard(guard); return;
	}
	for (; start < end; start += RVM_MMU_PAGE_SIZE) {
		pte = pte_lookup(start, 0);
		if (pte != NULL && (*pte & RVM_PTE_VALID) != 0)
			*pte &= ~mask | keep; /* Protection may only remove permissions. */
	}
	pmap_unguard(guard);
}

bool
pmap_extract(pmap_t pm, vaddr_t va, paddr_t *pa)
{
	paddr_t physical;
	unsigned long flags;
#ifdef RVM_KERNEL
	if (pm != pmap_kernel()) {
		volatile uint64_t *pte = rvm_user_pte(pm, va);
		if (pte == NULL || !(*pte & RVM_PTE_VALID)) return false;
		if (pa != NULL) *pa = (*pte & ~PAGE_MASK) | (va & PAGE_MASK);
		return true;
	}
#endif

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
	unsigned int guard = pmap_guard();
	if ((rvm_boot_get_cr() & ~RVM_CR_INSTCACHE) != RVM_CR_MMU)
		pmap_fail();
#ifdef RVM_KERNEL
	if (curlwp != NULL && curlwp->l_proc->p_vmspace->vm_map.pmap == pm &&
	    pm != pmap_kernel()) rvm_set_user_root(pm->pm_root);
#else
	if (pm != pmap_kernel()) pmap_fail();
#endif
	/* User execution always has I/D caches off. Optional supervisor I-cache
	 * fetches only fixed, immutable kernel text in the shared identity map.
	 * Dynamic executable kernel mappings require real I-cache invalidation
	 * before they can be used with this experiment.
	 */
	__asm volatile("" ::: "memory");
	pmap_unguard(guard);
}

long
pmap_resident_count(pmap_t pm)
{
#ifndef RVM_KERNEL
	if (pm != pmap_kernel())
		pmap_fail();
#endif
	return pm->pm_stats.resident_count;
}

long
pmap_wired_count(pmap_t pm)
{
#ifndef RVM_KERNEL
	if (pm != pmap_kernel())
		pmap_fail();
#endif
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

/* Access history includes the bootstrap direct map as well as every tracked
 * alias. Removing/replacing a mapping harvests its bits into mdpage.attrs. */
static unsigned long page_access(struct vm_page *pg, unsigned long clear)
{
	volatile uint64_t *pte;
	struct rvm_pv_entry *pv;
	unsigned long attrs;
	paddr_t pa;
	if (!uvm.page_init_done || pg == NULL ||
		PHYS_TO_VM_PAGE(VM_PAGE_TO_PHYS(pg)) != pg || (pg->flags & PG_FREE)) pmap_fail();
	pa = VM_PAGE_TO_PHYS(pg); attrs = pg->mdpage.attrs;
	pte = pte_lookup(pa, 0);
	if (pte == NULL || (*pte & ~PAGE_MASK) != pa || !(*pte & RVM_PTE_VALID)) pmap_fail();
	attrs |= *pte & RVM_PTE_ACCESS;
	if (*pte & RVM_PTE_USED_WRITE) attrs |= RVM_PTE_USED_READ;
	if (clear) *pte &= ~clear;
	for (pv = pg->mdpage.pv_head; pv != NULL; pv = pv->next) {
		pte = pv_pte(pv);
		if (pv->page != pg || pte == NULL || !(*pte & RVM_PTE_VALID) ||
			(*pte & ~PAGE_MASK) != pa) pmap_fail();
		attrs |= *pte & RVM_PTE_ACCESS;
		if (*pte & RVM_PTE_USED_WRITE) attrs |= RVM_PTE_USED_READ;
		if (clear) *pte &= ~clear;
	}
	pg->mdpage.attrs = attrs & ~clear;
	return attrs;
}
bool pmap_is_modified(struct vm_page *pg)
{
	unsigned int g = pmap_guard();
	bool result = (page_access(pg, 0) & RVM_PTE_USED_WRITE) != 0;
	pmap_unguard(g); return result;
}
bool pmap_is_referenced(struct vm_page *pg)
{
	unsigned int g = pmap_guard();
	bool result = (page_access(pg, 0) & RVM_PTE_USED_READ) != 0;
	pmap_unguard(g); return result;
}
bool pmap_clear_modify(struct vm_page *pg)
{
	unsigned int g = pmap_guard();
	bool result = (page_access(pg, RVM_PTE_USED_WRITE) & RVM_PTE_USED_WRITE) != 0;
	pmap_unguard(g); return result;
}
bool pmap_clear_reference(struct vm_page *pg)
{
	unsigned int g = pmap_guard();
	/* Writes imply references, so both hardware access bits must be cleared.
	   Retain modification history independently before clearing write bits. */
	unsigned long attrs = page_access(pg, RVM_PTE_ACCESS);
	pg->mdpage.attrs |= attrs & RVM_PTE_USED_WRITE;
	pmap_unguard(g); return (attrs & RVM_PTE_USED_READ) != 0;
}
void pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	unsigned int g = pmap_guard();
	struct rvm_pv_entry *pv;
	if (prot & ~VM_PROT_ALL) pmap_fail();
	page_access(pg, 0);
	for (pv = pg->mdpage.pv_head; pv != NULL; ) {
		struct rvm_pv_entry *next = pv->next;
		vaddr_t va = pv_address(pv);
		pmap_protect(pv_pmap(pv), va, va + PAGE_SIZE, prot);
		pv = next;
	}
	pmap_unguard(g);
}

void pmap_init(void)
{
	unsigned int g = pmap_guard();
	if (!uvm.page_init_done || kernel_pmap_store.pm_root == 0) pmap_fail();
	pmap_unguard(g);
}
