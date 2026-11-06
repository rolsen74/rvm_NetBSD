#!/usr/bin/env python3
"""Select MI hardclock/getticks with explicit bootstrap clock-service hooks."""
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
header = '''
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/callout.h>
#include <sys/sched.h>
#include <sys/dkstat.h>
#include <machine/irq.h>
#include <machine/bootclock.h>
extern const char rvm_hardclock_bad[];
static int hardclock_ticks __attribute__((section(".data"))) = 0;
static int stathz __attribute__((section(".data"))) = 0;
int schedhz __attribute__((section(".data"))) = 0;
static int hardscheddiv __attribute__((section(".data"))) = RVM_CLOCK_HZ / 16;
#define clockrnd_sample(source) do { } while (0)
static void rvm_no_ptimer(struct lwp *l, int user)
{
    if (user || l == NULL || !(l->l_flag & LW_SYSTEM))
        panic(rvm_hardclock_bad);
}
static void rvm_system_stats(struct clockframe *frame)
{
    curcpu()->ci_schedstate.spc_cp_time[
        curlwp->l_flag & LW_IDLE ? CP_IDLE : CP_SYS]++;
}
static void rvm_no_schedclock(struct lwp *l) { (void)l; }
static void rvm_no_schedtick(struct cpu_info *ci)
{
    ci->ci_schedstate.spc_ticks = RVM_CLOCK_HZ / 10;
}
#define ptimer_tick rvm_no_ptimer
#define statclock rvm_system_stats
#define schedclock rvm_no_schedclock
#define sched_tick rvm_no_schedtick
#define tc_ticktock rvm_bootclock_ticktock
#define heartbeat() do { } while (0)
'''
parts = []
for signature in ('int\ngetticks(', 'void\nhardclock('):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    parts.append(source[start:end])
Path(sys.argv[2]).write_text(source[:source.index('#include <sys/cdefs.h>')] + header + '\n'.join(parts))
