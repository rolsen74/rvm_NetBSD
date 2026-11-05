/* Included by bootstrap.py after machdep.c with its entry point renamed. */
#include <sys/systm.h>
#include <uvm/uvm.h>

const char rvm_test_uvm_ok[] = "UVM preload and stealing verified.\n";
const char rvm_test_uvm_bad[] = "UVM regression failed at line 0x";
const char rvm_test_uvm_format[] =
    "UVM diagnostic %s %d %u %ld %lu %lld %llu\n";
const char rvm_test_uvm_hex[] = "UVM hex %x %lx %llx %%\n";
const char rvm_test_uvm_name[] = "RVM";

#define REQUIRE(condition) do { \
	if (!(condition)) { \
		rvm_early_puts(rvm_test_uvm_bad); \
		rvm_early_puthex(__LINE__); \
		rvm_boot_stop(); \
	} \
} while (0)

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	uvm_physseg_t seg;
	paddr_t first, end, pa, next, physical;
	vaddr_t vstart, vend, saved_start;
	volatile uint64_t *buffer;
	unsigned long tables;
	unsigned int i;

	rvm_normal_early_init(bi);
	REQUIRE(rvm_uvm_boot.registered && !uvm.page_init_done);
	REQUIRE(uvmexp.pagesize == PAGE_SIZE && uvmexp.pagemask == PAGE_MASK &&
	    uvmexp.pageshift == PAGE_SHIFT && uvmexp.npages == 0);
	REQUIRE(uvm_physseg_get_first() == uvm_physseg_get_last());
	seg = uvm_physseg_get_first();
	REQUIRE(uvm_physseg_valid_p(seg));
	first = uvm_physseg_get_avail_start(seg);
	end = uvm_physseg_get_avail_end(seg);
	REQUIRE(first == rvm_uvm_boot.first + 1 && end == rvm_uvm_boot.end);
	REQUIRE(uvm_physseg_get_start(seg) == first &&
	    uvm_physseg_get_end(seg) == end &&
	    uvm_physseg_get_free_list(seg) == VM_FREELIST_DEFAULT);
	REQUIRE(uvm_physseg_find(first - 1, NULL) == UVM_PHYSSEG_TYPE_INVALID);
	REQUIRE(uvm_physseg_find(first, NULL) == seg);
	REQUIRE(uvm_physseg_find(end - 1, NULL) == seg);
	REQUIRE(uvm_physseg_find(end, NULL) == UVM_PHYSSEG_TYPE_INVALID);

	/* Dirty the candidate pages first, so zero-on-steal cannot pass by chance. */
	buffer = (volatile uint64_t *)(uintptr_t)ptoa(first);
	for (i = 0; i < 2 * PAGE_SIZE / sizeof(*buffer); i++)
		buffer[i] = ~(uint64_t)i;
	next = rvm_bootmem.next;
	tables = rvm_boot_pmap.tables;
	pmap_virtual_space(&vstart, &vend);
	vstart += 3 * PAGE_SIZE;
	saved_start = vstart;
	pa = pmap_steal_memory(PAGE_SIZE + 1, &vstart, &vend);
	REQUIRE(pa == ptoa(first) && vstart == saved_start);
	REQUIRE(vend == VM_KERNEL_VM_BASE + VM_KERNEL_VM_SIZE);
	REQUIRE(uvm_physseg_get_start(seg) == first + 2 &&
	    uvm_physseg_get_avail_start(seg) == first + 2);
	REQUIRE(uvm_physseg_find(first, NULL) == UVM_PHYSSEG_TYPE_INVALID &&
	    uvm_physseg_find(first + 1, NULL) == UVM_PHYSSEG_TYPE_INVALID);
	for (i = 0; i < 2 * PAGE_SIZE / sizeof(*buffer); i++)
		REQUIRE(buffer[i] == 0);
	REQUIRE(pmap_extract(pmap_kernel(), pa + 2 * PAGE_SIZE - 1, &physical) &&
	    physical == pa + 2 * PAGE_SIZE - 1);
	REQUIRE(pmap_steal_memory(1, NULL, NULL) == pa + 2 * PAGE_SIZE);
	REQUIRE(uvm_physseg_get_avail_start(seg) == first + 3 &&
	    uvm_physseg_get_avail_end(seg) == end);
	REQUIRE(!rvm_uvm_bootstrap());
	REQUIRE(uvm_physseg_get_avail_start(seg) == first + 3);
	REQUIRE(rvm_bootmem.next == next && rvm_boot_pmap.tables == tables &&
	    rvm_bootmem.sealed && rvm_bootmem_alloc(PAGE_SIZE, PAGE_SIZE) == 0);
	printf(rvm_test_uvm_format, rvm_test_uvm_name, -7, 4294967295U,
	    (-9223372036854775807L - 1), 18446744073709551615UL,
	    (-9223372036854775807LL - 1), 18446744073709551615ULL);
	printf(rvm_test_uvm_hex, 0xfedcba98U,
	    0xfedcba9876543210UL, 0xfedcba9876543210ULL);
	rvm_early_puts(rvm_test_uvm_ok);
}
