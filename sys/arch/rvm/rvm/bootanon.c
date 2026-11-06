/* Checked, resident-only anonymous descriptors backed by MI UVM and caches. */
#include <sys/param.h>
#include <sys/systm.h>
#include <uvm/uvm.h>
#include <machine/bootanon.h>
#include <machine/bootamap.h>
#include <machine/psl.h>
#include <machine/uvm_boot.h>

struct anon_record {
	struct vm_anon *anon;
	krwlock_t lock;
	struct vm_amap *owner;
};
static struct anon_record records[RVM_BOOTANON_ANONS]
    __attribute__((section(".data"))) = { 0 };
static int initialized __attribute__((section(".data"))) = 0;
static struct vm_anon *releasing __attribute__((section(".data"))) = NULL;
const char rvm_anon_bad[] = "Invalid bootstrap anonymous page operation";

int rvm_mi_anon_start(void);
int rvm_mi_anon_stop(void);
struct vm_anon *rvm_mi_analloc(void);
void rvm_mi_anfree(struct vm_anon *);

static int
available(void)
{
	return rvm_uvm_boot.initialized && kernel_map != NULL &&
	    rvm_boot_get_cr() == RVM_CR_MMU && curcpu()->ci_cpl == IPL_NONE;
}

static struct anon_record *
lookup(struct vm_anon *anon)
{
	unsigned int i;
	if (!initialized || !available() || anon == NULL)
		return NULL;
	for (i = 0; i < RVM_BOOTANON_ANONS; i++) {
		if (records[i].anon == anon)
			return &records[i];
	}
	return NULL;
}

int
rvm_bootanon_valid(struct vm_anon *anon)
{
	return lookup(anon) != NULL;
}

static krwlock_t *
owner_lock(struct anon_record *r)
{
	return r->owner == NULL ? &r->lock : rvm_bootamap_owner_lock(r->owner);
}

int
rvm_bootanon_transfer(struct vm_anon *anon, struct vm_amap *amap, int attach)
{
	struct anon_record *r = lookup(anon);
	krwlock_t *lock = rvm_bootamap_owner_lock(amap);
	if (r == NULL || lock == NULL || !rw_write_held(lock) ||
	    anon->an_ref != 1 || anon->an_swslot != 0 || rw_lock_held(&r->lock))
		return 0;
	if (attach) {
		if (r->owner != NULL || (anon->an_lock != NULL && anon->an_lock != &r->lock))
			return 0;
		r->owner = amap;
		anon->an_lock = lock;
	} else {
		if (r->owner != amap || anon->an_lock != lock)
			return 0;
		r->owner = NULL;
		anon->an_lock = &r->lock;
	}
	return 1;
}

int
rvm_bootanon_canalloc(struct vm_anon *anon)
{
	struct anon_record *r = lookup(anon);
	return r != NULL && anon->an_ref != 0 && anon->an_swslot == 0 &&
	    anon->an_page == NULL && owner_lock(r) != NULL &&
	    anon->an_lock == owner_lock(r) && rw_write_held(anon->an_lock);
}

int
rvm_bootanon_page_valid(struct vm_page *pg)
{
	struct anon_record *r;
	if (pg == NULL || (r = lookup(pg->uanon)) == NULL)
		return 0;
	return (r->owner == NULL || rvm_bootamap_page_freeable(r->owner)) &&
	    r->anon->an_page == pg && r->anon->an_swslot == 0 &&
	    owner_lock(r) != NULL && r->anon->an_lock == owner_lock(r) &&
	    rw_write_held(r->anon->an_lock) &&
	    pg->uobject == NULL && pg->loan_count == 0 && pg->wire_count == 0 &&
	    (pg->flags & (PG_FREE | PG_ANON | PG_RELEASED | PG_PAGEOUT)) == PG_ANON &&
	    (pg->pqflags & (PQ_WANTED | PQ_INTENT_SET | PQ_INTENT_QUEUED | PQ_PRIVATE)) == 0;
}

int
rvm_bootanon_init(void)
{
	if (!available())
		return 0;
	if (!initialized) {
		if (!rvm_mi_anon_start())
			return 0;
		initialized = 1;
	}
	return 1;
}

void
uvm_anon_init(void)
{
	if (!rvm_bootanon_init())
		panic(rvm_anon_bad);
}

int
rvm_bootanon_fini(void)
{
	unsigned int i;
	if (!initialized || !available() || releasing != NULL)
		return 0;
	for (i = 0; i < RVM_BOOTANON_ANONS; i++) {
		if (records[i].anon != NULL)
			return 0;
	}
	if (!rvm_mi_anon_stop())
		return 0;
	initialized = 0;
	return 1;
}

struct vm_anon *
uvm_analloc(void)
{
	struct vm_anon *anon;
	unsigned int i;
	if (!available() || releasing != NULL)
		return NULL;
	for (i = 0; i < RVM_BOOTANON_ANONS; i++) {
		if (records[i].anon == NULL)
			break;
	}
	if (i == RVM_BOOTANON_ANONS || !rvm_bootanon_init())
		return NULL;
	anon = rvm_mi_analloc();
	if (anon != NULL) {
		if (anon->an_ref != 1 || anon->an_lock != NULL || anon->an_page != NULL)
			panic(rvm_anon_bad);
		/* The no-swap MI constructor deliberately ignores this ABI field. */
		anon->an_swslot = 0;
		rw_init(&records[i].lock);
		records[i].anon = anon;
		records[i].owner = NULL;
	}
	return anon;
}

void
uvm_anfree(struct vm_anon *anon)
{
	struct anon_record *r = lookup(anon);
	if (r == NULL || anon->an_ref != 0 || anon->an_swslot != 0 ||
	    (anon->an_lock != NULL && (owner_lock(r) == NULL ||
	    anon->an_lock != owner_lock(r) || !rw_write_held(anon->an_lock) ||
	    (r->owner == NULL ? releasing != anon : !rvm_bootamap_releasing(r->owner)))) ||
	    (anon->an_page != NULL && (!rvm_bootanon_page_valid(anon->an_page) ||
	    (anon->an_page->flags & (PG_BUSY | PG_FAKE)) != 0)))
		panic(rvm_anon_bad);
	rvm_mi_anfree(anon);
	/* Do not inspect an object after MI has returned it to its cache. */
	r->anon = NULL;
	r->owner = NULL;
}

static struct anon_record *
bind_lock(struct vm_anon *anon)
{
	struct anon_record *r = lookup(anon);
	if (r == NULL || r->owner != NULL || anon->an_ref == 0 || anon->an_swslot != 0 ||
	    (anon->an_lock != NULL && anon->an_lock != &r->lock) ||
	    rw_lock_held(&r->lock))
		return NULL;
	anon->an_lock = &r->lock;
	return r;
}

struct vm_page *
rvm_bootanon_alloc(struct vm_anon *anon)
{
	struct anon_record *r = bind_lock(anon);
	struct vm_page *pg;
	if (r == NULL || anon->an_page != NULL)
		return NULL;
	rw_enter(&r->lock, RW_WRITER);
	pg = uvm_pagealloc_strat(NULL, 0, anon, UVM_PGA_ZERO,
	    UVM_PGA_STRAT_NORMAL, VM_FREELIST_DEFAULT);
	if (pg != NULL)
		pg->flags &= ~(PG_BUSY | PG_FAKE);
	rw_exit(&r->lock);
	return pg;
}

int
rvm_bootanon_hold(struct vm_anon *anon)
{
	struct anon_record *r = bind_lock(anon);
	if (r == NULL || anon->an_ref == (uintptr_t)-1)
		return 0;
	rw_enter(&r->lock, RW_WRITER);
	anon->an_ref++;
	rw_exit(&r->lock);
	return 1;
}

int
rvm_bootanon_drop(struct vm_anon *anon)
{
	struct anon_record *r = bind_lock(anon);
	if (r == NULL || releasing != NULL)
		return 0;
	rw_enter(&r->lock, RW_WRITER);
	if (--anon->an_ref == 0) {
		releasing = anon;
		uvm_anfree(anon);
		releasing = NULL;
	}
	rw_exit(&r->lock);
	return 1;
}

void
rvm_bootanon_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	vaddr_t va;
	paddr_t pa, target;
	if (prot != VM_PROT_NONE || !rvm_bootanon_page_valid(pg))
		panic(rvm_anon_bad);
	target = VM_PAGE_TO_PHYS(pg);
	/* All mutable mappings currently live in this preallocated kernel window.
	 * Leave the permanent supervisor identity map in place, as for raw pages.
	 * User mappings and generic PV tracking are not implemented yet.
	 */
	for (va = VM_KERNEL_VM_BASE; va < VM_KERNEL_VM_BASE + VM_KERNEL_VM_SIZE;
	    va += PAGE_SIZE) {
		if (pmap_extract(pmap_kernel(), va, &pa) && pa == target)
			pmap_kremove(va, PAGE_SIZE);
	}
	pmap_update(pmap_kernel());
}

int
rvm_bootanon_verify(void)
{
	struct vm_anon *anon;
	struct vm_page *pg;
	paddr_t pa;
	volatile uint64_t *data;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	int64_t dirty = curcpu()->ci_counts[CPU_COUNT_ANONDIRTY];
	long resident = pmap_resident_count(pmap_kernel());
	long wired = pmap_wired_count(pmap_kernel());
	anon = uvm_analloc();
	if (anon == NULL || anon->an_ref != 1 || anon->an_lock != NULL)
		return 0;
	pg = rvm_bootanon_alloc(anon);
	if (pg == NULL || anon->an_page != pg || pg->uanon != anon ||
	    uvm_pagegetdirty(pg) != UVM_PAGE_STATUS_DIRTY ||
	    curcpu()->ci_counts[CPU_COUNT_ANONDIRTY] != dirty + 1 ||
	    !rvm_bootanon_hold(anon) || anon->an_ref != 2)
		return 0;
	pmap_kenter_pa(VM_KERNEL_VM_BASE, VM_PAGE_TO_PHYS(pg),
	    VM_PROT_READ | VM_PROT_WRITE, 0);
	pmap_update(pmap_kernel());
	data = (volatile uint64_t *)VM_KERNEL_VM_BASE;
	if (data[0] != 0 || data[PAGE_SIZE / sizeof(*data) - 1] != 0)
		return 0;
	data[0] = 0x52564d414e4f4e31UL;
	if (!rvm_bootanon_drop(anon) || anon->an_ref != 1 ||
	    data[0] != 0x52564d414e4f4e31UL || !rvm_bootanon_drop(anon) ||
	    pmap_extract(pmap_kernel(), VM_KERNEL_VM_BASE, &pa) ||
	    !rvm_bootanon_fini())
		return 0;
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before &&
	    curcpu()->ci_counts[CPU_COUNT_ANONDIRTY] == dirty &&
	    pmap_resident_count(pmap_kernel()) == resident &&
	    pmap_wired_count(pmap_kernel()) == wired && curlwp->l_nopreempt == 0;
}
