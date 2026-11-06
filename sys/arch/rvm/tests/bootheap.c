/* Real vmem split/coalesce and backing-page lifetime through the kmem API. */
#include <sys/kmem.h>
#include <sys/vmem.h>
#include <uvm/uvm.h>
#include <machine/bootheap.h>

const char heap_ok[] = "Bootstrap kmem heap fragmentation and exhaustion verified.\n";
const char heap_bad[] = "Heap regression at line 0x";
#define REQUIRE(c) do { if (!(c)) { rvm_early_puts(heap_bad); \
    rvm_early_puthex(__LINE__); rvm_boot_stop(); } } while (0)

static void
empty(int64_t before)
{
	REQUIRE(rvm_bootheap_live() == 0);
	REQUIRE(rvm_bootheap_size(VMEM_ALLOC | VMEM_FREE) == 0);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(kernel_map->nentries == 0 && curcpu()->ci_cpl == IPL_NONE);
}

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	unsigned char *slots[RVM_BOOTHEAP_RECORDS], *a, *b, *c, *large;
	int64_t before;
	unsigned int i, j, n, previous = 0;

	rvm_normal_early_init(bi);
	REQUIRE(rvm_boot_stage == 15 && !rvm_bootheap_init());
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	empty(before);
	REQUIRE(!kmem_alloc(0, KM_NOSLEEP));
	REQUIRE(!kmem_alloc((size_t)-1, KM_NOSLEEP));
	REQUIRE(!kmem_alloc(RVM_BOOTALLOC_SIZE + 1, KM_NOSLEEP));

	/* Many live small blocks must share one physical page. */
	for (i = 0; i < 128; i++) {
		slots[i] = kmem_alloc(17, KM_NOSLEEP);
		REQUIRE(slots[i] != NULL && ((uintptr_t)slots[i] & 7) == 0);
		for (j = 0; j < i; j++)
			REQUIRE((uintptr_t)slots[i] + 17 <= (uintptr_t)slots[j] ||
			    (uintptr_t)slots[j] + 17 <= (uintptr_t)slots[i]);
		for (j = 0; j < 17; j++)
			slots[i][j] = i ^ j;
	}
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before - 1);
	for (i = 0; i < 128; i++) {
		for (j = 0; j < 17; j++)
			REQUIRE(slots[i][j] == (i ^ j));
	}
	for (i = 0; i < 128; i += 2)
		kmem_free(slots[i], 17);
	for (i = 1; i < 128; i += 2)
		kmem_free(slots[i], 17);
	empty(before);

	a = kmem_alloc(37, KM_NOSLEEP);
	b = kmem_alloc(64, KM_NOSLEEP);
	c = kmem_alloc(96, KM_NOSLEEP);
	REQUIRE(a && b && c);
	memset(a, 0xff, 37);
	memset(b, 0x55, 64);
	memset(c, 0xaa, 96);
	kmem_free(a, 37);
	large = kmem_zalloc(37, KM_NOSLEEP);
	REQUIRE(large == a); /* Best fit reuses the exact hole. */
	for (i = 0; i < 37; i++)
		REQUIRE(large[i] == 0);
	for (i = 0; i < 64; i++)
		REQUIRE(b[i] == 0x55);
	for (i = 0; i < 96; i++)
		REQUIRE(c[i] == 0xaa);
	large = kmem_zalloc(PAGE_SIZE + 17, KM_NOSLEEP);
	REQUIRE(large != NULL);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before - 3);
	for (i = 0; i < PAGE_SIZE + 17; i++)
		REQUIRE(large[i] == 0);
	large[PAGE_SIZE - 1] = 0x12;
	large[PAGE_SIZE] = 0x34;
	REQUIRE(large[PAGE_SIZE - 1] == 0x12 && large[PAGE_SIZE] == 0x34);
	kmem_free(large, PAGE_SIZE + 17);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before - 1);
	kmem_free(a, 37);
	kmem_free(b, 64);
	kmem_free(c, 96);
	empty(before);

	/* Tiny requests exhaust tags before either RAM or allocation records.
	 * Repeat to check that freeing returns all private metadata for reuse.
	 */
	for (j = 0; j < 3; j++) {
		for (n = 0; n < RVM_BOOTHEAP_RECORDS; n++) {
			slots[n] = kmem_alloc(1, KM_NOSLEEP);
			if (slots[n] == NULL)
				break;
			*slots[n] = n;
		}
		REQUIRE(n > 128 && n < RVM_BOOTHEAP_RECORDS);
		REQUIRE(j == 0 || n == previous);
		previous = n;
		REQUIRE(rvm_bootheap_live() == n);
		REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before - 1);
		for (i = 0; i < n; i++)
			REQUIRE(*slots[i] == i);
		while (n != 0)
			kmem_free(slots[--n], 1);
		empty(before);
	}

	/* Each request is just too large for the previous span's remainder.
	 * The 32-entry backing-map limit must return NULL, then fully recover.
	 */
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++) {
		slots[i] = kmem_alloc(PAGE_SIZE + 1, KM_NOSLEEP);
		REQUIRE(slots[i] != NULL);
	}
	REQUIRE(kmem_alloc(PAGE_SIZE + 1, KM_NOSLEEP) == NULL);
	REQUIRE(kernel_map->nentries == RVM_BOOTALLOC_SLOTS);
	for (i = 0; i < RVM_BOOTALLOC_SLOTS; i++)
		kmem_free(slots[i], PAGE_SIZE + 1);
	empty(before);

	a = kmem_alloc(RVM_BOOTALLOC_SIZE, KM_NOSLEEP);
	if (before < (int64_t)(RVM_BOOTALLOC_SIZE / PAGE_SIZE))
		REQUIRE(a == NULL); /* MI wired allocation rolls back partial pages. */
	else {
		REQUIRE(a != NULL);
		*a = 0x42;
		REQUIRE(*a == 0x42);
		kmem_free(a, RVM_BOOTALLOC_SIZE);
	}
	empty(before);
	a = kmem_zalloc(8, KM_NOSLEEP);
	REQUIRE(a != NULL && *a == 0);
	kmem_free(a, 8);
	empty(before);
	rvm_early_puts(heap_ok);
}
