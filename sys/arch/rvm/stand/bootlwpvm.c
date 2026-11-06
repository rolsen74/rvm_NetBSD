/* Checked system-only boundary for selected MI glue. Full UVM uses its caches. */
const char rvm_lwpvm_bad[] = "Invalid bootstrap MI LWP u-area lifecycle";
static int
lwpvm_context(void)
{
	return kernel_map != NULL && rvm_boot_get_cr() == RVM_CR_MMU &&
	    !cpu_intr_p() && curcpu()->ci_cpl == IPL_NONE;
}
vaddr_t
uvm_uarea_system_alloc(struct cpu_info *ci)
{
	if (!lwpvm_context() || (ci != NULL && ci != curcpu())) return 0;
	return (vaddr_t)cpu_uarea_alloc(true);
}
void
uvm_uarea_system_free(vaddr_t base)
{
	if (!lwpvm_context() || base == 0 || (base & PAGE_MASK)) panic(rvm_lwpvm_bad);
	if (!cpu_uarea_free((void *)base))
		uvm_km_free(kernel_map, base, USPACE, UVM_KMF_WIRED);
}
vaddr_t
uvm_lwp_getuarea(struct lwp *l)
{
	if (l == NULL) panic(rvm_lwpvm_bad);
	return rvm_mi_uvm_lwp_getuarea(l);
}
void
uvm_lwp_setuarea(struct lwp *l, vaddr_t base)
{
	paddr_t pa;
	vaddr_t va;
	if (!lwpvm_context() || l == NULL || l == curlwp || l->l_addr != NULL ||
	    l->l_md.md_pcb != NULL || l->l_stat != LSIDL || !(l->l_flag & LW_SYSTEM))
		panic(rvm_lwpvm_bad);
	if (base != 0) {
		if ((base & PAGE_MASK) || base < VM_KERNEL_VM_BASE ||
		    base > VM_KERNEL_VM_BASE + VM_KERNEL_VM_SIZE - USPACE) panic(rvm_lwpvm_bad);
		for (va = base; va < base + USPACE; va += PAGE_SIZE)
			if (!pmap_extract(pmap_kernel(), va, &pa)) panic(rvm_lwpvm_bad);
		if (!rvm_uarea_bind(l, base)) panic(rvm_lwpvm_bad);
	}
	rvm_mi_uvm_lwp_setuarea(l, base);
}
void
uvm_lwp_fork(struct lwp *parent, struct lwp *child, void *stack, size_t size,
    void (*fn)(void *), void *arg)
{
	if (!lwpvm_context() || parent != curlwp || child == NULL ||
	    child->l_addr == NULL || child->l_md.md_pcb != NULL ||
	    !(child->l_flag & LW_SYSTEM)) panic(rvm_lwpvm_bad);
	rvm_mi_uvm_lwp_fork(parent, child, stack, size, fn, arg);
}
void
uvm_lwp_exit(struct lwp *l)
{
	if (!lwpvm_context() || l == NULL || l == curlwp || l->l_addr == NULL ||
	    (l->l_pflag & LP_RUNNING) || !(l->l_flag & LW_SYSTEM) ||
	    (l->l_stat != LSIDL && l->l_stat != LSZOMB) || l->l_md.md_pcb != NULL ||
	    l->l_md.md_kstack_base != 0 || l->l_md.md_kstack_size != 0 ||
	    !rvm_uarea_unbind(l)) panic(rvm_lwpvm_bad);
	rvm_mi_uvm_lwp_exit(l);
	/* MI normally clears this in DIAGNOSTIC builds; keep exact-once semantics here. */
	rvm_mi_uvm_lwp_setuarea(l, 0);
}
