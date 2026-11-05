/* Physical stealing before uvm_page_init; the bootstrap identity map stays on. */
#include <sys/param.h>
#include <sys/systm.h>
#include <uvm/uvm.h>
#include <machine/bootmem.h>
#include <machine/pmap.h>

const char rvm_uvm_badsteal[] = "Invalid UVM bootstrap allocation";
const char rvm_uvm_nophysical[] = "No contiguous UVM bootstrap memory available";
const char rvm_uvm_badunload[] = "UVM bootstrap physical unload failed";

vaddr_t
pmap_steal_memory(vsize_t size, vaddr_t *vstart __unused, vaddr_t *vend __unused)
{
	uvm_physseg_t seg;
	paddr_t first, limit, pa, expected;
	vsize_t rounded;
	psize_t pages, i;

	if (!rvm_bootmem.sealed || uvm.page_init_done || size == 0 ||
	    size > (vsize_t)-1 - PAGE_MASK)
		panic(rvm_uvm_badsteal);
	rounded = round_page(size);
	pages = atop(rounded);
	for (seg = uvm_physseg_get_first();
	    seg != UVM_PHYSSEG_TYPE_INVALID && seg <= uvm_physseg_get_last();
	    seg = uvm_physseg_get_next(seg)) {
		if (!uvm_physseg_valid_p(seg))
			continue;
		first = uvm_physseg_get_avail_start(seg);
		limit = uvm_physseg_get_avail_end(seg);
		/* Keep at least one page: MI physunload panics on an empty last bank.
		 * Our preload registers only the available range, so front stealing
		 * can shrink the segment and remove stolen pages from lookup entirely.
		 */
		if (first != uvm_physseg_get_start(seg) || first >= limit ||
		    pages >= limit - first)
			continue;
		pa = ptoa(first);
		for (i = 0; i < pages; i++) {
			expected = pa + ptoa(i);
			paddr_t stolen;
			if (!uvm_page_physunload(seg, VM_FREELIST_DEFAULT, &stolen) ||
			    stolen != expected)
				panic(rvm_uvm_badunload);
			pmap_zero_page(stolen);
		}
		return (vaddr_t)pa;
	}
	panic(rvm_uvm_nophysical);
}
