/* Supervisor accesses always use MST. Translate user addresses explicitly,
 * faulting through UVM for demand allocation and copy-on-write. With one CPU,
 * no kernel preemption and caches disabled, a resident page cannot disappear
 * between the permission check and the guarded physical copy.
 */
#define __UFETCHSTORE_PRIVATE
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <uvm/uvm.h>
#include <machine/mmu.h>
#include <machine/pmap_private.h>

int
rvm_copyuser(struct vmspace *vm, vaddr_t user, void *kernel, size_t len,
    bool out, bool string, size_t *done)
{
	vm_prot_t prot = out ? VM_PROT_WRITE : VM_PROT_READ;
	unsigned long permission = out ? RVM_PTE_WRITE : RVM_PTE_READ;
	unsigned char *buffer = kernel;
	size_t copied = 0;
	int error = 0;
	bool terminated = false;
	if (done != NULL) *done = 0;
	if (len == 0) return string ? ENAMETOOLONG : 0;
	if (user < VM_MIN_ADDRESS || user >= VM_MAXUSER_ADDRESS ||
	    (!string && len > VM_MAXUSER_ADDRESS - user)) return EFAULT;
	while (copied < len) {
		if (user >= VM_MAXUSER_ADDRESS) { error = EFAULT; break; }
		volatile uint64_t *pte = rvm_user_pte(vm->vm_map.pmap, user);
		size_t amount = MIN(len - copied, PAGE_SIZE - (user & PAGE_MASK));
		paddr_t pa;
		if (pte == NULL || (*pte & (RVM_PTE_VALID | permission)) !=
		    (RVM_PTE_VALID | permission)) {
			error = uvm_fault(&vm->vm_map, trunc_page(user), prot);
			if (error != 0) { error = EFAULT; break; }
			pte = rvm_user_pte(vm->vm_map.pmap, user);
		}
		if (pte == NULL || (*pte & (RVM_PTE_VALID | permission | RVM_PTE_SUPERVISOR)) !=
		    (RVM_PTE_VALID | permission)) { error = EFAULT; break; }
		pa = (*pte & ~PAGE_MASK) | (user & PAGE_MASK);
		if (string) {
			unsigned char text[256];
			/* Inspect a bounded chunk before writing it. Neither source
			 * nor destination crosses a page, and bytes following NUL
			 * are never written. In particular, do not touch the next
			 * page merely because the caller supplied ARG_MAX as len.
			 */
			amount = MIN(amount, sizeof(text));
			amount = MIN(amount, PAGE_SIZE -
			    (((uintptr_t)buffer + copied) & PAGE_MASK));
			error = kcopy(out ? buffer + copied : (void *)(uintptr_t)pa,
			    text, amount);
			if (error != 0) break;
			for (size_t i = 0; i < amount; i++) {
				if (text[i] == '\0') {
					amount = i + 1;
					terminated = true;
					break;
				}
			}
			error = kcopy(text, out ? (void *)(uintptr_t)pa : buffer + copied, amount);
		} else {
			error = out ? kcopy(buffer + copied, (void *)(uintptr_t)pa, amount) :
			    kcopy((void *)(uintptr_t)pa, buffer + copied, amount);
		}
		if (error != 0) break;
		*pte |= RVM_PTE_USED_READ | (out ? RVM_PTE_USED_WRITE : 0);
		copied += amount; user += amount;
		if (terminated) break;
	}
	if (done != NULL) *done = copied;
	if (error == 0 && string && !terminated)
		error = ENAMETOOLONG;
	return error;
}
int copyin(const void *user, void *kernel, size_t len)
{
	return rvm_copyuser(curlwp->l_proc->p_vmspace, (vaddr_t)user,
	    kernel, len, false, false, NULL);
}
int copyout(const void *kernel, void *user, size_t len)
{
	return rvm_copyuser(curlwp->l_proc->p_vmspace, (vaddr_t)user,
	    __UNCONST(kernel), len, true, false, NULL);
}
int copyinstr(const void *user, void *kernel, size_t len, size_t *done)
{
	return rvm_copyuser(curlwp->l_proc->p_vmspace, (vaddr_t)user,
	    kernel, len, false, true, done);
}
int copyoutstr(const void *kernel, void *user, size_t len, size_t *done)
{
	return rvm_copyuser(curlwp->l_proc->p_vmspace, (vaddr_t)user,
	    __UNCONST(kernel), len, true, true, done);
}
#define USER_ACCESS(bits, type) \
int _ufetch_##bits(const type *p, type *v) { return copyin(p, v, sizeof(*v)); } \
int _ustore_##bits(type *p, type v) { return copyout(&v, p, sizeof(v)); }
USER_ACCESS(8, uint8_t)
USER_ACCESS(16, uint16_t)
USER_ACCESS(32, uint32_t)
USER_ACCESS(64, uint64_t)
