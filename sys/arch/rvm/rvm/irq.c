/* Timer IRQs: one enabled line, alternating one-shots, no scheduling in handler. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lwp.h>
#include <sys/cpu.h>
#include <machine/boot.h>
#include <machine/pcb.h>
#include <machine/pmap.h>
#include <machine/frame.h>
#include <machine/irq.h>
#include <machine/generic.h>
#include <machine/bootclock.h>

static unsigned int phase __attribute__((section(".data"))) = 0;
static unsigned int halted __attribute__((section(".data"))) = 0;
static unsigned int early __attribute__((section(".data"))) = 0;
static unsigned int clock_running __attribute__((section(".data"))) = 0;
static unsigned int clock_waiting __attribute__((section(".data"))) = 0;
static unsigned int next_channel __attribute__((section(".data"))) = 1;
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
	if (!clock_context() || phase != 3 || clock_running) return 0;
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
