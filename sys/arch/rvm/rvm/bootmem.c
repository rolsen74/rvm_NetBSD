/* Loader validation and physical allocation before NetBSD UVM is available. */
#include <sys/null.h>
#include <machine/boot.h>
#include <machine/bootmem.h>
#include <machine/mmu.h>

/* Exported BSS symbols are not yet supported by the GNU FHR object writer. */
struct rvm_bootinfo rvm_bootinfo __attribute__((section(".data"))) = { 0 };
struct rvm_bootmem rvm_bootmem __attribute__((section(".data"))) = { 0 };

static int
inside_section(const struct rvm_bootinfo *bi, paddr_t start, psize_t size,
    unsigned long permissions)
{
	unsigned int i;

	for (i = 0; i < bi->nsections; i++) {
		const struct rvm_boot_section *s = &bi->section[i];

		if (start >= s->start && start - s->start <= s->size &&
		    size <= s->size - (start - s->start) &&
		    (s->flags & permissions) == permissions)
			return 1;
	}
	return 0;
}

int
rvm_bootmem_init(const struct rvm_bootinfo *bi)
{
	paddr_t end = 0;
	unsigned int i;

	if (rvm_bootmem.sealed || bi == NULL || ((uintptr_t)bi & 7) != 0 ||
	    bi->magic != RVM_BOOTINFO_MAGIC ||
	    bi->version != RVM_BOOTINFO_VERSION || bi->size != sizeof(*bi))
		return 0;
	if (bi->ram_start != 0 || bi->ram_end < 4 * RVM_MMU_PAGE_SIZE ||
	    (bi->ram_end & RVM_MMU_PAGE_MASK) != 0 ||
	    bi->loader_stack != bi->ram_end - RVM_MMU_PAGE_SIZE ||
	    bi->loaded_end >= bi->loader_stack ||
	    (bi->loaded_end & RVM_MMU_PAGE_MASK) != 0 ||
	    bi->nsections == 0 || bi->nsections > RVM_BOOTINFO_MAXSECTIONS)
		return 0;
	/* Keep the record, raw loader, raw FHR and loaded sections reserved. */
	if ((uintptr_t)bi >= bi->loaded_end ||
	    sizeof(*bi) > bi->loaded_end - (uintptr_t)bi)
		return 0;
	for (i = 0; i < bi->nsections; i++) {
		const struct rvm_boot_section *s = &bi->section[i];

		if (s->start < RVM_MMU_PAGE_SIZE || s->start < end ||
		    (s->start & RVM_MMU_PAGE_MASK) != 0 || s->size == 0 ||
		    s->start >= bi->loaded_end ||
		    s->size > bi->loaded_end - s->start || (s->flags & ~15UL) != 0)
			return 0;
		end = s->start + s->size;
	}
	if (((end + RVM_MMU_PAGE_MASK) & ~RVM_MMU_PAGE_MASK) != bi->loaded_end)
		return 0;
	if (!inside_section(bi, (uintptr_t)rvm_boot_stack,
	    RVM_BOOT_STACK_SIZE, RVM_BOOT_WRITE) ||
	    !inside_section(bi, (uintptr_t)rvm_boot_vectors,
	    RVM_BOOT_NVECTORS * sizeof(void *), RVM_BOOT_WRITE) ||
	    !inside_section(bi, (uintptr_t)rvm_early_init, 1, RVM_BOOT_EXEC))
		return 0;

	/* Copy before page zero (where the loader record lives) is unmapped. */
	for (i = 0; i < sizeof(*bi) / sizeof(uint64_t); i++)
		((uint64_t *)&rvm_bootinfo)[i] = ((const uint64_t *)bi)[i];
	rvm_bootmem.ram_end = bi->ram_end;
	rvm_bootmem.loaded_end = bi->loaded_end;
	rvm_bootmem.next = bi->loaded_end;
	rvm_bootmem.limit = bi->loader_stack;
	rvm_bootmem.free_start = bi->loaded_end;
	rvm_bootmem.free_end = bi->loader_stack;
	rvm_bootmem.sealed = 0;
	return 1;
}

paddr_t
rvm_bootmem_alloc(psize_t size, psize_t alignment)
{
	paddr_t start;
	psize_t i, padding;
	volatile unsigned char *p;

	if (rvm_bootmem.sealed || size == 0 || alignment == 0 ||
	    (alignment & (alignment - 1)) != 0 ||
	    alignment > RVM_MMU_PAGE_SIZE ||
	    rvm_bootmem.next > rvm_bootmem.limit)
		return 0;
	padding = -rvm_bootmem.next & (alignment - 1);
	if (padding > rvm_bootmem.limit - rvm_bootmem.next)
		return 0;
	start = rvm_bootmem.next + padding;
	if (size > rvm_bootmem.limit - start)
		return 0;
	rvm_bootmem.next = start + size;
	rvm_bootmem.free_start =
	    (rvm_bootmem.next + RVM_MMU_PAGE_MASK) & ~RVM_MMU_PAGE_MASK;
	p = (volatile unsigned char *)(uintptr_t)start;
	for (i = 0; i < size; i++)
		p[i] = 0;
	return start;
}

int
rvm_bootmem_seal(void)
{
	if (rvm_bootmem.loaded_end == 0 ||
	    rvm_bootmem.free_start >= rvm_bootmem.free_end)
		return 0;
	rvm_bootmem.sealed = 1;
	return 1;
}
