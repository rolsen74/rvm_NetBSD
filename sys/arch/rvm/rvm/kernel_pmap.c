/* Empty process-pmap lifecycle. User mappings are still rejected by pmap.c.
 * Supervisor translations stay in the shared kernel root across switches.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/atomic.h>
#include <sys/proc.h>
#include <uvm/uvm.h>
#include <machine/pmap.h>

struct pmap *
pmap_create(void)
{
	struct pmap *pm = kmem_zalloc(sizeof(*pm), KM_SLEEP);
	pm->pm_refs = 1;
	return pm;
}

void
pmap_reference(pmap_t pm)
{
	if (pm != pmap_kernel()) atomic_inc_uint(&pm->pm_refs);
}

void
pmap_destroy(pmap_t pm)
{
	KASSERT(pm != pmap_kernel() && pm->pm_refs != 0);
	if (atomic_dec_uint_nv(&pm->pm_refs) != 0) return;
	KASSERT(pm->pm_root == 0 && pm->pm_stats.resident_count == 0);
	kmem_free(pm, sizeof(*pm));
}

void
pmap_activate(struct lwp *l)
{
	struct pmap *pm = l->l_proc->p_vmspace->vm_map.pmap;
	KASSERT(pm == pmap_kernel() || pm->pm_root == 0);
}
void pmap_deactivate(struct lwp *l __unused) { }

bool
pmap_remove_all(pmap_t pm)
{
	KASSERT(pm != pmap_kernel() && pm->pm_root == 0);
	return false;
}

/* Advisory eager copying is optional: UVM will fault in pages as needed. */
void
pmap_copy(pmap_t dst __unused, pmap_t src __unused, vaddr_t daddr __unused,
    vsize_t len __unused, vaddr_t saddr __unused)
{
}
