/* Real MI raw-page allocator tests, after uvm_page_init(). */
#include <sys/systm.h>
#include <uvm/uvm.h>

const char rvm_test_pages_ok[] = "Managed UVM pages verified.\n";
const char rvm_test_pages_bad[] = "Managed UVM regression at line 0x";

#define REQUIRE(condition) do { \
	if (!(condition)) { \
		rvm_early_puts(rvm_test_pages_bad); \
		rvm_early_puthex(__LINE__); \
		rvm_boot_stop(); \
	} \
} while (0)

void
rvm_early_init(const struct rvm_bootinfo *bi)
{
	struct vm_page *pages[2048], *page, *again;
	unsigned char seen[2048] = { 0 };
	volatile uint64_t *data;
	uvm_physseg_t bank;
	paddr_t first, end, pa;
	int64_t before, remaining;
	unsigned int n = 0, i, count;
	u_int saved_flags;
	kmutex_t lock;

	rvm_normal_early_init(bi);
	REQUIRE(uvm.page_init_done && rvm_uvm_boot.initialized);
	REQUIRE(curlwp != NULL && curlwp != uvm.pagedaemon_lwp);
	REQUIRE(!rvm_uvm_pageinit());
	bank = uvm_physseg_get_first();
	first = uvm_physseg_get_start(bank);
	end = uvm_physseg_get_end(bank);
	before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	REQUIRE(before == uvmexp.npages && before == (int64_t)(end - first));
	REQUIRE(first > rvm_uvm_boot.first + 1 && end == rvm_uvm_boot.end);
	REQUIRE(PHYS_TO_VM_PAGE(ptoa(first)) != NULL);
	REQUIRE(PHYS_TO_VM_PAGE(rvm_uvm_boot.probe) == NULL);
	REQUIRE(PHYS_TO_VM_PAGE(ptoa(first - 1)) == NULL);
	REQUIRE(PHYS_TO_VM_PAGE(ptoa(end)) == NULL);
	REQUIRE(curcpu()->ci_cpl == IPL_NONE);
	mutex_init(&lock, MUTEX_NODEBUG, IPL_VM);
	mutex_enter(&lock);
	REQUIRE(curcpu()->ci_cpl == IPL_VM && lock.mtx_state == 1);
	REQUIRE(splraise(IPL_SOFTCLOCK) == IPL_VM);
	mutex_exit(&lock);
	REQUIRE(curcpu()->ci_cpl == IPL_NONE && lock.mtx_state == 0);

	page = uvm_pagealloc_strat(NULL, 0, NULL, UVM_PGA_USERESERVE,
	    UVM_PGA_STRAT_NORMAL, VM_FREELIST_DEFAULT);
	REQUIRE(page != NULL);
	pa = VM_PAGE_TO_PHYS(page);
	REQUIRE(PHYS_TO_VM_PAGE(pa) == page && pa >= ptoa(first) && pa < ptoa(end));
	data = (volatile uint64_t *)(uintptr_t)pa;
	for (i = 0; i < PAGE_SIZE / sizeof(*data); i++)
		data[i] = ~(uint64_t)i;
	uvm_pagefree(page);
	again = uvm_pagealloc_strat(NULL, 0, NULL, UVM_PGA_USERESERVE | UVM_PGA_ZERO,
	    UVM_PGA_STRAT_NORMAL, VM_FREELIST_DEFAULT);
	REQUIRE(again == page);
	for (i = 0; i < PAGE_SIZE / sizeof(*data); i++)
		REQUIRE(data[i] == 0);
	uvm_pagefree(again);
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);

	/* Exhaust normal pages, then the kernel reserve, without touching the
	 * pagedaemon reserve. The metadata identity map must remain usable.
	 */
	REQUIRE(before < 2048 && end <= 2048);
	saved_flags = curlwp->l_flag;
	curlwp->l_flag &= ~LW_SYSTEM; /* Normal callers may not consume kernel reserves. */
	for (;;) {
		page = uvm_pagealloc_strat(NULL, 0, NULL, 0,
		    UVM_PGA_STRAT_NORMAL, VM_FREELIST_DEFAULT);
		if (page == NULL)
			break;
		REQUIRE(n < 2048);
		pages[n++] = page;
	}
	remaining = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	REQUIRE(remaining == MIN(before, uvmexp.reserve_kernel));
	for (;;) {
		page = uvm_pagealloc_strat(NULL, 0, NULL, UVM_PGA_USERESERVE,
		    UVM_PGA_STRAT_NORMAL, VM_FREELIST_DEFAULT);
		if (page == NULL)
			break;
		REQUIRE(n < 2048);
		pages[n++] = page;
	}
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == uvmexp.reserve_pagedaemon);
	REQUIRE(n == before - uvmexp.reserve_pagedaemon);
	count = n;
	for (i = 0; i < n; i++) {
		pa = VM_PAGE_TO_PHYS(pages[i]);
		REQUIRE(atop(pa) >= first && atop(pa) < end && !seen[atop(pa)]);
		seen[atop(pa)] = 1;
		REQUIRE(PHYS_TO_VM_PAGE(pa) == pages[i]);
		*(volatile uint64_t *)(uintptr_t)pa = 0x52564d5041474500UL ^ i;
	}
	for (i = 0; i < count; i++) {
		pa = VM_PAGE_TO_PHYS(pages[i]);
		REQUIRE(*(volatile uint64_t *)(uintptr_t)pa == (0x52564d5041474500UL ^ i));
		uvm_pagefree(pages[i]);
	}
	REQUIRE(curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before);
	REQUIRE(uvm.page_free[0].pgfl_buckets[0]->pgb_nfree == (uintptr_t)before);
	REQUIRE(curcpu()->ci_cpl == IPL_NONE);
	curlwp->l_flag = saved_flags;
	rvm_early_puts(rvm_test_pages_ok);
}
