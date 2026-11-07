/* Verify the real MI list/tree, reservation errors and entry-store reuse. */
#include <sys/errno.h>
#include <uvm/uvm.h>
#include <machine/bootmap.h>

const char map_ok[] = "MI kernel VM map reservations verified.\n";
const char map_bad[] = "VM map regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(map_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)

static void
check_map(unsigned int count)
{
	struct vm_map_entry *entry, *found, *previous, *tree;
	unsigned int n = 0;
	vsize_t total = 0;
	paddr_t pa;

	REQUIRE(!rw_lock_held(&kernel_map->lock));
	vm_map_lock_read(kernel_map);
	REQUIRE(rw_read_held(&kernel_map->lock));
	previous = &kernel_map->header;
	tree = rb_tree_iterate(&kernel_map->rb_tree, NULL, RB_DIR_LEFT);
	for (entry = previous->next; entry != &kernel_map->header;
	    entry = entry->next) {
		REQUIRE(entry->prev == previous && entry->start >= previous->end);
		REQUIRE(entry == tree);
		REQUIRE(entry->gap == entry->next->start - entry->end);
		REQUIRE(entry->start < entry->end && entry->etype == 0);
		REQUIRE(entry->aref.ar_amap == NULL && entry->object.uvm_obj == NULL);
		REQUIRE(entry->protection == VM_PROT_NONE);
		/* The real lookup must find both ends of each half-open entry. */
		REQUIRE(uvm_map_lookup_entry(kernel_map, entry->start, &found));
		REQUIRE(found == entry);
		REQUIRE(uvm_map_lookup_entry(kernel_map, entry->end - 1, &found));
		REQUIRE(found == entry);
		REQUIRE(!pmap_extract(pmap_kernel(), entry->start, &pa));
		total += entry->end - entry->start;
		previous = entry;
		tree = rb_tree_iterate(&kernel_map->rb_tree, tree, RB_DIR_RIGHT);
		n++;
		REQUIRE(n <= RVM_BOOTALLOC_SLOTS);
	}
	REQUIRE(kernel_map->header.prev == previous);
	REQUIRE(tree == NULL);
	REQUIRE(n == count && kernel_map->nentries == (int)count);
	REQUIRE(kernel_map->size == total);
	vm_map_unlock_read(kernel_map);
	REQUIRE(!rw_lock_held(&kernel_map->lock));
}

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	vaddr_t a, b, c, slots[RVM_BOOTALLOC_SLOTS];
	int64_t before;
	unsigned int i, index;
	krwlock_t lock;

	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 32 && !rvm_bootmap_init());
	REQUIRE(kernel_map != NULL && kernel_map->pmap == pmap_kernel());
	REQUIRE(vm_map_min(kernel_map) == RVM_BOOTALLOC_BASE);
	REQUIRE(vm_map_max(kernel_map) == RVM_BOOTALLOC_BASE + RVM_BOOTALLOC_SIZE);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	check_map(0);

	rw_init(&lock);
	REQUIRE(rw_tryenter(&lock, RW_READER));
	REQUIRE(rw_tryenter(&lock, RW_READER));
	REQUIRE(!rw_tryenter(&lock, RW_WRITER));
	rw_exit(&lock);
	REQUIRE(rw_read_held(&lock));
	rw_exit(&lock);
	REQUIRE(rw_tryenter(&lock, RW_WRITER));
	REQUIRE(rw_write_held(&lock));
	REQUIRE(!rw_tryenter(&lock, RW_READER) && !rw_tryenter(&lock, RW_WRITER));
	rw_exit(&lock);
	REQUIRE(!rw_lock_held(&lock));

	a = RVM_BOOTALLOC_BASE;
	REQUIRE(rvm_bootmap_reserve(NULL, PAGE_SIZE, PAGE_SIZE, 0) == EINVAL);
	REQUIRE(rvm_bootmap_reserve(&a, 0, PAGE_SIZE, 0) == EINVAL);
	REQUIRE(rvm_bootmap_reserve(&a, 1, PAGE_SIZE, 0) == EINVAL);
	REQUIRE(rvm_bootmap_reserve(&a, PAGE_SIZE, 3 * PAGE_SIZE, 0) == EINVAL);
	REQUIRE(rvm_bootmap_reserve(&a, PAGE_SIZE, PAGE_SIZE, 1) == EINVAL);
	REQUIRE(rvm_bootmap_reserve(&a, PAGE_SIZE, PAGE_SIZE, 2) == EINVAL);
	REQUIRE(rvm_bootmap_release(a, PAGE_SIZE) == EINVAL);
	a = (vaddr_t)-PAGE_SIZE;
	REQUIRE(rvm_bootmap_reserve(&a, 2 * PAGE_SIZE, 0, 1) == EINVAL);
	check_map(0);

	a = RVM_BOOTALLOC_BASE + PAGE_SIZE;
	REQUIRE(rvm_bootmap_reserve(&a, 2 * PAGE_SIZE, 0, 1) == 0);
	b = a + PAGE_SIZE;
	REQUIRE(rvm_bootmap_reserve(&b, PAGE_SIZE, 0, 1) != 0);
	REQUIRE(rvm_bootmap_release(a, PAGE_SIZE) == EINVAL);
	REQUIRE(rvm_bootmap_release(a + PAGE_SIZE, PAGE_SIZE) == EINVAL);
	check_map(1);
	b = RVM_BOOTALLOC_BASE;
	REQUIRE(rvm_bootmap_reserve(&b, PAGE_SIZE, 4 * PAGE_SIZE, 0) == 0);
	REQUIRE((b & (4 * PAGE_SIZE - 1)) == 0 && b != a);
	check_map(2);
	REQUIRE(!rvm_bootmap_release(a, 2 * PAGE_SIZE));
	REQUIRE(rvm_bootmap_release(a, 2 * PAGE_SIZE) == EINVAL);
	REQUIRE(!rvm_bootmap_release(b, PAGE_SIZE));
	check_map(0);

	/* Insert in permuted order to exercise tree rotations and gap metadata,
	 * then remove in another order. Repeat to detect leaked entry slots.
	 */
	for (unsigned int pass = 0; pass < 2; pass++) {
		for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
			index = (i * 13) % RVM_BOOTALLOC_SLOTS;
			slots[index] = RVM_BOOTALLOC_BASE + index * 2 * PAGE_SIZE;
			REQUIRE(!rvm_bootmap_reserve(&slots[index], PAGE_SIZE, 0, 1));
			check_map(i + 1);
		}
		c = RVM_BOOTALLOC_BASE + PAGE_SIZE;
		REQUIRE(rvm_bootmap_reserve(&c, PAGE_SIZE, 0, 1) == ENOMEM);
		check_map(RVM_BOOTALLOC_SLOTS);
		for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
			index = (i * 7) % RVM_BOOTALLOC_SLOTS;
			REQUIRE(!rvm_bootmap_release(slots[index], PAGE_SIZE));
			check_map(RVM_BOOTALLOC_SLOTS - i - 1);
		}
	}
	a = RVM_BOOTALLOC_BASE;
	REQUIRE(!rvm_bootmap_reserve(&a, RVM_BOOTALLOC_SIZE, PAGE_SIZE, 0));
	b = RVM_BOOTALLOC_BASE;
	REQUIRE(rvm_bootmap_reserve(&b, PAGE_SIZE, PAGE_SIZE, 0) == ENOMEM);
	check_map(1);
	REQUIRE(!rvm_bootmap_release(a, RVM_BOOTALLOC_SIZE));
	check_map(0);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(curcpu()->ci_cpl == IPL_NONE);
	rvm_early_puts(map_ok);
}
