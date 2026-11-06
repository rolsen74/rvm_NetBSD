/* Initial MD process storage. Scheduling and syscall entry remain pending. */
#ifndef _RVM_PROC_H_
#define _RVM_PROC_H_

struct trapframe;
struct pcb;

struct mdlwp {
	struct trapframe *md_tf;
	struct pcb *md_pcb;
	vaddr_t md_kstack_base;
	vsize_t md_kstack_size;
};

struct mdproc {
	void (*md_syscall)(struct trapframe *);
};

#endif /* _RVM_PROC_H_ */
