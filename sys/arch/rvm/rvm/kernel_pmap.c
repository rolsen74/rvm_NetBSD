/* User translations for the uncached, single-CPU RVM MMU.
 * Page tables use UVM-owned pages, accessed through the supervisor direct map.
 * Each table occupies the first 2 KiB; ownership metadata follows it. Tables
 * are retained until pmap destruction, including after allocation failure.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/atomic.h>
#include <sys/proc.h>
#include <uvm/uvm.h>
#include <machine/pmap.h>
#include <machine/pmap_private.h>
#include <machine/mmu.h>
#include <machine/asyncirq.h>

struct user_table {
	struct user_table *next;
	struct vm_page *page;
};
#define ACCESS (RVM_PTE_USED_READ | RVM_PTE_USED_WRITE)

static void
check_range(vaddr_t start, vaddr_t end)
{
	KASSERT(start >= VM_MIN_ADDRESS && end <= VM_MAXUSER_ADDRESS);
	KASSERT(end >= start && ((start | end) & PAGE_MASK) == 0);
}

static paddr_t
alloc_table(pmap_t pm)
{
	struct vm_page *pg = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_ZERO);
	struct user_table *table;
	paddr_t pa;
	if (pg == NULL) return 0;
	/* These pages belong solely to this pmap, not to an object/anon. */
	pg->flags &= ~PG_BUSY;
	UVM_PAGE_OWN(pg, NULL);
	pa = VM_PAGE_TO_PHYS(pg);
	table = (void *)(uintptr_t)(pa + RVM_MMU_TABLE_SIZE);
	table->page = pg;
	table->next = pm->pm_tables;
	pm->pm_tables = table;
	return pa;
}

static volatile uint64_t *
lookup(pmap_t pm, vaddr_t va, bool create)
{
	volatile uint64_t *table;
	paddr_t pa;
	if (pm->pm_root == 0) {
		if (!create || (pm->pm_root = alloc_table(pm)) == 0) return NULL;
	}
	table = (void *)(uintptr_t)pm->pm_root;
	for (unsigned int shift = 56; shift > PAGE_SHIFT; shift -= 8) {
		unsigned int index = (va >> shift) & 255;
		if (!(table[index] & RVM_PTE_VALID)) {
			if (!create || (pa = alloc_table(pm)) == 0) return NULL;
			table[index] = pa | RVM_PTE_VALID;
		}
		table = (void *)(uintptr_t)(table[index] & ~RVM_MMU_TABLE_MASK);
	}
	return &table[(va >> PAGE_SHIFT) & 255];
}

volatile uint64_t *
rvm_user_pte(pmap_t pm, vaddr_t va)
{
	if (va < VM_MIN_ADDRESS || va >= VM_MAXUSER_ADDRESS) return NULL;
	return lookup(pm, va, false);
}

static struct rvm_pv_entry **
find_mapping(pmap_t pm, vaddr_t va)
{
	struct rvm_pv_entry **p;
	for (p = &pm->pm_mappings; *p != NULL; p = &(*p)->pm_next)
		if ((*p)->va == va) break;
	return p;
}

static void
unlink_page(struct rvm_pv_entry *pv, uint64_t pte)
{
	struct vm_page *pg = pv->page;
	pg->mdpage.attrs |= pte & ACCESS;
	if (pte & RVM_PTE_USED_WRITE) pg->mdpage.attrs |= RVM_PTE_USED_READ;
	if (pv->previous != NULL) pv->previous->next = pv->next;
	else pg->mdpage.pv_head = pv->next;
	if (pv->next != NULL) pv->next->previous = pv->previous;
}

int
rvm_user_enter(pmap_t pm, vaddr_t va, paddr_t pa, vm_prot_t prot, u_int flags)
{
	struct rvm_pv_entry *pv, **link;
	struct vm_page *pg;
	volatile uint64_t *pte;
	unsigned long bits = RVM_PTE_VALID;
	unsigned int guard;
	check_range(va, va + PAGE_SIZE);
	KASSERT((pa & PAGE_MASK) == 0 && prot != VM_PROT_NONE);
	KASSERT((prot & ~VM_PROT_ALL) == 0);
	KASSERT((flags & ~(PMAP_WIRED | PMAP_CANFAIL | PMAP_NOCACHE | VM_PROT_ALL)) == 0);
	KASSERT(((flags & VM_PROT_ALL) & ~prot) == 0);
	pg = PHYS_TO_VM_PAGE(pa);
	KASSERT(pg != NULL && !(pg->flags & PG_FREE));
	guard = rvm_intr_suspend();
	link = find_mapping(pm, va);
	pv = *link;
	if (pv == NULL) pv = kmem_zalloc(sizeof(*pv), KM_NOSLEEP);
	pte = pv != NULL ? lookup(pm, va, true) : NULL;
	if (pte == NULL) {
		if (*link == NULL && pv != NULL) kmem_free(pv, sizeof(*pv));
		rvm_intr_resume(guard);
		if (!(flags & PMAP_CANFAIL)) panic("RVM user pmap: out of memory");
		return ENOMEM;
	}
	if (*link != NULL) {
		unlink_page(pv, *pte);
		if (pv->wired) pm->pm_stats.wired_count--;
	} else {
		pv->pm_next = pm->pm_mappings;
		pm->pm_mappings = pv;
		pm->pm_stats.resident_count++;
	}
	pv->pmap = pm; pv->va = va; pv->page = pg;
	pv->wired = (flags & PMAP_WIRED) != 0;
	if (pv->wired) pm->pm_stats.wired_count++;
	pv->next = pg->mdpage.pv_head; pv->previous = NULL;
	if (pv->next != NULL) pv->next->previous = pv;
	pg->mdpage.pv_head = pv;
	if (prot & VM_PROT_READ) bits |= RVM_PTE_READ;
	if (prot & VM_PROT_WRITE) bits |= RVM_PTE_WRITE;
	if (prot & VM_PROT_EXECUTE) bits |= RVM_PTE_EXEC;
	if (flags & (VM_PROT_READ | VM_PROT_EXECUTE)) pg->mdpage.attrs |= RVM_PTE_USED_READ;
	if (flags & VM_PROT_WRITE) pg->mdpage.attrs |= ACCESS;
	*pte = pa | bits;
	rvm_intr_resume(guard);
	return 0;
}

static void
remove_mapping(pmap_t pm, struct rvm_pv_entry **link)
{
	struct rvm_pv_entry *pv = *link;
	volatile uint64_t *pte = lookup(pm, pv->va, false);
	KASSERT(pte != NULL && (*pte & RVM_PTE_VALID));
	unlink_page(pv, *pte);
	*pte = 0;
	pm->pm_stats.resident_count--;
	if (pv->wired) pm->pm_stats.wired_count--;
	*link = pv->pm_next;
	kmem_free(pv, sizeof(*pv));
}

void
rvm_user_remove(pmap_t pm, vaddr_t start, vaddr_t end)
{
	unsigned int guard;
	struct rvm_pv_entry **link;
	check_range(start, end);
	guard = rvm_intr_suspend();
	for (link = &pm->pm_mappings; *link != NULL; ) {
		if ((*link)->va >= start && (*link)->va < end) remove_mapping(pm, link);
		else link = &(*link)->pm_next;
	}
	rvm_intr_resume(guard);
}

void
rvm_user_protect(pmap_t pm, vaddr_t start, vaddr_t end, vm_prot_t prot)
{
	unsigned int guard;
	unsigned long keep = 0;
	struct rvm_pv_entry *pv;
	check_range(start, end);
	KASSERT((prot & ~VM_PROT_ALL) == 0);
	if (prot == VM_PROT_NONE) { rvm_user_remove(pm, start, end); return; }
	if (prot & VM_PROT_READ) keep |= RVM_PTE_READ;
	if (prot & VM_PROT_WRITE) keep |= RVM_PTE_WRITE;
	if (prot & VM_PROT_EXECUTE) keep |= RVM_PTE_EXEC;
	guard = rvm_intr_suspend();
	for (pv = pm->pm_mappings; pv != NULL; pv = pv->pm_next) {
		if (pv->va >= start && pv->va < end) {
			volatile uint64_t *pte = lookup(pm, pv->va, false);
			*pte &= ~(RVM_PTE_READ | RVM_PTE_WRITE | RVM_PTE_EXEC) | keep;
		}
	}
	rvm_intr_resume(guard);
}

void
rvm_user_unwire(pmap_t pm, vaddr_t va)
{
	unsigned int guard = rvm_intr_suspend();
	struct rvm_pv_entry *pv = *find_mapping(pm, va);
	KASSERT(pv != NULL && pv->wired);
	pv->wired = 0;
	pm->pm_stats.wired_count--;
	rvm_intr_resume(guard);
}

struct pmap *
pmap_create(void)
{
	struct pmap *pm = kmem_zalloc(sizeof(*pm), KM_SLEEP);
	pm->pm_refs = 1;
	return pm;
}
void pmap_reference(pmap_t pm)
{
	if (pm != pmap_kernel()) atomic_inc_uint(&pm->pm_refs);
}
void
pmap_destroy(pmap_t pm)
{
	struct user_table *table, *next;
	KASSERT(pm != pmap_kernel() && pm->pm_refs != 0);
	if (atomic_dec_uint_nv(&pm->pm_refs) != 0) return;
	KASSERT(pm->pm_mappings == NULL && pm->pm_stats.resident_count == 0);
	for (table = pm->pm_tables; table != NULL; table = next) {
		next = table->next;
		uvm_pagefree(table->page);
	}
	kmem_free(pm, sizeof(*pm));
}
void
pmap_activate(struct lwp *l)
{
	pmap_t pm = l->l_proc->p_vmspace->vm_map.pmap;
	if (l == curlwp) rvm_set_user_root(pm == pmap_kernel() ? 0 : pm->pm_root);
}
void pmap_deactivate(struct lwp *l __unused) { }
bool
pmap_remove_all(pmap_t pm)
{
	unsigned int guard = rvm_intr_suspend();
	KASSERT(pm != pmap_kernel());
	while (pm->pm_mappings != NULL) remove_mapping(pm, &pm->pm_mappings);
	rvm_intr_resume(guard);
	return false;
}
/* Advisory eager copying is optional; UVM faults populate the new address space. */
void
pmap_copy(pmap_t dst __unused, pmap_t src __unused, vaddr_t daddr __unused,
    vsize_t len __unused, vaddr_t saddr __unused)
{
}
