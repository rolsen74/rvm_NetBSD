/* Real MI condition-variable, timeout and wait-channel lifecycle checks. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/condvar.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/callout.h>
#include <uvm/uvm.h>
#include <machine/asyncirq.h>
#include <machine/bootsched.h>
#include <machine/bootwait.h>
const char rvm_wait_check_bad[] = "MI wait verification failed";
const char rvm_wait_name[] = "rvm-wait";
static struct lwp worker_lwp __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static kmutex_t interlock __attribute__((section(".data"))) = { 0 };
static kcondvar_t condition __attribute__((section(".data"))) = { 0 };
static unsigned int progress __attribute__((section(".data"))) = 0;
static struct lwp *parent __attribute__((section(".data"))) = NULL;
static unsigned int channel __attribute__((section(".data"))) = 0;
static void signal_worker(void *arg)
{
	if (arg != &worker_lwp) panic(rvm_wait_check_bad);
	mutex_enter(&interlock);
	if (parent->l_stat != LSSLEEP || !cv_has_waiters(&condition)) panic(rvm_wait_check_bad);
	progress = 1; cv_signal(&condition); mutex_exit(&interlock);
	rvm_bootsched_finish();
}
int rvm_cv_verify(void)
{
	vaddr_t stack;
	unsigned int guard;
	int64_t pages = curcpu()->ci_counts[CPU_COUNT_FREEPAGES];
	if (!rvm_bootsched_init() || !rvm_bootsched_idle_init(NULL, NULL) || !rvm_wait_init()) return 0;
	parent = curlwp; progress = 0;
	mutex_init(&interlock, MUTEX_DEFAULT, IPL_NONE); cv_init(&condition, rvm_wait_name);
	memset(&worker_lwp, 0, sizeof(worker_lwp));
	stack = uvm_uarea_system_alloc(curcpu()); if (!stack) return 0;
	worker_lwp.l_cpu = curcpu(); worker_lwp.l_proc = &process;
	worker_lwp.l_stat = LSIDL; worker_lwp.l_flag = LW_SYSTEM; worker_lwp.l_pflag = LP_MPSAFE;
	uvm_lwp_setuarea(&worker_lwp, stack);
	uvm_lwp_fork(parent, &worker_lwp, NULL, 0, signal_worker, &worker_lwp);
	if (!rvm_bootsched_ready(&worker_lwp) || !rvm_async_clock_start()) return 0;
	mutex_enter(&interlock);
	while (!progress) cv_wait(&condition, &interlock);
	if (!mutex_owned(&interlock) || cv_has_waiters(&condition)) return 0;
	mutex_exit(&interlock);
	guard = rvm_intr_suspend();
	if (!rvm_async_clock_stop() || worker_lwp.l_stat != LSZOMB || !rvm_bootsched_reap(&worker_lwp)) return 0;
	cv_destroy(&condition); mutex_destroy(&interlock);
	if (!rvm_bootsched_fini()) return 0;
	rvm_intr_resume(guard);
	return curcpu()->ci_counts[CPU_COUNT_FREEPAGES] == pages && kernel_map->nentries == 0;
}
static void signal_timeout(void *arg)
{
	if (arg != &condition || cpu_intr_p()) panic(rvm_wait_check_bad);
	mutex_enter(&interlock); cv_signal(&condition); mutex_exit(&interlock);
}
static int setup_wait(void)
{
	if (!rvm_bootsched_init() || !rvm_bootsched_idle_init(NULL, NULL) || !rvm_wait_init()) return 0;
	mutex_init(&interlock, MUTEX_DEFAULT, IPL_NONE); cv_init(&condition, rvm_wait_name);
	return rvm_async_clock_start();
}
static int finish_wait(void)
{
	unsigned int guard = rvm_intr_suspend();
	if (!rvm_async_clock_stop()) return 0;
	cv_destroy(&condition); mutex_destroy(&interlock);
	if (!rvm_bootsched_fini()) return 0;
	rvm_intr_resume(guard); return 1;
}
int rvm_cv_timeout_verify(void)
{
	callout_t signal;
	int before, result;
	if (!setup_wait()) return 0;
	mutex_enter(&interlock); before = getticks();
	result = cv_timedwait(&condition, &interlock, 3);
	if (result != EWOULDBLOCK || (unsigned)(getticks() - before) < 3 || !mutex_owned(&interlock)) return 0;
	mutex_exit(&interlock);
	callout_init(&signal, CALLOUT_MPSAFE); callout_reset(&signal, 1, signal_timeout, &condition);
	mutex_enter(&interlock); result = cv_timedwait(&condition, &interlock, 5);
	if (result != 0 || !mutex_owned(&interlock) || cv_has_waiters(&condition)) return 0;
	mutex_exit(&interlock); callout_halt(&signal, NULL); callout_destroy(&signal);
	return finish_wait();
}
int rvm_kpause_verify(void)
{
	int before, result;
	if (!setup_wait()) return 0;
	before = getticks(); result = kpause(rvm_wait_name, false, 2, NULL);
	if (result != EWOULDBLOCK || (unsigned)(getticks() - before) < 2) return 0;
	mutex_enter(&interlock); result = kpause(rvm_wait_name, false, 2, &interlock);
	if (result != EWOULDBLOCK || !mutex_owned(&interlock)) return 0;
	mutex_exit(&interlock);
	return finish_wait();
}
static void wake_channel(void *arg)
{
	if (arg != &channel) panic(rvm_wait_check_bad);
	wakeup(&channel);
}
int rvm_wchan_verify(void)
{
	callout_t wake;
	int result;
	if (!setup_wait()) return 0;
	callout_init(&wake, CALLOUT_MPSAFE); callout_reset(&wake, 1, wake_channel, &channel);
	mutex_enter(&interlock); result = mtsleep(&channel, 0, rvm_wait_name, 5, &interlock);
	if (result != 0 || !mutex_owned(&interlock)) return 0;
	mutex_exit(&interlock); callout_halt(&wake, NULL); callout_destroy(&wake);
	mutex_enter(&interlock); result = mtsleep(&channel, PNORELOCK, rvm_wait_name, 2, &interlock);
	if (result != EWOULDBLOCK || mutex_owned(&interlock)) return 0;
	return finish_wait();
}

int rvm_cv_bintime_verify(void)
{
	struct bintime remaining, epsilon = { 0, 0 };
	callout_t signal;
	int result;
	unsigned int n;
	if (!setup_wait()) return 0;
	mutex_enter(&interlock); remaining.sec = 0; remaining.frac = 0;
	result = cv_timedwaitbt(&condition, &interlock, &remaining, &epsilon);
	if (result != EWOULDBLOCK || remaining.sec != 0 || remaining.frac != 0) return 0;
	remaining.frac = 1UL << 62;
	for (n = 0; n < 3 && remaining.frac != 0; n++) {
		result = cv_timedwaitbt(&condition, &interlock, &remaining, &epsilon);
		if (result != EWOULDBLOCK || remaining.sec != 0 || remaining.frac >= (1UL << 62)) return 0;
	}
	if (remaining.sec != 0 || remaining.frac != 0) return 0;
	mutex_exit(&interlock);
	callout_init(&signal, CALLOUT_MPSAFE); callout_reset(&signal, 1, signal_timeout, &condition);
	mutex_enter(&interlock); remaining.sec = 1; remaining.frac = 0;
	result = cv_timedwaitbt(&condition, &interlock, &remaining, &epsilon);
	if (result != 0 || remaining.sec != 0 || remaining.frac == 0 || !mutex_owned(&interlock)) return 0;
	mutex_exit(&interlock); callout_halt(&signal, NULL); callout_destroy(&signal);
	return finish_wait();
}
