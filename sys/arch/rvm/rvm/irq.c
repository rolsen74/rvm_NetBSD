/* Timer IRQs: one enabled line, alternating one-shots, no scheduling in handler. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lwp.h>
#include <sys/cpu.h>
#include <machine/boot.h>
#include <machine/pcb.h>
#include <machine/pmap.h>
#include <machine/mmu.h>
#include <machine/frame.h>
#include <machine/irq.h>
#include <machine/generic.h>
#include <machine/bootclock.h>
#include <machine/asyncirq.h>

static unsigned int phase __attribute__((section(".data"))) = 0;
static unsigned int halted __attribute__((section(".data"))) = 0;
static unsigned int early __attribute__((section(".data"))) = 0;
static unsigned int clock_running __attribute__((section(".data"))) = 0;
static unsigned int clock_waiting __attribute__((section(".data"))) = 0;
static volatile unsigned int next_channel __attribute__((section(".data"))) = 1;
static uint64_t clock_ticks __attribute__((section(".data"))) = 0;
static uint64_t clock_events[2] __attribute__((section(".data"))) = { 0 };
extern char rvm_clock_halt_pc[], rvm_clock_after_halt[];
void rvm_timer_interrupt_entry(void);
extern char rvm_timer_halt_pc[], rvm_timer_after_halt[];
const char rvm_irq_bad[] = "Invalid RVM timer interrupt frame";

unsigned int rvm_timer_irq_halted(void) { return halted; }
unsigned int rvm_timer_irq_early(void) { return early; }

void
rvm_timer_irq_dispatch(struct rvm_irqframe *irq)
{
	struct rvm_exceptionframe *frame = &irq->exception;
	struct lwp *l = curlwp;
	vaddr_t address = (vaddr_t)irq, base, end;
	volatile struct rvm_general_driver *gd = (void *)RVM_GENERIC_ADDR;
	uint64_t header = frame->header;
	unsigned int channel = clock_running ? next_channel : 0;
	unsigned int number = channel == 0 ? RVM_TIMER_IRQ : RVM_TIMER2_IRQ;
	vaddr_t halt_pc = clock_running ? (vaddr_t)rvm_clock_halt_pc : (vaddr_t)rvm_timer_halt_pc;
	vaddr_t after_halt = clock_running ? (vaddr_t)rvm_clock_after_halt : (vaddr_t)rvm_timer_after_halt;
	if ((clock_running ? !clock_waiting : phase != 1) || irq->cr != (RVM_CR_MMU | RVM_CR_INTERRUPT) ||
	    rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() ||
	    PSL_IPL(rvm_boot_get_sr()) != number + 1 ||
	    curcpu()->ci_cpl != IPL_NONE || l == NULL || l != curcpu()->ci_onproc ||
	    l->l_cpu != curcpu() || !(l->l_flag & LW_SYSTEM) ||
	    l->l_addr == NULL || l->l_md.md_pcb != l->l_addr ||
	    (header >> 48) != RVM_EF_VERSION_INTERRUPT ||
	    ((header >> 32) & 0xffff) != 32 + number ||
	    USERMODE(header) || (header & PSL_IPL_MASK) != 0 ||
	    frame->root != pmap_kernel()->pm_root ||
	    (frame->pc != halt_pc && frame->pc != after_halt))
		panic(rvm_irq_bad);
	base = l->l_md.md_kstack_base; end = base + l->l_md.md_kstack_size;
	if (base == 0 || end < base || address < base || address > end ||
	    end - address < sizeof(*irq) || (address & STACK_ALIGNBYTES))
		panic(rvm_irq_bad);
	curcpu()->ci_intr_depth = 1;
	curcpu()->ci_cpl = IPL_HIGH;
	/* ACK is queued to the GUI device thread. Mask this line now so an
	 * asserted request cannot retrigger while that command is in flight.
	 */
	rvm_irq_set_inte(0);
	gd->int_mask = 1UL << channel; /* Device ACK bits differ from CPU IRQ bits. */
	if (!clock_running && (header & PSL_H)) halted++;
	if (frame->pc == halt_pc) {
		if (!clock_running) early++;
		frame->pc = after_halt;
	}
	/* VMEmu saved H before clearing it on interrupt entry. */
	frame->header &= ~PSL_H;
	if (clock_running) {
		rvm_bootclock_intr(frame->pc, frame->header);
		clock_ticks++;
		clock_events[channel]++;
		next_channel ^= 1;
		clock_waiting = 0;
	} else phase = 2;
	curcpu()->ci_cpl = IPL_NONE;
	curcpu()->ci_intr_depth = 0;
}

int
rvm_timer_irq_verify(void)
{
	volatile struct rvm_general_driver *gd = (void *)RVM_GENERIC_ADDR;
	if (rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() ||
	    PSL_IPL(rvm_boot_get_sr()) != 0 ||
	    curcpu()->ci_cpl != IPL_NONE || curlwp == NULL ||
	    curlwp->l_addr == NULL || curcpu()->ci_onproc != curlwp) return 0;
	/* A GUI acknowledgement has no guest readback yet. Repeating this test
	 * could consume the previous pending level before its ACK is processed.
	 * A completed one-shot is idempotent; this is not a periodic clock API.
	 */
	if (phase == 3) return 1;
	if (phase != 0) return 0;
	rvm_irq_set_inte(0);
	rvm_boot_vectors[RVM_TIMER_VECTOR] = rvm_timer_interrupt_entry;
	gd->timer[0].control = 0;
	gd->int_mask = 1;
	gd->timer[0].counter = 0;
	gd->timer[0].reset = 2; /* Two GUI ticks, nominally 40 ms. */
	phase = 1;
	__asm volatile("" ::: "memory");
	gd->timer[0].control = RVM_TIMER_ACTIVE | RVM_TIMER_INTERRUPT;
	rvm_irq_set_inte(1UL << RVM_TIMER_IRQ);
	if (!rvm_timer_irq_probe()) return 0;
	gd->timer[0].control = 0;
	rvm_irq_set_inte(0);
	if (phase != 2 || rvm_boot_get_cr() != RVM_CR_MMU || rvm_irq_get_inte() != 0 ||
	    curcpu()->ci_intr_depth != 0 || curcpu()->ci_cpl != IPL_NONE ||
	    halted + early != 1) return 0;
	phase = 3;
	return 1;
}

/* The GUI consumes ACK and timer writes in FIFO order. After the opposite
 * timer has fired, the previous ACK for this channel has necessarily been
 * consumed. Alternation therefore provides a rearm fence without readback.
 */
static int
clock_context(void)
{
	return rvm_boot_get_cr() == RVM_CR_MMU && !cpu_intr_p() &&
	    PSL_IPL(rvm_boot_get_sr()) == 0 && curcpu()->ci_cpl == IPL_NONE &&
	    curlwp != NULL && curlwp == curcpu()->ci_onproc &&
	    curlwp->l_cpu == curcpu() && (curlwp->l_flag & LW_SYSTEM) &&
	    curlwp->l_addr != NULL && curlwp->l_md.md_pcb == curlwp->l_addr;
}

int rvm_clock_active(void) { return clock_running; }
uint64_t rvm_clock_ticks(void) { return clock_ticks; }
uint64_t rvm_clock_events(unsigned int channel)
{
	return channel < 2 ? clock_events[channel] : 0;
}

int
rvm_clock_start(void)
{
	if (!clock_context() || phase != 3 || clock_running || rvm_async_clock_active()) return 0;
	/* The one-shot startup test finished on Timer 1. Timer 2 is initially
	 * next; preserve next_channel across stop/start as well as each tick.
	 */
	rvm_boot_vectors[RVM_TIMER_VECTOR] = rvm_timer_interrupt_entry;
	rvm_boot_vectors[RVM_TIMER2_VECTOR] = rvm_timer_interrupt_entry;
	rvm_irq_set_inte(0);
	clock_running = 1;
	return 1;
}

int
rvm_clock_wait(void)
{
	volatile struct rvm_general_driver *gd = (void *)RVM_GENERIC_ADDR;
	unsigned int channel, number;
	uint64_t before;
	if (!clock_context() || !clock_running || clock_waiting || rvm_bootclock_busy()) return 0;
	channel = next_channel;
	number = channel == 0 ? RVM_TIMER_IRQ : RVM_TIMER2_IRQ;
	before = clock_ticks;
	gd->timer[channel].control = 0;
	gd->timer[channel].counter = 0;
	gd->timer[channel].reset = 1; /* One GUI tick, nominally 20 ms. */
	clock_waiting = 1;
	__asm volatile("" ::: "memory");
	gd->timer[channel].control = RVM_TIMER_ACTIVE | RVM_TIMER_INTERRUPT;
	rvm_irq_set_inte(1UL << number);
	if (!rvm_clock_probe()) return 0;
	rvm_bootclock_poll();
	return clock_ticks == before + 1 && !clock_waiting &&
	    rvm_boot_get_cr() == RVM_CR_MMU && rvm_irq_get_inte() == 0 &&
	    curcpu()->ci_intr_depth == 0 && curcpu()->ci_cpl == IPL_NONE;
}

int
rvm_clock_stop(void)
{
	volatile struct rvm_general_driver *gd = (void *)RVM_GENERIC_ADDR;
	if (!clock_context() || !clock_running || clock_waiting) return 0;
	rvm_irq_set_inte(0);
	gd->timer[0].control = 0;
	gd->timer[1].control = 0;
	gd->int_mask = 3;
	clock_running = 0;
	return 1;
}

/* Asynchronous clock: independent from the legacy explicit-wait API. */
#include <machine/asyncirq.h>
#include <machine/clock.h>
#include <machine/boottime.h>
#include <machine/bootsched.h>
static volatile unsigned int async_running __attribute__((section(".data"))) = 0;
static volatile unsigned int async_armed __attribute__((section(".data"))) = 0;
static volatile unsigned int async_stopping __attribute__((section(".data"))) = 0;
static volatile unsigned int async_preempt __attribute__((section(".data"))) = 0;
static volatile uint64_t async_events __attribute__((section(".data"))) = 0;
static volatile uint64_t async_ticks __attribute__((section(".data"))) = 0;
static volatile uint64_t async_catchups __attribute__((section(".data"))) = 0;
static uint64_t async_deadline __attribute__((section(".data"))) = 0;
extern char rvm_async_halt_pc[], rvm_async_after_halt[];
void rvm_async_interrupt_entry(void);
void rvm_async_halt(void);
const char rvm_async_bad[] = "Invalid RVM asynchronous interrupt frame";
int rvm_async_clock_active(void) { return async_running; }
uint64_t rvm_async_clock_mask(void)
{ return async_running && async_armed ? 1UL << (next_channel == 0 ? RVM_TIMER_IRQ : RVM_TIMER2_IRQ) : 0; }
uint64_t rvm_async_clock_events(void) { return async_events; }
uint64_t rvm_async_clock_ticks(void) { return async_ticks; }
uint64_t rvm_async_clock_catchups(void) { return async_catchups; }
void rvm_async_preemption(int enabled) { async_preempt = enabled != 0; }
static void
async_arm(void)
{
	volatile struct rvm_general_driver *gd = (void *)RVM_GENERIC_ADDR;
	unsigned int channel = next_channel;
	gd->timer[channel].control = 0;
	gd->timer[channel].counter = 0;
	gd->timer[channel].reset = 1;
	async_armed = 1;
	gd->timer[channel].control = RVM_TIMER_ACTIVE | RVM_TIMER_INTERRUPT;
}
int
rvm_async_clock_start(void)
{
	unsigned int guard = rvm_intr_suspend();
	int ok = 0;
	if (!clock_context() || phase != 3 || clock_running || async_running ||
	    !rvm_timecounter_ready() || rvm_bootclock_busy() || curcpu()->ci_irq_locks != 0)
		goto out;
	rvm_boot_vectors[RVM_TIMER_VECTOR] = rvm_async_interrupt_entry;
	rvm_boot_vectors[RVM_TIMER2_VECTOR] = rvm_async_interrupt_entry;
	async_deadline = rvm_clock_monotonic_ns() + 1000000000U / RVM_CLOCK_HZ;
	async_stopping = async_preempt = 0;
	async_running = 1;
	async_arm();
	ok = 1;
out:
	rvm_intr_resume(guard);
	return ok;
}
void
rvm_async_irq_dispatch(struct rvm_async_irqframe *af)
{
	struct rvm_irqframe *irq = &af->irq;
	struct rvm_exceptionframe *frame = &irq->exception;
	struct lwp *l = curlwp;
	uint64_t header = frame->header, now, count, i;
	unsigned int channel = next_channel;
	unsigned int number = channel == 0 ? RVM_TIMER_IRQ : RVM_TIMER2_IRQ;
	vaddr_t address = (vaddr_t)af, base, end;
	paddr_t pcpa;
	unsigned long flags;
	int oldipl = curcpu()->ci_cpl;
	volatile struct rvm_general_driver *gd = (void *)RVM_GENERIC_ADDR;
	if (!async_running || !async_armed || af->inte != rvm_async_clock_mask() ||
	    irq->cr != (RVM_CR_MMU | RVM_CR_INTERRUPT) || rvm_boot_get_cr() != RVM_CR_MMU ||
	    rvm_irq_get_inte() != 0 || cpu_intr_p() || PSL_IPL(rvm_boot_get_sr()) != number + 1 ||
	    oldipl < IPL_NONE || oldipl >= IPL_SCHED || l == NULL || l != curcpu()->ci_onproc ||
	    l->l_cpu != curcpu() || !(l->l_flag & LW_SYSTEM) || l->l_addr == NULL ||
	    l->l_md.md_pcb != l->l_addr || l->l_md.md_irq_hold == UINT_MAX ||
	    curcpu()->ci_irq_locks != 0 || (l->l_md.md_irq_hold != 0 && !async_stopping && !(l->l_flag & LW_IDLE)) ||
	    (header >> 48) != RVM_EF_VERSION_INTERRUPT ||
	    ((header >> 32) & 0xffff) != 32 + number || USERMODE(header) ||
	    (header & PSL_IPL_MASK) != 0 || frame->root != pmap_kernel()->pm_root ||
	    !rvm_boot_pmap_lookup(frame->pc, &pcpa, &flags) || !(flags & RVM_PTE_EXEC))
		panic(rvm_async_bad);
	base = l->l_md.md_kstack_base; end = base + l->l_md.md_kstack_size;
	if (base == 0 || end < base || address < base || address > end ||
	    end - address < sizeof(*af) || (address & STACK_ALIGNBYTES)) panic(rvm_async_bad);
	if ((header & PSL_H) && frame->pc != (vaddr_t)rvm_async_after_halt &&
	    frame->pc != (vaddr_t)rvm_async_halt_pc) panic(rvm_async_bad);
	if (frame->pc == (vaddr_t)rvm_async_halt_pc)
		frame->pc = (vaddr_t)rvm_async_after_halt;
	frame->header &= ~PSL_H;
	l->l_md.md_irq_hold++;
	curcpu()->ci_intr_depth = 1; curcpu()->ci_cpl = IPL_HIGH;
	gd->int_mask = 1UL << channel;
	async_armed = 0;
	async_events++;
	now = rvm_clock_monotonic_ns();
	if (now >= async_deadline) {
		count = (now - async_deadline) / (1000000000U / RVM_CLOCK_HZ) + 1;
		/* Bound IRQ work after a long masked interval; carry remaining debt. */
		if (count > 64) count = 64;
		if (count > 1) async_catchups += count - 1;
		for (i = 0; i < count; i++) rvm_bootclock_intr(frame->pc, frame->header);
		async_ticks += count;
		curcpu()->ci_want_resched = 1;
		async_deadline += count * (1000000000U / RVM_CLOCK_HZ);
	}
	next_channel ^= 1;
	if (!async_stopping) async_arm();
	curcpu()->ci_cpl = oldipl; curcpu()->ci_intr_depth = 0;
}
void
rvm_async_irq_return(struct rvm_async_irqframe *af)
{
	struct lwp *l = curlwp;
	if (rvm_boot_get_cr() != RVM_CR_MMU || cpu_intr_p() || l == NULL ||
	    l != curcpu()->ci_onproc || l->l_md.md_irq_hold == 0) panic(rvm_async_bad);
	/* Normalize hardware IPL before callbacks/MI switches. CR stays masked,
	 * and the LWP guard prevents splx from enabling it on this return stack. */
	rvm_irq_set_sr(af->irq.exception.header & PSL_FLAG_MASK);
	if (l->l_md.md_irq_hold == 1 && curcpu()->ci_cpl == IPL_NONE &&
	    curcpu()->ci_irq_locks == 0 && l->l_nopreempt == 0 && !async_stopping) {
		rvm_bootclock_poll();
		if (async_preempt) rvm_bootsched_preempt();
	}
	if (curlwp != l || l->l_md.md_irq_hold == 0 || rvm_boot_get_cr() != RVM_CR_MMU)
		panic(rvm_async_bad);
	l->l_md.md_irq_hold--;
	af->inte = rvm_async_clock_mask();
	af->irq.cr = async_running ? RVM_CR_MMU | RVM_CR_INTERRUPT : RVM_CR_MMU;
}
int
rvm_async_clock_stop(void)
{
	uint64_t limit;
	unsigned int guard = rvm_intr_suspend();
	int ok = 0;
	if (!async_running || cpu_intr_p() || rvm_bootclock_busy() ||
	    curcpu()->ci_cpl != IPL_NONE || curlwp->l_nopreempt != 0 ||
	    curcpu()->ci_irq_locks != 0 || curcpu()->ci_mtx_count != 0) goto out;
	async_stopping = 1;
	limit = rvm_clock_monotonic_ns() + 5000000000UL;
	/* Drain one last armed one-shot. Its arrival fences the previous ACK;
	 * do not rearm in the handler. The next start uses the opposite line. */
	rvm_irq_set_inte(rvm_async_clock_mask());
	rvm_irq_set_cr(RVM_CR_MMU | RVM_CR_INTERRUPT);
	while (async_armed && rvm_clock_monotonic_ns() < limit)
		__asm volatile("" ::: "memory");
	rvm_intr_disable();
	if (async_armed) goto out;
	rvm_irq_set_inte(0);
	rvm_bootclock_poll();
	async_running = async_stopping = async_preempt = 0;
	rvm_boot_vectors[RVM_TIMER_VECTOR] = rvm_timer_interrupt_entry;
	rvm_boot_vectors[RVM_TIMER2_VECTOR] = rvm_timer_interrupt_entry;
	ok = 1;
out:
	rvm_intr_resume(guard);
	return ok;
}
void
rvm_async_idle_wait(void)
{
	/* The caller guards idle bookkeeping. Temporarily admit only the armed
	 * clock in the HALT window; callbacks run after returning to CR=8. */
	if (!async_running || rvm_boot_get_cr() != RVM_CR_MMU || !CURCPU_IDLE_P())
		panic(rvm_async_bad);
	rvm_irq_set_inte(rvm_async_clock_mask());
	rvm_async_halt();
	rvm_bootclock_poll();
}
