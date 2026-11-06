/* MD u-area hooks backed by the bootstrap MI map, objects and page allocator. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <uvm/uvm.h>
#include <machine/bootobject.h>
#include <machine/psl.h>
#include <machine/switch.h>
#include <machine/uarea.h>
#include <machine/bootsched.h>
#include <machine/irq.h>

static struct {
	vaddr_t reservation;
	struct uvm_object *object;
	struct lwp *owner;
} areas[RVM_UAREA_SLOTS] __attribute__((section(".data"))) = { 0 };
const char rvm_uarea_bad[] = "Invalid RVM guarded u-area ownership";
#define AREA_SIZE (PAGE_SIZE + USPACE)

static int
available(void)
{
	return kernel_map != NULL && rvm_boot_get_cr() == RVM_CR_MMU &&
	    !cpu_intr_p() && curcpu()->ci_cpl == IPL_NONE;
}
static int
find(vaddr_t base)
{
	unsigned int i;
	for (i = 0; i < RVM_UAREA_SLOTS; i++)
		if (areas[i].reservation != 0 && base == areas[i].reservation + PAGE_SIZE)
			return (int)i;
	return -1;
}
static void
release_pages(vaddr_t base, vsize_t size, struct uvm_object *obj)
{
	vsize_t off;
	paddr_t pa;
	struct vm_page *pg;
	for (off = 0; off < size; off += PAGE_SIZE) {
		if (!pmap_extract(pmap_kernel(), base + off, &pa) ||
		    (pg = PHYS_TO_VM_PAGE(pa)) == NULL || pg->uobject != obj ||
		    pg->offset != (voff_t)off || pg->wire_count != 1) panic(rvm_uarea_bad);
		pmap_kremove(base + off, PAGE_SIZE);
		pmap_update(pmap_kernel());
		if (!rvm_bootobj_free(obj, off)) panic(rvm_uarea_bad);
	}
}

/* Assembly canonicalizes the public bool argument and uses an aligned C home. */
void *
rvm_cpu_uarea_alloc(unsigned int system)
{
	unsigned int slot;
	vaddr_t reservation, base, va;
	vsize_t done;
	paddr_t pa;
	struct vm_page *pg;
	struct uvm_object *obj;
	if (!available() || !system) return NULL;
	for (slot = 0; slot < RVM_UAREA_SLOTS; slot++)
		if (areas[slot].reservation == 0) break;
	if (slot == RVM_UAREA_SLOTS) return NULL;
	reservation = uvm_km_alloc(kernel_map, AREA_SIZE, PAGE_SIZE,
	    UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
	if (!reservation) return NULL;
	base = reservation + PAGE_SIZE;
	/* Do not overwrite an alias installed independently of the VA map. */
	for (va = reservation; va < reservation + AREA_SIZE; va += PAGE_SIZE) {
		if (pmap_extract(pmap_kernel(), va, &pa)) {
			uvm_km_free(kernel_map, reservation, AREA_SIZE, UVM_KMF_VAONLY);
			return NULL;
		}
	}
	obj = rvm_bootobj_create(USPACE);
	if (obj == NULL) {
		uvm_km_free(kernel_map, reservation, AREA_SIZE, UVM_KMF_VAONLY);
		return NULL;
	}
	for (done = 0; done < USPACE; done += PAGE_SIZE) {
		pg = rvm_bootobj_alloc(obj, done);
		if (pg == NULL) {
			release_pages(base, done, obj);
			if (!rvm_bootobj_destroy(obj)) panic(rvm_uarea_bad);
			uvm_km_free(kernel_map, reservation, AREA_SIZE, UVM_KMF_VAONLY);
			return NULL;
		}
		pmap_kenter_pa(base + done, VM_PAGE_TO_PHYS(pg), VM_PROT_READ | VM_PROT_WRITE, 0);
	}
	pmap_update(pmap_kernel());
	areas[slot].reservation = reservation;
	areas[slot].object = obj;
	areas[slot].owner = NULL;
	return (void *)base;
}

bool
cpu_uarea_free(void *address)
{
	int slot;
	vaddr_t base = (vaddr_t)address, reservation;
	paddr_t pa;
	if (!available()) return false;
	slot = find(base);
	if (slot == -1) return false; /* MI may own this allocation instead. */
	reservation = areas[slot].reservation;
	if (areas[slot].owner != NULL || pmap_extract(pmap_kernel(), reservation, &pa))
		panic(rvm_uarea_bad);
	release_pages(base, USPACE, areas[slot].object);
	if (!rvm_bootobj_destroy(areas[slot].object)) panic(rvm_uarea_bad);
	uvm_km_free(kernel_map, reservation, AREA_SIZE, UVM_KMF_VAONLY);
	memset(&areas[slot], 0, sizeof(areas[slot]));
	return true;
}

int
rvm_uarea_bind(struct lwp *l, vaddr_t base)
{
	int slot = find(base);
	if (slot == -1) return 1; /* Existing bootstrap callers own plain wired stacks. */
	if (!available() || l == NULL ||
	    (areas[slot].owner != NULL && areas[slot].owner != l)) return 0;
	areas[slot].owner = l;
	return 1;
}
int
rvm_uarea_unbind(struct lwp *l)
{
	int slot = find((vaddr_t)l->l_addr);
	if (slot == -1) return 1;
	if (!available() || l == curlwp || (l->l_pflag & LP_RUNNING) ||
	    (areas[slot].owner != l &&
	    !(areas[slot].owner == NULL && l->l_md.md_pcb == NULL))) return 0;
	areas[slot].owner = NULL;
	return 1;
}

static struct lwp child __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static void
entry(void *arg)
{
	volatile uint64_t canary = 0x554152454153544bUL;
	if (arg != &child || curlwp != &child || !rvm_clock_wait() ||
	    canary != 0x554152454153544bUL) panic(rvm_uarea_bad);
	rvm_bootsched_finish();
}
int
rvm_uarea_verify(void)
{
	void *base;
	paddr_t pa;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (cpu_uarea_alloc(false) != NULL || !rvm_bootsched_init() || !rvm_clock_start()) return 0;
	base = cpu_uarea_alloc(true);
	if (!base || pmap_extract(pmap_kernel(), (vaddr_t)base - PAGE_SIZE, &pa)) return 0;
	memset(&child, 0, sizeof(child));
	child.l_addr = base; child.l_cpu = curcpu(); child.l_proc = &process;
	child.l_flag = LW_SYSTEM; child.l_pflag = LP_MPSAFE; child.l_stat = LSIDL;
	cpu_lwp_fork(curlwp, &child, NULL, 0, entry, &child);
	if (!rvm_bootsched_ready(&child)) return 0;
	rvm_bootsched_yield();
	if (child.l_stat != LSZOMB || (child.l_pflag & LP_RUNNING)) return 0;
	/* The guarded u-area remains bound until MD teardown on another stack. */
	if (!rvm_bootsched_reap(&child) || !rvm_clock_stop() || !rvm_bootsched_fini()) return 0;
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before;
}

int
rvm_lwpvm_verify(void)
{
	vaddr_t base;
	int64_t before = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_bootsched_init() || !rvm_clock_start()) return 0;
	base = uvm_uarea_system_alloc(NULL);
	if (!base) return 0;
	memset(&child, 0, sizeof(child));
	child.l_cpu = curcpu(); child.l_proc = &process;
	child.l_flag = LW_SYSTEM; child.l_pflag = LP_MPSAFE; child.l_stat = LSIDL;
	uvm_lwp_setuarea(&child, base);
	if (uvm_lwp_getuarea(&child) != base) return 0;
	uvm_lwp_fork(curlwp, &child, NULL, 0, entry, &child);
	if (!rvm_bootsched_ready(&child)) return 0;
	rvm_bootsched_yield();
	if (child.l_stat != LSZOMB || !rvm_bootsched_reap(&child) || uvm_lwp_getuarea(&child) != 0)
		return 0;
	return rvm_clock_stop() && rvm_bootsched_fini() &&
	    curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == before;
}
