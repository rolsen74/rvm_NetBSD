/* Resident-only bootstrap objects using real MI object/page/radix code. */
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <uvm/uvm.h>
#include <machine/bootalloc.h>
#include <machine/bootobject.h>
#include <machine/psl.h>
#include <machine/uvm_boot.h>

static struct {
	struct uvm_object object;
	krwlock_t lock;
	vsize_t size;
} objects[RVM_BOOTALLOC_SLOTS] __attribute__((section(".data"))) = { 0 };
/* A private pager type identifies resident objects, with no paging callbacks. */
static const struct uvm_pagerops resident_pager = { 0 };
static uint64_t nodes[RVM_BOOT_RADIX_NODES][16] __aligned(16)
    __attribute__((section(".data"))) = { 0 };
static unsigned char node_used[RVM_BOOT_RADIX_NODES]
    __attribute__((section(".data"))) = { 0 };
const char rvm_bad_object[] = "Invalid resident bootstrap object operation";

static int
available(void)
{
	return rvm_uvm_boot.initialized && rvm_boot_get_cr() == RVM_CR_MMU &&
	    curcpu()->ci_cpl == IPL_NONE;
}

int
rvm_bootobj_valid(struct uvm_object *obj)
{
	unsigned int i;
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		if (obj == &objects[i].object && objects[i].size != 0)
			return available();
	}
	return 0;
}

int
rvm_bootobj_offset(struct uvm_object *obj, voff_t offset)
{
	unsigned int i;
	if (offset < 0 || (offset & PAGE_MASK) != 0 || !available())
		return 0;
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		if (obj == &objects[i].object)
			return (vsize_t)offset < objects[i].size;
	}
	return 0;
}

void
rvm_bootobj_assert(struct uvm_object *obj)
{
	if (!rvm_bootobj_valid(obj) || !rw_write_held(obj->vmobjlock))
		panic(rvm_bad_object);
}

struct uvm_object *
rvm_bootobj_create(vsize_t size)
{
	unsigned int i;
	if (!available() || size == 0 || (size & PAGE_MASK) != 0 ||
	    size > (vsize_t)__type_max(voff_t))
		return NULL;
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		if (objects[i].size == 0) {
			uvm_obj_init(&objects[i].object, &resident_pager, false, 1);
			rw_init(&objects[i].lock);
			uvm_obj_setlock(&objects[i].object, &objects[i].lock);
			objects[i].size = size;
			return &objects[i].object;
		}
	}
	return NULL;
}

int
rvm_bootobj_destroy(struct uvm_object *obj)
{
	unsigned int i;
	if (!rvm_bootobj_valid(obj) || rw_lock_held(obj->vmobjlock) ||
	    obj->uo_refs != 1 || obj->uo_npages != 0 ||
	    !radix_tree_empty_tree_p(&obj->uo_pages))
		return 0;
	uvm_obj_destroy(obj, false);
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		if (obj == &objects[i].object) {
			objects[i].size = 0;
			return 1;
		}
	}
	return 0;
}

struct vm_page *
rvm_bootobj_alloc(struct uvm_object *obj, voff_t offset)
{
	struct vm_page *pg;
	if (!rvm_bootobj_valid(obj) || !rvm_bootobj_offset(obj, offset) ||
	    rw_lock_held(obj->vmobjlock))
		return NULL;
	rw_enter(obj->vmobjlock, RW_WRITER);
	if (uvm_pagelookup(obj, offset) != NULL) {
		rw_exit(obj->vmobjlock);
		return NULL;
	}
	pg = uvm_pagealloc_strat(obj, offset, NULL, UVM_PGA_ZERO,
	    UVM_PGA_STRAT_NORMAL, VM_FREELIST_DEFAULT);
	if (pg != NULL) {
		/* ZERO has supplied valid contents; no pager or waiter can exist. */
		pg->flags &= ~(PG_FAKE | PG_BUSY);
		uvm_pagelock(pg);
		uvm_pagewire(pg);
		uvm_pageunlock(pg);
	}
	rw_exit(obj->vmobjlock);
	return pg;
}

int
rvm_bootobj_free(struct uvm_object *obj, voff_t offset)
{
	struct vm_page *pg;
	if (!rvm_bootobj_valid(obj) || !rvm_bootobj_offset(obj, offset) ||
	    rw_lock_held(obj->vmobjlock))
		return 0;
	rw_enter(obj->vmobjlock, RW_WRITER);
	pg = uvm_pagelookup(obj, offset);
	if (pg != NULL)
		uvm_pagefree(pg); /* MI removes the tree entry and unwires on free. */
	rw_exit(obj->vmobjlock);
	return pg != NULL;
}

/* Only the MI radix-tree compilation uses these bounded allocation hooks.
 * No general kmem allocator is provided. Node storage is part of the image.
 */
void *
rvm_bootradix_alloc(size_t size, km_flag_t flags)
{
	unsigned int i;
	if (!available() || size != sizeof(nodes[0]) || flags != KM_NOSLEEP)
		panic(rvm_bad_object);
	for (i = 0; i < RVM_BOOT_RADIX_NODES; i++) {
		if (!node_used[i]) {
			node_used[i] = 1;
			return nodes[i];
		}
	}
	return NULL;
}

void
rvm_bootradix_free(void *node, size_t size)
{
	unsigned int i;
	if (!available() || size != sizeof(nodes[0]))
		panic(rvm_bad_object);
	for (i = 0; i < RVM_BOOT_RADIX_NODES; i++) {
		if (node == nodes[i] && node_used[i]) {
			node_used[i] = 0;
			return;
		}
	}
	panic(rvm_bad_object);
}

unsigned int
rvm_bootradix_used(void)
{
	unsigned int i, count = 0;
	for (i = 0; i < RVM_BOOT_RADIX_NODES; i++)
		count += node_used[i];
	return count;
}
