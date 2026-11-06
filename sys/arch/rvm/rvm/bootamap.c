/* Checked full-range references and sparse slots around actual MI amaps. */
#include <sys/param.h>
#include <sys/systm.h>
#include <uvm/uvm.h>
#include <machine/bootamap.h>
#include <machine/bootanon.h>
#include <machine/bootheap.h>
#include <machine/psl.h>
#include <machine/uvm_boot.h>

static struct {
	struct vm_amap *map;
	krwlock_t *lock;
	vsize_t size;
	unsigned int entries;
} maps[RVM_BOOTAMAP_MAPS] __attribute__((section(".data"))) = { 0 };
static int initialized __attribute__((section(".data"))) = 0;
static struct vm_amap *releasing __attribute__((section(".data"))) = NULL;
const char rvm_amap_bad[] = "Invalid bootstrap anonymous map operation";

int rvm_mi_amap_start(void);
int rvm_mi_amap_stop(void);
struct vm_amap *rvm_mi_amap_alloc(vaddr_t, vaddr_t, int);
void rvm_mi_amap_add(struct vm_aref *, vaddr_t, struct vm_anon *, bool);
void rvm_mi_amap_unadd(struct vm_aref *, vaddr_t);
struct vm_anon *rvm_mi_amap_lookup(struct vm_aref *, vaddr_t);
void rvm_mi_amap_ref(struct vm_amap *, vaddr_t, vsize_t, int);
void rvm_mi_amap_unref(struct vm_amap *, vaddr_t, vsize_t, bool);

static int
available(void)
{
	return rvm_uvm_boot.initialized && kernel_map != NULL &&
	    rvm_boot_get_cr() == RVM_CR_MMU && curcpu()->ci_cpl == IPL_NONE &&
	    rvm_bootheap_ready();
}

static int
index_of(struct vm_amap *map)
{
	unsigned int i;
	if (!initialized || !available() || map == NULL)
		return -1;
	for (i = 0; i < RVM_BOOTAMAP_MAPS; i++) {
		if (maps[i].map == map)
			return i;
	}
	return -1;
}

krwlock_t *
rvm_bootamap_owner_lock(struct vm_amap *map)
{
	int i = index_of(map);
	return i < 0 || map->am_lock != maps[i].lock ? NULL : maps[i].lock;
}

int
rvm_bootamap_releasing(struct vm_amap *map)
{
	return releasing == map && index_of(map) >= 0;
}

int
rvm_bootamap_init(void)
{
	if (!available())
		return 0;
	if (!initialized) {
		if (!rvm_mi_amap_start())
			return 0;
		initialized = 1;
	}
	return 1;
}

int
rvm_bootamap_fini(void)
{
	unsigned int i;
	if (!initialized || !available() || releasing != NULL)
		return 0;
	for (i = 0; i < RVM_BOOTAMAP_MAPS; i++) {
		if (maps[i].map != NULL)
			return 0;
	}
	if (!rvm_mi_amap_stop())
		return 0;
	initialized = 0;
	return 1;
}

struct vm_amap *
rvm_bootamap_create(vsize_t size)
{
	struct vm_amap *map;
	unsigned int i;
	if (!available() || size == 0 || (size & PAGE_MASK) != 0 ||
	    size / PAGE_SIZE > RVM_BOOTAMAP_SLOTS || releasing != NULL)
		return NULL;
	for (i = 0; i < RVM_BOOTAMAP_MAPS; i++) {
		if (maps[i].map == NULL)
			break;
	}
	if (i == RVM_BOOTAMAP_MAPS || !rvm_bootamap_init())
		return NULL;
	map = rvm_mi_amap_alloc(size, 0, UVM_FLAG_NOWAIT);
	if (map != NULL) {
		maps[i].map = map;
		maps[i].lock = map->am_lock;
		maps[i].size = size;
		maps[i].entries = 0;
	}
	return map;
}

static int
slot_ok(struct vm_amap *map, vsize_t offset)
{
	int i = index_of(map);
	return i >= 0 && (offset & PAGE_MASK) == 0 && offset < maps[i].size &&
	    map->am_nslot == (int)(maps[i].size / PAGE_SIZE) && map->am_ref > 0 &&
	    rvm_bootamap_owner_lock(map) != NULL && !rw_lock_held(map->am_lock);
}

struct vm_anon *
rvm_bootamap_lookup(struct vm_amap *map, vsize_t offset)
{
	struct vm_aref ref;
	struct vm_anon *anon;
	if (!slot_ok(map, offset))
		return NULL;
	ref.ar_amap = map; ref.ar_pageoff = 0;
	rw_enter(map->am_lock, RW_READER);
	anon = rvm_mi_amap_lookup(&ref, offset);
	rw_exit(map->am_lock);
	return anon; /* Borrowed; map ownership is not transferred. */
}

int
rvm_bootamap_put(struct vm_amap *map, vsize_t offset, struct vm_anon *anon)
{
	struct vm_aref ref;
	if (!slot_ok(map, offset) || map->am_ref != 1 || maps[index_of(map)].entries != 0)
		return 0;
	ref.ar_amap = map; ref.ar_pageoff = 0;
	rw_enter(map->am_lock, RW_WRITER);
	if (rvm_mi_amap_lookup(&ref, offset) != NULL ||
	    !rvm_bootanon_transfer(anon, map, 1)) {
		rw_exit(map->am_lock);
		return 0;
	}
	rvm_mi_amap_add(&ref, offset, anon, false);
	rw_exit(map->am_lock);
	return 1;
}

struct vm_anon *
rvm_bootamap_take(struct vm_amap *map, vsize_t offset)
{
	struct vm_aref ref;
	struct vm_anon *anon;
	if (!slot_ok(map, offset) || map->am_ref != 1 || maps[index_of(map)].entries != 0)
		return NULL;
	ref.ar_amap = map; ref.ar_pageoff = 0;
	rw_enter(map->am_lock, RW_WRITER);
	anon = rvm_mi_amap_lookup(&ref, offset);
	if (anon != NULL) {
		rvm_mi_amap_unadd(&ref, offset);
		if (!rvm_bootanon_transfer(anon, map, 0))
			panic(rvm_amap_bad);
	}
	rw_exit(map->am_lock);
	return anon;
}

struct vm_page *
rvm_bootamap_page(struct vm_amap *map, vsize_t offset)
{
	struct vm_aref ref;
	struct vm_anon *anon;
	struct vm_page *pg = NULL;
	if (!slot_ok(map, offset))
		return NULL;
	ref.ar_amap = map; ref.ar_pageoff = 0;
	rw_enter(map->am_lock, RW_WRITER);
	anon = rvm_mi_amap_lookup(&ref, offset);
	if (anon != NULL) {
		pg = anon->an_page;
		if (pg == NULL) {
			pg = uvm_pagealloc_strat(NULL, 0, anon, UVM_PGA_ZERO, 0, 0);
			if (pg != NULL)
				pg->flags &= ~(PG_BUSY | PG_FAKE);
		}
	}
	rw_exit(map->am_lock);
	return pg;
}

int
rvm_bootamap_hold(struct vm_amap *map)
{
	if (!slot_ok(map, 0) || map->am_ref >= 1024)
		return 0;
	rvm_mi_amap_ref(map, 0, map->am_nslot, AMAP_REFALL | AMAP_SHARED);
	return 1;
}

int
rvm_bootamap_drop(struct vm_amap *map)
{
	int i = index_of(map), last;
	if (i < 0 || !slot_ok(map, 0) || releasing != NULL ||
	    map->am_ref <= (int)maps[i].entries)
		return 0;
	last = map->am_ref == 1;
	if (last)
		releasing = map;
	rvm_mi_amap_unref(map, 0, map->am_nslot, true);
	if (last) {
		releasing = NULL;
		maps[i].map = NULL;
		maps[i].lock = NULL;
		maps[i].size = 0;
		maps[i].entries = 0;
	}
	return 1;
}

int
rvm_bootamap_page_freeable(struct vm_amap *map)
{
	int i = index_of(map);
	return i >= 0 && (maps[i].entries == 0 || releasing == map);
}

/* Transfer an existing caller reference to an exact, full-size VM entry. */
int
rvm_bootamap_entry_pin(struct vm_amap *map, vsize_t size)
{
	int i = index_of(map);
	if (i < 0 || !slot_ok(map, 0) || size != maps[i].size ||
	    map->am_ref <= (int)maps[i].entries)
		return 0;
	maps[i].entries++;
	return 1;
}

int
rvm_bootamap_entry_drop(struct vm_amap *map)
{
	int i = index_of(map);
	if (i < 0 || maps[i].entries == 0 || !slot_ok(map, 0))
		return 0;
	maps[i].entries--;
	return rvm_bootamap_drop(map);
}

int
rvm_bootamap_verify(void)
{
	struct vm_amap *map;
	struct vm_anon *a, *b;
	struct vm_page *pg;
	paddr_t pa;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	int64_t dirty = curcpu()->ci_counts[CPU_COUNT_ANONDIRTY];
	unsigned int heap = rvm_bootheap_live();
	map = rvm_bootamap_create(3 * PAGE_SIZE);
	a = uvm_analloc(); b = uvm_analloc();
	if (map == NULL || a == NULL || b == NULL ||
	    !rvm_bootamap_put(map, 0, a) || !rvm_bootamap_put(map, 2 * PAGE_SIZE, b) ||
	    rvm_bootamap_lookup(map, PAGE_SIZE) != NULL ||
	    rvm_bootamap_lookup(map, 2 * PAGE_SIZE) != b)
		return 0;
	pg = rvm_bootamap_page(map, 0);
	if (pg == NULL || !rvm_bootamap_hold(map) || map->am_ref != 2 ||
	    !rvm_bootamap_drop(map) || map->am_ref != 1)
		return 0;
	pmap_kenter_pa(VM_KERNEL_VM_BASE, VM_PAGE_TO_PHYS(pg),
	    VM_PROT_READ | VM_PROT_WRITE, 0);
	pmap_update(pmap_kernel());
	if (rvm_bootamap_take(map, 2 * PAGE_SIZE) != b || !rvm_bootanon_drop(b) ||
	    !rvm_bootamap_drop(map) ||
	    pmap_extract(pmap_kernel(), VM_KERNEL_VM_BASE, &pa) ||
	    !rvm_bootamap_fini() || !rvm_bootanon_fini())
		return 0;
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before &&
	    curcpu()->ci_counts[CPU_COUNT_ANONDIRTY] == dirty &&
	    rvm_bootheap_live() == heap;
}
