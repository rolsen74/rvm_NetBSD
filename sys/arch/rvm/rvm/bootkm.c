/* Standalone validation around real MI wired/VA-only kernel allocation.
 * Full kernels use normal UVM startup, kmem arenas and scheduler services.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <uvm/uvm.h>
#include <machine/bootalloc.h>
#include <machine/psl.h>
#include <machine/uvm_boot.h>

struct uvm_object *uvm_kernel_object __attribute__((section(".data"))) = NULL;
static struct {
	vaddr_t address;
	vsize_t size;
	uvm_flag_t type;
} allocations[RVM_BOOTALLOC_SLOTS] __attribute__((section(".data"))) = { 0 };
const char rvm_bad_km[] = "Invalid bootstrap UVM kernel allocation operation";

vaddr_t rvm_mi_km_alloc(struct vm_map *, vsize_t, vsize_t, uvm_flag_t);
void rvm_mi_km_free(struct vm_map *, vaddr_t, vsize_t, uvm_flag_t);
int rvm_mi_map(struct vm_map *, vaddr_t *, vsize_t, struct uvm_object *,
    voff_t, vsize_t, uvm_flag_t);

static int
available(struct vm_map *map)
{
	return map != NULL && map == kernel_map && rvm_uvm_boot.initialized &&
	    rvm_boot_get_cr() == RVM_CR_MMU && curcpu()->ci_cpl == IPL_NONE;
}

vaddr_t
uvm_km_alloc(struct vm_map *map, vsize_t size, vsize_t align, uvm_flag_t flags)
{
	unsigned int slot;
	uvm_flag_t type = flags & UVM_KMF_TYPEMASK;
	vaddr_t address;

	if (!available(map) || size == 0 || size > RVM_BOOTALLOC_SIZE ||
	    (type != UVM_KMF_WIRED && type != UVM_KMF_VAONLY) ||
	    !(flags & UVM_KMF_NOWAIT) ||
	    (flags & ~(UVM_KMF_WIRED | UVM_KMF_VAONLY | UVM_KMF_ZERO |
	    UVM_KMF_NOWAIT | UVM_KMF_TRYLOCK)) != 0 ||
	    (type == UVM_KMF_VAONLY && (flags & UVM_KMF_ZERO)) ||
	    (align != 0 && (align < PAGE_SIZE || !powerof2(align) ||
	    align > RVM_BOOTALLOC_SIZE)))
		return 0;
	for (slot = 0; slot < RVM_BOOTALLOC_SLOTS; slot++) {
		if (allocations[slot].address == 0)
			break;
	}
	if (slot == RVM_BOOTALLOC_SLOTS)
		return 0;
	size = round_page(size);
	address = rvm_mi_km_alloc(map, size, align == 0 ? PAGE_SIZE : align, flags);
	if (address != 0) {
		allocations[slot].address = address;
		allocations[slot].size = size;
		allocations[slot].type = type;
	}
	return address;
}

void
uvm_km_free(struct vm_map *map, vaddr_t address, vsize_t size, uvm_flag_t flags)
{
	unsigned int slot;

	if (!available(map) || address == 0 || (address & PAGE_MASK) != 0 ||
	    size == 0 || size > RVM_BOOTALLOC_SIZE)
		panic(rvm_bad_km);
	size = round_page(size);
	for (slot = 0; slot < RVM_BOOTALLOC_SLOTS; slot++) {
		if (allocations[slot].address == address)
			break;
	}
	if (slot == RVM_BOOTALLOC_SLOTS || allocations[slot].size != size ||
	    allocations[slot].type != flags)
		panic(rvm_bad_km);
	rvm_mi_km_free(map, address, size, flags);
	allocations[slot].address = 0;
	allocations[slot].size = 0;
	allocations[slot].type = 0;
}

/* The MI km source alone calls this shim. Separate entries make exact frees
 * and partial physical-allocation rollback possible without entry splitting
 * or an unavailable sleeping pool-cache path.
 */
int
rvm_bootkm_map(struct vm_map *map, vaddr_t *address, vsize_t size,
    struct uvm_object *obj, voff_t offset, vsize_t align, uvm_flag_t flags)
{
	if (!available(map) || obj != NULL || offset != UVM_UNKNOWN_OFFSET ||
	    !(flags & UVM_FLAG_NOWAIT) || (flags & UVM_FLAG_WAITVA))
		panic(rvm_bad_km);
	return rvm_mi_map(map, address, size, obj, offset, align,
	    flags | UVM_FLAG_NOMERGE);
}

int
rvm_uvm_km_verify(void)
{
	const uvm_flag_t wired = UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO;
	vaddr_t va;
	paddr_t pa;
	volatile uint64_t *data;
	int64_t before;
	int entries;
	unsigned int i;

	if (!available(kernel_map))
		return 0;
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	entries = kernel_map->nentries;
	va = uvm_km_alloc(kernel_map, PAGE_SIZE + 1, 2 * PAGE_SIZE, wired);
	if (va == 0 || (va & (2 * PAGE_SIZE - 1)) != 0 ||
	    curcpu()->ci_counts[CPU_COUNT_FREEPAGES] != before - 2)
		return 0;
	data = (volatile uint64_t *)va;
	for (i = 0; i < 2 * PAGE_SIZE / sizeof(*data); i++) {
		if (data[i] != 0)
			return 0;
	}
	data[0] = 0x55564d4b4d454d31UL;
	data[2 * PAGE_SIZE / sizeof(*data) - 1] = ~data[0];
	if (!pmap_extract(pmap_kernel(), va, &pa) ||
	    *(volatile uint64_t *)(uintptr_t)pa != data[0] ||
	    data[2 * PAGE_SIZE / sizeof(*data) - 1] != ~data[0])
		return 0;
	uvm_km_free(kernel_map, va, PAGE_SIZE + 1, UVM_KMF_WIRED);
	if (curcpu()->ci_counts[CPU_COUNT_FREEPAGES] != before ||
	    pmap_extract(pmap_kernel(), va, &pa))
		return 0;
	va = uvm_km_alloc(kernel_map, PAGE_SIZE, 0,
	    UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
	if (va == 0 || pmap_extract(pmap_kernel(), va, &pa) ||
	    curcpu()->ci_counts[CPU_COUNT_FREEPAGES] != before)
		return 0;
	uvm_km_free(kernel_map, va, PAGE_SIZE, UVM_KMF_VAONLY);
	return kernel_map->nentries == entries &&
	    curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before;
}
