/* Standalone, non-sleeping wired buffers; replaced by UVM km in a full kernel. */
#ifndef _RVM_BOOTALLOC_H_
#define _RVM_BOOTALLOC_H_

#include <sys/types.h>
#include <machine/vmparam.h>

/* Keep the allocator away from the low-window pmap regression probes. */
#define RVM_BOOTALLOC_SIZE (16UL * 1024 * 1024)
#define RVM_BOOTALLOC_BASE \
	(VM_KERNEL_VM_BASE + VM_KERNEL_VM_SIZE - RVM_BOOTALLOC_SIZE)
#define RVM_BOOTALLOC_SLOTS 32

int rvm_bootalloc_init(void);
/* Size rounds up to pages. Returns 0 on invalid size or resource exhaustion.
 * Every successful buffer is zeroed, writable and non-executable.
 */
vaddr_t rvm_bootalloc(vsize_t);
/* Only an exact live base address is accepted; invalid frees return 0. */
int rvm_bootfree(vaddr_t);

#endif
