/* EARLY-only resident program: real MI process, syscall table, descriptors,
 * blocking sleep, exit and wait. No filesystem/executable-format loader yet.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/wait.h>
#include <sys/exec.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <uvm/uvm.h>
#include <machine/user.h>
#include <machine/process_test.h>
__CTASSERT(sizeof(RVM_PROCESS_MESSAGE) == RVM_PROCESS_MESSAGE_SIZE);
__CTASSERT(SIGUSR1 == RVM_PROCESS_SIGNAL);

void rvm_process_smoke(void);
extern char rvm_process_code[], rvm_process_end[];
extern char rvm_process_message[], rvm_process_message_end[];
vaddr_t rvm_process_handler_address(void);
vaddr_t rvm_process_trampoline_address(void);
#define CHECK(c) do { if (!(c)) panic("RVM process check: " #c); } while (0)

static void
map_anon(vaddr_t base, vsize_t size, vm_prot_t prot)
{
	CHECK(uvm_map(&curproc->p_vmspace->vm_map, &base, size, NULL,
	    UVM_UNKNOWN_OFFSET, 0, UVM_MAPFLAG(prot, prot,
	    UVM_INH_COPY, UVM_ADV_NORMAL, UVM_FLAG_FIXED | UVM_FLAG_COPYONW)) == 0);
}

static void
process_start(void *arg)
{
	struct lwp *l = arg;
	struct proc *p = l->l_proc;
	struct exec_package package;
	struct timespec sleep_time = { .tv_sec = 0, .tv_nsec = 40000000 };
	size_t size = rvm_process_end - rvm_process_code;
	CHECK(l == curlwp && !(l->l_flag & LW_SYSTEM));
	strlcpy(p->p_comm, "rvm-hello", sizeof(p->p_comm));
	/* Default fatal-signal tests must not attempt a core file before root exists. */
	lim_privatise(p);
	mutex_enter(p->p_lock);
	p->p_rlimit[RLIMIT_CORE].rlim_cur = 0;
	mutex_exit(p->p_lock);
	CHECK(rvm_console_stdio() == 0);
	struct sigaction ignored;
	memset(&ignored, 0, sizeof(ignored));
	ignored.sa_handler = SIG_IGN;
	sigemptyset(&ignored.sa_mask);
	CHECK(sigaction1(l, SIGSYS, &ignored, NULL, NULL, 0) == 0);
	CHECK(size > 0 && size < PAGE_SIZE);
	map_anon(0x400000, PAGE_SIZE, UVM_PROT_ALL);
	map_anon(0x500000, PAGE_SIZE, UVM_PROT_RW);
	map_anon(0x700000, PAGE_SIZE, UVM_PROT_RW);
	map_anon(USRSTACK - PAGE_SIZE, PAGE_SIZE, UVM_PROT_RW);
	CHECK(copyout(rvm_process_code, (void *)0x400000, size) == 0);
	CHECK(copyout(rvm_process_message, (void *)0x500000,
	    rvm_process_message_end - rvm_process_message) == 0);
	CHECK(rvm_process_message_end - rvm_process_message == RVM_PROCESS_MESSAGE_SIZE);
	CHECK(copyout(&sleep_time, (void *)0x500100, sizeof(sleep_time)) == 0);
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
	action.sa_handler = (void *)rvm_process_handler_address();
	sigemptyset(&action.sa_mask);
	CHECK(copyout(&action, (void *)0x500400, sizeof(action)) == 0);
	stack_t altstack = { .ss_sp = (void *)0x700000, .ss_size = PAGE_SIZE, .ss_flags = 0 };
	CHECK(copyout(&altstack, (void *)0x500480, sizeof(altstack)) == 0);
	vaddr_t trampoline = rvm_process_trampoline_address();
	CHECK(copyout(&trampoline, (void *)0x5004a0, sizeof(trampoline)) == 0);
	CHECK(uvm_map_protect(&p->p_vmspace->vm_map, 0x400000,
	    0x400000 + PAGE_SIZE, UVM_PROT_RX, false) == 0);
	/* Leave stack nonresident: its first actual user write must fault via UVM. */
	CHECK(!pmap_extract(p->p_vmspace->vm_map.pmap, USRSTACK - PAGE_SIZE, NULL));
	memset(&package, 0, sizeof(package));
	package.ep_entry = 0x400000;
	setregs(l, &package, USRSTACK);
	/* Returning uses the production user-return path in kernel_switch.c. */
}

void
rvm_process_smoke(void)
{
	struct proc *p = curproc;
	register_t result[2];
	int pid, status;
	unsigned int no_wait;
	unsigned long user_irqs = rvm_user_interrupts();
	unsigned long restarts = rvm_user_restarts();
	/* proc0 normally discards exited children; this test must wait/reap its
	 * child. Restore the original policy after waiting, before rootconf. */
	mutex_enter(p->p_lock);
	no_wait = p->p_flag & PK_NOCLDWAIT;
	p->p_flag &= ~PK_NOCLDWAIT;
	mutex_exit(p->p_lock);
	CHECK(fork1(curlwp, FORK_CLEANFILES, SIGCHLD, NULL, 0,
	    process_start, NULL, result) == 0);
	pid = result[0];
	CHECK(pid > 1);
	CHECK(do_sys_wait(&pid, &status, 0, NULL) == 0);
	CHECK(pid == result[0] && WIFEXITED(status) && WEXITSTATUS(status) == 37);
	CHECK(rvm_user_interrupts() > user_irqs);
	CHECK(rvm_user_restarts() > restarts);
	mutex_enter(p->p_lock);
	p->p_flag |= no_wait;
	mutex_exit(p->p_lock);
	printf("RVM normal kernel: real user process, write, sleep, exit and wait verified\n");
	printf("RVM normal kernel: native syscall errors, user fork/COW/wait and user IRQ return verified\n");
	printf("RVM normal kernel: user signal delivery, alternate stack and setcontext return verified\n");
	printf("RVM normal kernel: fatal user privilege, kernel-access and text-write faults contained\n");
	printf("RVM normal kernel: interrupted syscall restart through signal/setcontext verified\n");
}
