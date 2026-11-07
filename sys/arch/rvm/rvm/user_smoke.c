/* EARLY integration checks use real UVM maps and the production copy engine. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/ucontext.h>
#include <uvm/uvm.h>
#include <machine/mmu.h>
#include <machine/pmap_private.h>
#include <machine/trap.h>
#include <machine/frame.h>
#include <machine/boot.h>
#include <machine/asyncirq.h>
#include <machine/bootcopy.h>
#include <machine/reg.h>

void rvm_user_smoke(void);
void rvm_user_probe_dispatch(struct rvm_exceptionframe *);
void rvm_user_probe(paddr_t, vaddr_t, vaddr_t);
void rvm_user_probe_entry(void);
void rvm_user_probe_return(void);
extern char rvm_user_probe_code[], rvm_user_probe_end[];
extern char rvm_user_probe_kernel_read[], rvm_user_probe_text_write[];
extern char rvm_user_probe_privilege[];
static paddr_t probe_root;
static unsigned int probe_calls;
static unsigned int probe_vector, probe_version;
static vaddr_t probe_fault_address;
#define CHECK(c) do { if (!(c)) panic("RVM user VM check: " #c); } while (0)
#define COPY(vm, va, buf, n, out) rvm_copyuser(vm, va, buf, n, out, false, NULL)

static void
check_context(const struct rvm_exceptionframe *saved)
{
	struct lwp testlwp;
	struct trapframe tf, before;
	mcontext_t mc, invalid;
	struct reg regs;
	unsigned int flags = _UC_SIGMASK;
	memset(&testlwp, 0, sizeof(testlwp));
	memset(&tf, 0, sizeof(tf));
	memcpy(tf.tf_r, saved->r, sizeof(tf.tf_r));
	tf.tf_pc = saved->pc;
	tf.tf_sr = saved->header & (PSL_FLAG_MASK | PSL_IPL_MASK);
	tf.tf_usp = VM_MAXUSER_ADDRESS;
	tf.tf_ssp = (vaddr_t)saved;
	tf.tf_mmu_table = saved->root;
	tf.tf_fault_va = UINT64_C(0x123456789abcdef0);
	testlwp.l_proc = curlwp->l_proc;
	testlwp.l_md.md_tf = &tf;
	cpu_getmcontext(&testlwp, &mc, &flags);
	CHECK(flags == (_UC_SIGMASK | _UC_CPU));
	CHECK(memcmp(mc.__gregs, saved->r, sizeof(saved->r)) == 0);
	CHECK(mc.__gregs[_REG_SP] == VM_MAXUSER_ADDRESS);
	CHECK(mc.__gregs[_REG_PC] == saved->pc);
	CHECK(cpu_mcontext_validate(&testlwp, &mc) == 0);
	mc.__gregs[0] = 42;
	mc.__gregs[_REG_PC]++;
	mc.__gregs[_REG_SP] -= 4;
	mc.__gregs[_REG_SR] = PSL_C | PSL_N;
	CHECK(cpu_setmcontext(&testlwp, &mc, _UC_CPU) == 0);
	CHECK(tf.tf_r[0] == 42 && tf.tf_pc == mc.__gregs[_REG_PC]);
	CHECK(tf.tf_usp == VM_MAXUSER_ADDRESS - 4 && tf.tf_sr == (PSL_C | PSL_N));
	CHECK(tf.tf_ssp == (vaddr_t)saved && tf.tf_mmu_table == saved->root);
	CHECK(tf.tf_fault_va == UINT64_C(0x123456789abcdef0));
	before = tf;
	const uint64_t forbidden[] = { PSL_S, PSL_H, PSL_IPL_MASK, 0x100, UINT64_MAX };
	for (size_t i = 0; i < __arraycount(forbidden); i++) {
		invalid = mc;
		invalid.__gregs[_REG_SR] |= forbidden[i];
		CHECK(cpu_setmcontext(&testlwp, &invalid, _UC_CPU) == EINVAL);
		CHECK(memcmp(&before, &tf, sizeof(tf)) == 0);
	}
	invalid = mc; invalid.__gregs[_REG_PC] = 0;
	CHECK(cpu_setmcontext(&testlwp, &invalid, _UC_CPU) == EINVAL);
	invalid = mc; invalid.__gregs[_REG_PC] = VM_MAXUSER_ADDRESS;
	CHECK(cpu_setmcontext(&testlwp, &invalid, _UC_CPU) == EINVAL);
	invalid = mc; invalid.__gregs[_REG_SP]++;
	CHECK(cpu_setmcontext(&testlwp, &invalid, _UC_CPU) == EINVAL);
	invalid = mc; invalid.__gregs[_REG_SP] = VM_MAXUSER_ADDRESS + 4;
	CHECK(cpu_setmcontext(&testlwp, &invalid, _UC_CPU) == EINVAL);
	CHECK(cpu_setmcontext(&testlwp, &mc, _UC_CPU | _UC_TLSBASE) == EINVAL);
	CHECK(cpu_setmcontext(&testlwp, &mc, _UC_CPU | _UC_FPU) == EINVAL);
	CHECK(cpu_setmcontext(&testlwp, &mc, _UC_SETSTACK | _UC_CLRSTACK) == EINVAL);
	CHECK(process_read_regs(&testlwp, &regs) == 0);
	CHECK(regs.r_regs[0] == 42 && regs.r_regs[RVM_REG_SP] == tf.tf_usp);
	regs.r_regs[10] = 99;
	CHECK(process_write_regs(&testlwp, &regs) == 0 && tf.tf_r[10] == 99);
	regs.r_sr |= PSL_S;
	CHECK(process_write_regs(&testlwp, &regs) == EINVAL);
	CHECK(process_set_pc(&testlwp, (void *)VM_KERNEL_VM_BASE) == EINVAL);
	CHECK(process_set_pc(&testlwp, (void *)(VM_MIN_ADDRESS + 1)) == 0);
	CHECK(tf.tf_pc == VM_MIN_ADDRESS + 1);
	tf.tf_sr = PSL_S;
	CHECK(process_read_regs(&testlwp, &regs) == EINVAL);
	CHECK(cpu_setmcontext(&testlwp, &mc, _UC_CPU) == EINVAL);
}

static void
probe_fault(vaddr_t pc, vaddr_t sp, unsigned int vector, unsigned int frame_version,
    vaddr_t address)
{
	void (*oldvector)(void) = rvm_boot_vectors[vector];
	probe_vector = vector;
	probe_version = frame_version;
	probe_fault_address = address;
	probe_calls = 0;
	rvm_boot_vectors[vector] = rvm_user_probe_entry;
	rvm_user_probe(probe_root, pc, sp);
	rvm_boot_vectors[vector] = oldvector;
	CHECK(probe_calls == 1);
	probe_vector = 0;
}

void
rvm_user_smoke(void)
{
	struct vmspace *parent, *child;
	struct vm_page *pg;
	pmap_t pm;
	vaddr_t base = 0x100000, cross = base + PAGE_SIZE - 4;
	paddr_t pa, child_pa;
	char source[] = "rvm-user", dest[sizeof(source)];
	size_t done;
	volatile uint64_t *pte;

	parent = uvmspace_alloc(VM_MIN_ADDRESS, VM_MAXUSER_ADDRESS, false);
	CHECK(parent != NULL);
	pm = parent->vm_map.pmap;
	CHECK(uvm_map(&parent->vm_map, &base, 2 * PAGE_SIZE, NULL,
	    UVM_UNKNOWN_OFFSET, 0, UVM_MAPFLAG(UVM_PROT_RW, UVM_PROT_RW,
	    UVM_INH_COPY, UVM_ADV_NORMAL, UVM_FLAG_FIXED | UVM_FLAG_COPYONW)) == 0);
	CHECK(pmap_resident_count(pm) == 0);
	memset(dest, 0xa5, sizeof(dest));
	CHECK(COPY(parent, base, dest, sizeof(dest), false) == 0);
	for (size_t i = 0; i < sizeof(dest); i++) CHECK(dest[i] == 0);
	CHECK(COPY(parent, cross, source, sizeof(source), true) == 0);
	/* A real wired, unaligned physio buffer crosses both user pages. */
	struct proc io_proc;
	struct buf io_buf;
	memset(&io_proc, 0, sizeof(io_proc));
	memset(&io_buf, 0, sizeof(io_buf));
	io_proc.p_vmspace = parent;
	io_buf.b_proc = &io_proc;
	io_buf.b_flags = B_PHYS | B_READ;
	io_buf.b_data = (void *)cross;
	CHECK(uvm_vslock(parent, io_buf.b_data, sizeof(source), VM_PROT_WRITE) == 0);
	CHECK(pmap_wired_count(pm) == 2);
	CHECK(vmapbuf(&io_buf, sizeof(source)) == 0);
	CHECK(memcmp(io_buf.b_data, source, sizeof(source)) == 0);
	((char *)io_buf.b_data)[0] = 'I';
	CHECK(COPY(parent, cross, dest, sizeof(dest), false) == 0 && dest[0] == 'I');
	((char *)io_buf.b_data)[0] = source[0];
	vunmapbuf(&io_buf, sizeof(source));
	CHECK(io_buf.b_data == (void *)cross && io_buf.b_saveaddr == NULL);
	uvm_vsunlock(parent, io_buf.b_data, sizeof(source));
	CHECK(pmap_wired_count(pm) == 0);
	io_buf.b_data = (void *)(base + 2 * PAGE_SIZE - 1);
	CHECK(vmapbuf(&io_buf, 2) == EFAULT); /* Remove a partially built alias. */
	CHECK(io_buf.b_data == (void *)(base + 2 * PAGE_SIZE - 1));
	CHECK(io_buf.b_saveaddr == NULL);
	CHECK(vmapbuf(&io_buf, VM_PHYS_SIZE) == EINVAL);
	CHECK(pmap_resident_count(pm) == 2);
	CHECK(pmap_extract(pm, cross, &pa));
	CHECK((pa & PAGE_MASK) == (cross & PAGE_MASK));
	CHECK(!pmap_extract(pm, VM_KERNEL_VM_BASE, NULL));
	pte = rvm_user_pte(pm, base);
	CHECK(pte != NULL && !(*pte & RVM_PTE_SUPERVISOR));
	pg = PHYS_TO_VM_PAGE(trunc_page(pa));
	CHECK(pg != NULL && pmap_is_referenced(pg) && pmap_is_modified(pg));
	(void)pmap_clear_modify(pg);
	(void)pmap_clear_reference(pg);
	CHECK(!pmap_is_modified(pg) && !pmap_is_referenced(pg));
	CHECK(COPY(parent, cross, dest, sizeof(dest), false) == 0);
	CHECK(pmap_is_referenced(pg) && !pmap_is_modified(pg));
	CHECK(memcmp(source, dest, sizeof(dest)) == 0);
	CHECK(rvm_copyuser(parent, cross, dest, sizeof(dest), false, true, &done) == 0);
	CHECK(done == sizeof(source));
	CHECK(rvm_copyuser(parent, cross, dest, 3, false, true, &done) == ENAMETOOLONG);
	CHECK(done == 3);
	CHECK(COPY(parent, base - PAGE_SIZE, dest, 1, false) == EFAULT);
	CHECK(COPY(parent, 0, dest, 1, false) == EFAULT);
	CHECK(COPY(parent, VM_KERNEL_VM_BASE, dest, 1, false) == EFAULT);
	CHECK(COPY(parent, VM_MAXUSER_ADDRESS - 1, dest, 2, false) == EFAULT);
	CHECK(COPY(parent, (vaddr_t)-4, dest, 8, false) == EFAULT);
	CHECK(COPY(parent, 0, dest, 0, false) == 0);
	CHECK(rvm_copyuser(parent, base + 2 * PAGE_SIZE - 3, dest, 8,
	    false, false, &done) == EFAULT && done == 3);
	/* String copies must stop at NUL even when maxlen extends past user VA. */
	vaddr_t lastpage = VM_MAXUSER_ADDRESS - PAGE_SIZE;
	CHECK(uvm_map(&parent->vm_map, &lastpage, PAGE_SIZE, NULL,
	    UVM_UNKNOWN_OFFSET, 0, UVM_MAPFLAG(UVM_PROT_RW, UVM_PROT_RW,
	    UVM_INH_COPY, UVM_ADV_NORMAL, UVM_FLAG_FIXED | UVM_FLAG_COPYONW)) == 0);
	char terminator = 0;
	CHECK(COPY(parent, VM_MAXUSER_ADDRESS - 1, &terminator, 1, true) == 0);
	CHECK(rvm_copyuser(parent, VM_MAXUSER_ADDRESS - 1, dest, sizeof(dest),
	    false, true, &done) == 0 && done == 1 && dest[0] == 0);
	CHECK(rvm_copyuser(parent, VM_MAXUSER_ADDRESS - 1, source, sizeof(source),
	    true, true, &done) == EFAULT && done == 1);
	CHECK(rvm_copyuser(parent, VM_MAXUSER_ADDRESS - 1, dest, sizeof(dest),
	    false, true, &done) == EFAULT && done == 1);
	CHECK(rvm_copyuser(parent, VM_MAXUSER_ADDRESS - 1, &terminator, 1,
	    true, true, &done) == 0 && done == 1);

	CHECK(uvm_map_protect(&parent->vm_map, base, base + PAGE_SIZE,
	    VM_PROT_READ, false) == 0);
	CHECK(COPY(parent, base, source, 1, true) == EFAULT);
	CHECK(uvm_map_protect(&parent->vm_map, base, base + PAGE_SIZE,
	    UVM_PROT_RW, false) == 0);
	CHECK(COPY(parent, base, source, 1, true) == 0);
	child = uvmspace_fork(parent);
	CHECK(child != NULL);
	CHECK(COPY(child, cross, dest, sizeof(dest), false) == 0);
	CHECK(memcmp(source, dest, sizeof(dest)) == 0);
	dest[0] = 'C';
	CHECK(COPY(child, cross, dest, sizeof(dest), true) == 0);
	CHECK(pmap_extract(child->vm_map.pmap, cross, &child_pa));
	CHECK(trunc_page(child_pa) != trunc_page(pa));
	CHECK(COPY(parent, cross, dest, sizeof(dest), false) == 0);
	CHECK(memcmp(source, dest, sizeof(dest)) == 0);
	/* Reverse-page protection must cover both process maps and retain history. */
	pmap_t aliases = pmap_create();
	CHECK(pmap_enter(aliases, VM_MIN_ADDRESS, trunc_page(pa), UVM_PROT_RW,
	    PMAP_CANFAIL | PMAP_WIRED) == 0);
	CHECK(pmap_enter(aliases, VM_MAXUSER_ADDRESS - PAGE_SIZE, trunc_page(pa),
	    UVM_PROT_RW, PMAP_CANFAIL) == 0);
	CHECK(pmap_resident_count(aliases) == 2 && pmap_wired_count(aliases) == 1);
	/* Replacing a VA must detach its old reverse mapping and preserve counts. */
	CHECK(pmap_enter(aliases, VM_MIN_ADDRESS, trunc_page(child_pa), UVM_PROT_RW,
	    PMAP_CANFAIL) == 0);
	CHECK(pmap_resident_count(aliases) == 2 && pmap_wired_count(aliases) == 0);
	CHECK(pmap_enter(aliases, VM_MIN_ADDRESS, trunc_page(pa), UVM_PROT_RW,
	    PMAP_CANFAIL | PMAP_WIRED) == 0);
	pmap_page_protect(pg, VM_PROT_READ);
	CHECK(!(*rvm_user_pte(pm, base) & RVM_PTE_WRITE));
	CHECK(!(*rvm_user_pte(aliases, VM_MIN_ADDRESS) & RVM_PTE_WRITE));
	CHECK(!(*rvm_user_pte(aliases, VM_MAXUSER_ADDRESS - PAGE_SIZE) & RVM_PTE_WRITE));
	pmap_unwire(aliases, VM_MIN_ADDRESS);
	CHECK(pmap_wired_count(aliases) == 0);
	pmap_page_protect(pg, VM_PROT_NONE);
	CHECK(!pmap_extract(pm, base, NULL));
	CHECK(pmap_resident_count(aliases) == 0 && pg->mdpage.pv_head == NULL);
	(void)pmap_remove_all(aliases);
	pmap_update(aliases);
	pmap_destroy(aliases);
	pmap_reference(pm);
	pmap_destroy(pm);
	uvmspace_free(child);
	/* Install a tiny copied instruction stream in a separate executable page. */
	vaddr_t code = 0x400000;
	size_t code_size = rvm_user_probe_end - rvm_user_probe_code;
	CHECK(code_size > 0 && code_size < PAGE_SIZE);
	CHECK(uvm_map(&parent->vm_map, &code, PAGE_SIZE, NULL,
	    UVM_UNKNOWN_OFFSET, 0, UVM_MAPFLAG(UVM_PROT_ALL, UVM_PROT_ALL,
	    UVM_INH_COPY, UVM_ADV_NORMAL, UVM_FLAG_FIXED | UVM_FLAG_COPYONW)) == 0);
	CHECK(COPY(parent, code, rvm_user_probe_code, code_size, true) == 0);
	CHECK(uvm_map_protect(&parent->vm_map, code, code + PAGE_SIZE,
	    UVM_PROT_RX, false) == 0);
	CHECK(COPY(parent, code, source, 1, true) == EFAULT);
	CHECK(COPY(parent, base + 2 * PAGE_SIZE - 8, source, 8, true) == 0);
	CHECK(COPY(parent, base, dest, 1, false) == 0);
	unsigned int guard = rvm_intr_suspend();
	void (*oldvector)(void) = rvm_boot_vectors[RVM_VECTOR_SYSCALL];
	probe_root = pm->pm_root;
	probe_calls = 0;
	rvm_boot_vectors[RVM_VECTOR_SYSCALL] = rvm_user_probe_entry;
	rvm_user_probe(probe_root, code, base + 2 * PAGE_SIZE);
	rvm_boot_vectors[RVM_VECTOR_SYSCALL] = oldvector;
	CHECK(probe_calls == 2);
	probe_fault(code + (rvm_user_probe_kernel_read - rvm_user_probe_code),
	    base + 2 * PAGE_SIZE, RVM_VECTOR_MMU_ENABLE, RVM_EF_VERSION_MMU_FAULT,
	    VM_KERNEL_VM_BASE);
	probe_fault(code + (rvm_user_probe_text_write - rvm_user_probe_code),
	    base + 2 * PAGE_SIZE, RVM_VECTOR_MMU_WRITE, RVM_EF_VERSION_MMU_FAULT, code);
	probe_fault(base, base + 2 * PAGE_SIZE, RVM_VECTOR_MMU_EXEC,
	    RVM_EF_VERSION_MMU_FAULT, base);
	probe_fault(code + (rvm_user_probe_privilege - rvm_user_probe_code),
	    base + 2 * PAGE_SIZE, RVM_VECTOR_PRIVILEGE, RVM_EF_VERSION_TEMP, 0);
	probe_root = 0;
	rvm_intr_resume(guard);
	/* Repeated fault/fork/exit must return table and anonymous data pages. */
	unsigned int free_before = uvmexp.free;
	for (unsigned int iteration = 0; iteration < 8; iteration++) {
		child = uvmspace_fork(parent);
		CHECK(child != NULL);
		CHECK(COPY(child, cross, source, sizeof(source), true) == 0);
		uvmspace_free(child);
	}
	/* Allow a few pool/cache pages to be retained, but not leaked tables. */
	CHECK(uvmexp.free + 8 >= free_before);
	uvmspace_free(parent);
	CHECK(pg->mdpage.pv_head == NULL);
	printf("RVM normal kernel: user pmaps, demand-zero, copies, protection and COW verified\n");
	printf("RVM normal kernel: user-mode execution, user stack and syscall/RTE roundtrip verified\n");
	printf("RVM normal kernel: user kernel isolation, read-only text, NX and privilege faults verified\n");
	printf("RVM normal kernel: wired user I/O buffers and partial mapping rollback verified\n");
	printf("RVM normal kernel: integer contexts, ptrace registers and privileged-state rejection verified\n");
}

/* Exercise actual MMU user fetches, USP/SSP switching, SYSCALL and RTE. */
void
rvm_user_probe_dispatch(struct rvm_exceptionframe *frame)
{
	CHECK(probe_root != 0 && frame->root == probe_root);
	CHECK(USERMODE(frame->header));
	if (probe_vector != 0) {
		CHECK(((frame->header >> 32) & 0xffff) == probe_vector);
		CHECK((frame->header >> 48) == probe_version);
		if (probe_version == RVM_EF_VERSION_MMU_FAULT)
			CHECK(((struct rvm_copyfault_frame *)frame)->virtual_address ==
			    probe_fault_address);
		CHECK(probe_calls++ == 0);
		frame->header |= PSL_S;
		frame->pc = (vaddr_t)rvm_user_probe_return;
		return;
	}
	CHECK((frame->header >> 48) == RVM_EF_VERSION_SYSCALL);
	CHECK(((frame->header >> 32) & 0xffff) == RVM_VECTOR_SYSCALL);
	CHECK(frame->r[1] == UINT64_C(0x123456789abcdef0));
	CHECK(frame->r[2] == frame->r[1]);
	CHECK(probe_calls < 2);
	if (probe_calls++ == 0) {
		CHECK(frame->r[0] == UINT64_C(0x52564d5553455201));
		check_context(frame);
		frame->r[0] = UINT64_C(0x52564d5553455202);
	} else {
		CHECK(frame->r[0] == UINT64_C(0x52564d5553455203));
		frame->header |= PSL_S;
		frame->pc = (vaddr_t)rvm_user_probe_return;
	}
}
