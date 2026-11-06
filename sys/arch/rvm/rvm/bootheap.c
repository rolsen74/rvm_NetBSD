/* Non-sleeping bootstrap kmem API. Full kernels use subr_kmem and its caches. */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/vmem.h>
#include <sys/vmem_impl.h>
#include <uvm/uvm.h>
#include <machine/bootalloc.h>
#include <machine/bootheap.h>
#include <machine/psl.h>
#include <machine/uvm_boot.h>

static struct vmem arena_store __attribute__((section(".data"))) = { 0 };
static struct vmem_btag tags[RVM_BOOTHEAP_TAGS]
    __attribute__((section(".data"))) = { 0 };
static vmem_t *arena __attribute__((section(".data"))) = NULL;
static struct {
	void *address;
	size_t size;
} records[RVM_BOOTHEAP_RECORDS] __attribute__((section(".data"))) = { 0 };
const char rvm_heap_name[] = "rvm-heap";
const char rvm_heap_bad[] = "Invalid bootstrap heap operation";
const char rvm_heap_sleep[] = "Bootstrap heap requires KM_NOSLEEP";
const char rvm_heap_free_info[] =
    "Heap free address %lx size %lu expected %lu slot %u\n";

vmem_t *rvm_mi_heap_init(vmem_t *);
int rvm_mi_heap_alloc(vmem_t *, vmem_size_t, vm_flag_t, vmem_addr_t *);
void rvm_mi_heap_free(vmem_t *, vmem_addr_t, vmem_size_t);

static int
available(void)
{
	return rvm_uvm_boot.initialized && kernel_map != NULL &&
	    rvm_boot_get_cr() == RVM_CR_MMU && curcpu()->ci_cpl == IPL_NONE;
}

int
rvm_bootheap_init(void)
{
	if (arena != NULL || !available())
		return 0;
	arena = rvm_mi_heap_init(&arena_store);
	if (arena == NULL)
		return 0;
	vmem_add_bts(arena, tags, RVM_BOOTHEAP_TAGS);
	return 1;
}

int
rvm_heap_import(vmem_t *source, vmem_size_t size, vmem_size_t *actual,
    vm_flag_t flags, vmem_addr_t *address)
{
	vaddr_t va;
	if (source != NULL || !available() || (flags & VM_SLEEP) ||
	    !(flags & VM_NOSLEEP) || size == 0 || size > RVM_BOOTALLOC_SIZE)
		return ENOMEM;
	size = round_page(size);
	va = uvm_km_alloc(kernel_map, size, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT);
	if (va == 0)
		return ENOMEM;
	*actual = size;
	*address = va;
	return 0;
}

void
rvm_heap_release(vmem_t *source, vmem_addr_t address, vmem_size_t size)
{
	if (source != NULL || !available())
		panic(rvm_heap_bad);
	uvm_km_free(kernel_map, address, size, UVM_KMF_WIRED);
}

void *
kmem_alloc(size_t size, km_flag_t flags)
{
	unsigned int slot;
	vmem_addr_t address;
	if (flags != KM_NOSLEEP)
		panic(rvm_heap_sleep);
	if (arena == NULL || !available() || size == 0 ||
	    size > RVM_BOOTALLOC_SIZE)
		return NULL;
	for (slot = 0; slot < RVM_BOOTHEAP_RECORDS; slot++) {
		if (records[slot].address == NULL)
			break;
	}
	if (slot == RVM_BOOTHEAP_RECORDS ||
	    rvm_mi_heap_alloc(arena, size, VM_NOSLEEP | VM_BESTFIT, &address) != 0)
		return NULL;
	records[slot].address = (void *)address;
	records[slot].size = size;
	return (void *)address;
}

void *
kmem_zalloc(size_t size, km_flag_t flags)
{
	void *memory = kmem_alloc(size, flags);
	if (memory != NULL)
		memset(memory, 0, size);
	return memory;
}

void
kmem_free(void *address, size_t size)
{
	unsigned int slot;
	if (arena == NULL || !available() || address == NULL || size == 0)
		panic(rvm_heap_bad);
	for (slot = 0; slot < RVM_BOOTHEAP_RECORDS; slot++) {
		if (records[slot].address == address)
			break;
	}
	if (slot == RVM_BOOTHEAP_RECORDS || records[slot].size != size) {
		printf(rvm_heap_free_info, (unsigned long)address, (unsigned long)size,
		    (unsigned long)(slot == RVM_BOOTHEAP_RECORDS ? 0 : records[slot].size), slot);
		panic(rvm_heap_bad);
	}
	rvm_mi_heap_free(arena, (vmem_addr_t)address, size);
	records[slot].address = NULL;
	records[slot].size = 0;
}

unsigned int
rvm_bootheap_live(void)
{
	unsigned int i, live = 0;
	for (i = 0; i < RVM_BOOTHEAP_RECORDS; i++)
		live += records[i].address != NULL;
	return live;
}

vsize_t
rvm_bootheap_size(int mask)
{
	if (arena == NULL || !available())
		return 0;
	return vmem_size(arena, mask);
}

int
rvm_bootheap_ready(void)
{
	return arena != NULL && available();
}

int
rvm_bootheap_verify(void)
{
	volatile unsigned char *small, *large;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	unsigned int i;
	if (!rvm_bootheap_init())
		return 0;
	small = kmem_zalloc(37, KM_NOSLEEP);
	large = kmem_zalloc(PAGE_SIZE + 17, KM_NOSLEEP);
	if (small == NULL || large == NULL ||
	    ((uintptr_t)small & 7) != 0 || ((uintptr_t)large & 7) != 0)
		return 0;
	for (i = 0; i < 37; i++) {
		if (small[i] != 0)
			return 0;
		small[i] = i;
	}
	for (i = 0; i < PAGE_SIZE + 17; i++) {
		if (large[i] != 0)
			return 0;
		large[i] = i;
	}
	for (i = 0; i < 37; i++) {
		if (small[i] != i)
			return 0;
	}
	kmem_free(__UNVOLATILE(small), 37);
	kmem_free(__UNVOLATILE(large), PAGE_SIZE + 17);
	return rvm_bootheap_live() == 0 && vmem_size(arena, VMEM_ALLOC | VMEM_FREE) == 0 &&
	    curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before;
}
