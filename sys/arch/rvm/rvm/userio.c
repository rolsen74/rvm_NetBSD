/* Kernel aliases for user pages already wired by MI physio/uvm_vslock. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <uvm/uvm.h>

int
vmapbuf(struct buf *bp, vsize_t len)
{
	vaddr_t user, offset, kva;
	vsize_t size, mapped;
	paddr_t pa;
	pmap_t pm;

	KASSERT(bp->b_flags & B_PHYS);
	KASSERT(bp->b_saveaddr == NULL);
	if (bp->b_proc == NULL || len == 0) return EINVAL;
	user = (vaddr_t)bp->b_data;
	offset = user & PAGE_MASK;
	if (user < VM_MIN_ADDRESS || user >= VM_MAXUSER_ADDRESS ||
	    len > VM_MAXUSER_ADDRESS - user || len > VM_PHYS_SIZE - offset)
		return EINVAL;
	size = round_page(offset + len);
	user = trunc_page(user);
	pm = bp->b_proc->p_vmspace->vm_map.pmap;
	kva = uvm_km_alloc(phys_map, size, 0, UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
	if (kva == 0) return ENOMEM;
	for (mapped = 0; mapped < size; mapped += PAGE_SIZE) {
		if (!pmap_extract(pm, user + mapped, &pa)) {
			pmap_kremove(kva, mapped);
			pmap_update(pmap_kernel());
			uvm_km_free(phys_map, kva, size, UVM_KMF_VAONLY);
			return EFAULT;
		}
		pmap_kenter_pa(kva + mapped, pa, UVM_PROT_RW, 0);
	}
	pmap_update(pmap_kernel());
	bp->b_saveaddr = bp->b_data;
	bp->b_data = (void *)(kva + offset);
	return 0;
}

void
vunmapbuf(struct buf *bp, vsize_t len)
{
	vaddr_t kva = trunc_page((vaddr_t)bp->b_data);
	vsize_t size = round_page(((vaddr_t)bp->b_data & PAGE_MASK) + len);
	KASSERT(bp->b_flags & B_PHYS);
	KASSERT(bp->b_saveaddr != NULL && len != 0 && size <= VM_PHYS_SIZE);
	pmap_kremove(kva, size);
	pmap_update(pmap_kernel());
	uvm_km_free(phys_map, kva, size, UVM_KMF_VAONLY);
	bp->b_data = bp->b_saveaddr;
	bp->b_saveaddr = NULL;
}
