/* Standalone kernel VA reservations. Full kernels use normal UVM startup. */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/pool.h>
#include <sys/systm.h>
#include <uvm/uvm.h>
#include <machine/bootalloc.h>
#include <machine/bootmap.h>
#include <machine/bootobject.h>
#include <machine/bootamap.h>
#include <machine/psl.h>
#include <machine/uvm_boot.h>

static struct vm_map map_store __attribute__((section(".data"))) = { 0 };
struct vm_map *pager_map __attribute__((section(".data"))) = NULL;
kmutex_t uvm_kentry_lock __attribute__((section(".data"))) = { 0 };
static struct vm_map_entry entries[RVM_BOOTALLOC_SLOTS]
    __attribute__((section(".data"))) = { 0 };
static unsigned char used[RVM_BOOTALLOC_SLOTS]
    __attribute__((section(".data"))) = { 0 };
static struct vm_amap *detaching_amap __attribute__((section(".data"))) = NULL;
static vsize_t detaching_size __attribute__((section(".data"))) = 0;
const char rvm_bad_map[] = "Invalid bootstrap VM map operation";
pool_cache_t rvm_map_cache_address(void);
int rvm_mi_map(struct vm_map *, vaddr_t *, vsize_t, struct uvm_object *,
    voff_t, vsize_t, uvm_flag_t);

static int
available(void)
{
	return rvm_uvm_boot.initialized && rvm_boot_get_cr() == RVM_CR_MMU &&
	    curcpu()->ci_cpl == IPL_NONE;
}

int
rvm_bootmap_init(void)
{
	if (kernel_map != NULL || !available())
		return 0;
	uvm_map_init();
	uvm_map_setup(&map_store, RVM_BOOTALLOC_BASE,
	    RVM_BOOTALLOC_BASE + RVM_BOOTALLOC_SIZE, 0);
	map_store.pmap = pmap_kernel();
	kernel_map = &map_store;
	return 1;
}

static int
reserve(vaddr_t *address, vsize_t size, vsize_t align, int fixed,
    struct uvm_object *obj)
{
	uvm_flag_t flags;
	vm_prot_t prot = obj == NULL ? VM_PROT_NONE : VM_PROT_READ | VM_PROT_WRITE;

	if (kernel_map == NULL || !available() || address == NULL ||
	    size == 0 || (size & PAGE_MASK) != 0 ||
	    size > RVM_BOOTALLOC_SIZE || (fixed != 0 && fixed != 1) ||
	    (fixed && (align != 0 || (*address & PAGE_MASK) != 0)) ||
	    (!fixed && (align < PAGE_SIZE || !powerof2(align) ||
	    align > RVM_BOOTALLOC_SIZE)))
		return EINVAL;
	if (fixed && (*address < RVM_BOOTALLOC_BASE ||
	    *address > RVM_BOOTALLOC_BASE + RVM_BOOTALLOC_SIZE - size))
		return EINVAL;
	flags = UVM_FLAG_NOWAIT | UVM_FLAG_NOMERGE;
	if (fixed)
		flags |= UVM_FLAG_FIXED;
	return rvm_mi_map(kernel_map, address, size, obj, 0,
	    align, UVM_MAPFLAG(prot, prot, UVM_INH_NONE,
	    UVM_ADV_RANDOM, flags));
}

int
rvm_bootmap_reserve(vaddr_t *address, vsize_t size, vsize_t align, int fixed)
{
	return reserve(address, size, align, fixed, NULL);
}

int
rvm_bootmap_reserve_object(vaddr_t *address, vsize_t size, struct uvm_object *obj)
{
	int error;
	if (!rvm_bootobj_valid(obj) || size == 0 ||
	    !rvm_bootobj_offset(obj, size - PAGE_SIZE))
		return EINVAL;
	error = reserve(address, size, PAGE_SIZE, 0, obj);
	if (error == 0)
		obj->uo_refs++; /* The resident pager has no detach callback. */
	return error;
}

int
rvm_bootmap_release(vaddr_t address, vsize_t size)
{
	struct vm_map_entry *entry, *dead;
	struct uvm_object *obj;

	if (kernel_map == NULL || !available() || size == 0 ||
	    ((address | size) & PAGE_MASK) != 0 ||
	    size > RVM_BOOTALLOC_SIZE || address < RVM_BOOTALLOC_BASE ||
	    address > RVM_BOOTALLOC_BASE + RVM_BOOTALLOC_SIZE - size)
		return EINVAL;
	vm_map_lock(kernel_map);
	if (!uvm_map_lookup_entry(kernel_map, address, &entry) ||
	    entry->start != address || entry->end != address + size ||
	    entry->aref.ar_amap != NULL ||
	    (entry->etype != 0 && (entry->etype != UVM_ET_OBJ ||
	    !rvm_bootobj_valid(entry->object.uvm_obj) || entry->offset != 0))) {
		vm_map_unlock(kernel_map);
		return EINVAL;
	}
	obj = entry->etype == UVM_ET_OBJ ? entry->object.uvm_obj : NULL;
	/* Physical ownership belongs to the caller. VAONLY avoids page removal
	 * and object services, which are not part of this reservation API.
	 */
	uvm_unmap_remove(kernel_map, address, address + size, &dead, UVM_FLAG_VAONLY);
	vm_map_unlock(kernel_map);
	uvm_unmap_detach(dead, 0);
	if (obj != NULL)
		obj->uo_refs--;
	return 0;
}

int
rvm_bootmap_attach_amap(vaddr_t address, vsize_t size, struct vm_amap *amap)
{
	struct vm_map_entry *entry;
	if (!available() || kernel_map == NULL || size == 0 ||
	    ((address | size) & PAGE_MASK) != 0)
		return EINVAL;
	vm_map_lock(kernel_map);
	if (!uvm_map_lookup_entry(kernel_map, address, &entry) ||
	    entry->start != address || entry->end - address != size ||
	    entry->etype != 0 || entry->aref.ar_amap != NULL ||
	    entry->object.uvm_obj != NULL || entry->wired_count != 0 ||
	    entry->protection != VM_PROT_NONE || !rvm_bootamap_entry_pin(amap, size)) {
		vm_map_unlock(kernel_map);
		return EINVAL;
	}
	entry->aref.ar_amap = amap;
	entry->aref.ar_pageoff = 0;
	entry->protection = VM_PROT_READ | VM_PROT_WRITE;
	entry->max_protection = entry->protection;
	vm_map_unlock(kernel_map);
	return 0;
}

/* Called only by the real MI detach body under the MAP compilation profile. */
void
rvm_map_amap_unref(struct vm_amap *amap, vaddr_t offset, vsize_t pages, bool all)
{
	if (!available() || amap != detaching_amap || offset != 0 || all ||
	    pages != detaching_size / PAGE_SIZE || !rvm_bootamap_entry_drop(amap))
		panic(rvm_bad_map);
	detaching_amap = NULL;
	detaching_size = 0;
}

int
rvm_bootmap_release_amap(vaddr_t address, vsize_t size, struct vm_amap *amap)
{
	struct vm_map_entry *entry, *dead;
	if (!available() || kernel_map == NULL || size == 0 ||
	    ((address | size) & PAGE_MASK) != 0 || amap == NULL || detaching_amap != NULL)
		return EINVAL;
	vm_map_lock(kernel_map);
	if (!uvm_map_lookup_entry(kernel_map, address, &entry) ||
	    entry->start != address || entry->end - address != size ||
	    entry->etype != 0 || entry->aref.ar_amap != amap ||
	    entry->aref.ar_pageoff != 0 || entry->object.uvm_obj != NULL ||
	    entry->wired_count != 0 || rvm_bootamap_owner_lock(amap) == NULL) {
		vm_map_unlock(kernel_map);
		return EINVAL;
	}
	/* This kernel map is nonpageable: VAONLY avoids freeing anon-owned pages
	 * through the raw km path. Remove aliases before dropping any reference.
	 */
	pmap_kremove(address, size);
	pmap_update(pmap_kernel());
	uvm_unmap_remove(kernel_map, address, address + size, &dead, UVM_FLAG_VAONLY);
	vm_map_unlock(kernel_map);
	detaching_amap = amap;
	detaching_size = size;
	uvm_unmap_detach(dead, 0);
	if (detaching_amap != NULL)
		panic(rvm_bad_map);
	return 0;
}

void *
rvm_map_entry_get(pool_cache_t cache, int flags, paddr_t *pa)
{
	unsigned int i;

	if (cache != rvm_map_cache_address() || flags != PR_NOWAIT || pa != NULL ||
	    !available())
		panic(rvm_bad_map);
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		if (!used[i]) {
			used[i] = 1;
			memset(&entries[i], 0, sizeof(entries[i]));
			return &entries[i];
		}
	}
	return NULL;
}

void
rvm_map_entry_put(pool_cache_t cache, void *entry, paddr_t pa)
{
	unsigned int i;

	if (cache != rvm_map_cache_address() || pa != (paddr_t)-1 || !available())
		panic(rvm_bad_map);
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		if (entry == &entries[i] && used[i]) {
			used[i] = 0;
			return;
		}
	}
	panic(rvm_bad_map);
}
