/* Early wired buffers using real MI maps, resident objects, pages and MD pmap.
 * One CPU, interrupts/caches disabled. No sleeping, paging or reclamation.
 * This harness is deliberately separate from the full kernel's uvm_km API.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <uvm/uvm.h>
#include <machine/boot.h>
#include <machine/bootalloc.h>
#include <machine/bootmap.h>
#include <machine/bootobject.h>
#include <machine/psl.h>
#include <machine/uvm_boot.h>

static int initialized __attribute__((section(".data"))) = 0;
static struct {
	vaddr_t address;
	vsize_t size;
	struct uvm_object *object;
} buffers[RVM_BOOTALLOC_SLOTS] __attribute__((section(".data"))) = { 0 };
const char rvm_bootalloc_corrupt[] = "Early wired allocator ownership corrupted";

static int
available(void)
{
	return rvm_uvm_boot.initialized && rvm_boot_get_cr() == RVM_CR_MMU &&
	    curcpu()->ci_cpl == IPL_NONE;
}

int
rvm_bootalloc_init(void)
{
	paddr_t pa;
	vaddr_t va;

	if (initialized || !available())
		return 0;
	for (va = RVM_BOOTALLOC_BASE;
	    va < RVM_BOOTALLOC_BASE + RVM_BOOTALLOC_SIZE; va += PAGE_SIZE) {
		if (pmap_extract(pmap_kernel(), va, &pa))
			return 0;
	}
	if (!rvm_bootmap_init())
		return 0;
	initialized = 1;
	return 1;
}

static void
release_pages(vaddr_t base, vsize_t size, struct uvm_object *obj)
{
	vaddr_t va;
	paddr_t pa;
	struct vm_page *page;

	for (va = base; va < base + size; va += PAGE_SIZE) {
		if (!pmap_extract(pmap_kernel(), va, &pa) ||
		    (page = PHYS_TO_VM_PAGE(pa)) == NULL ||
		    page->uobject != obj || page->offset != (voff_t)(va - base))
			panic(rvm_bootalloc_corrupt);
		/* Remove the alias before returning its backing page to MI queues. */
		pmap_kremove(va, PAGE_SIZE);
		pmap_update(pmap_kernel());
		if (!rvm_bootobj_free(obj, va - base))
			panic(rvm_bootalloc_corrupt);
	}
}

vaddr_t
rvm_bootalloc(vsize_t size)
{
	vaddr_t base = RVM_BOOTALLOC_BASE;
	vsize_t done;
	struct vm_page *page;
	struct uvm_object *obj;
	unsigned int slot;
	paddr_t pa;

	if (!initialized || !available() || size == 0 ||
	    size > RVM_BOOTALLOC_SIZE)
		return 0;
	size = round_page(size);
	for (slot = 0; slot < RVM_BOOTALLOC_SLOTS; slot++) {
		if (buffers[slot].address == 0)
			break;
	}
	if (slot == RVM_BOOTALLOC_SLOTS)
		return 0;
	obj = rvm_bootobj_create(size);
	if (obj == NULL)
		return 0;
	if (rvm_bootmap_reserve_object(&base, size, obj) != 0) {
		if (!rvm_bootobj_destroy(obj))
			panic(rvm_bootalloc_corrupt);
		return 0;
	}
	for (done = 0; done < size; done += PAGE_SIZE) {
		if (pmap_extract(pmap_kernel(), base + done, &pa))
			panic(rvm_bootalloc_corrupt);
		page = rvm_bootobj_alloc(obj, done);
		if (page == NULL) {
			release_pages(base, done, obj);
			if (rvm_bootmap_release(base, size) != 0)
				panic(rvm_bootalloc_corrupt);
			if (!rvm_bootobj_destroy(obj))
				panic(rvm_bootalloc_corrupt);
			return 0;
		}
		pmap_kenter_pa(base + done, VM_PAGE_TO_PHYS(page),
		    VM_PROT_READ | VM_PROT_WRITE, 0);
	}
	pmap_update(pmap_kernel());
	buffers[slot].address = base;
	buffers[slot].size = size;
	buffers[slot].object = obj;
	return base;
}

int
rvm_bootfree(vaddr_t address)
{
	unsigned int slot;

	if (!initialized || !available() || address == 0)
		return 0;
	for (slot = 0; slot < RVM_BOOTALLOC_SLOTS; slot++) {
		if (buffers[slot].address == address)
			break;
	}
	if (slot == RVM_BOOTALLOC_SLOTS)
		return 0;
	release_pages(address, buffers[slot].size, buffers[slot].object);
	if (rvm_bootmap_release(address, buffers[slot].size) != 0)
		panic(rvm_bootalloc_corrupt);
	if (!rvm_bootobj_destroy(buffers[slot].object))
		panic(rvm_bootalloc_corrupt);
	buffers[slot].address = 0;
	buffers[slot].size = 0;
	buffers[slot].object = NULL;
	return 1;
}
