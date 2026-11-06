/* MD kernel-LWP storage. User trap/syscall integration remains pending. */
#ifndef _RVM_PROC_H_
#define _RVM_PROC_H_

struct trapframe;
struct pcb;

struct mdlwp {
	struct trapframe *md_tf;
	struct pcb *md_pcb;
	vaddr_t md_kstack_base;
	vsize_t md_kstack_size;
	unsigned int md_irq_hold; /* IRQ guard nesting, preserved across switches */
	unsigned int md_startup; /* MI first-entry handoff pending */
};

struct mdproc {
	void (*md_syscall)(struct trapframe *);
};

#endif /* _RVM_PROC_H_ */
