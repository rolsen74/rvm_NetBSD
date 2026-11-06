#!/usr/bin/env python3
"""Select unchanged MI switch/next-LWP bodies for IRQ-off kernel scheduling.

Run queues use selected real UP operations. Soft interrupts, timers
and tracing remain unavailable. Directed and queue-selected switches are
supported. Normal kernel builds link kern_synch.c instead.
"""
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
start = source.index('void\nmi_switch(lwp_t *l)\n{')
end = source.index('\n}\n', start) + 3
license_end = source.index('\n */', source.index('/*-')) + 4
header = '''
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/cpu.h>
#include <sys/proc.h>
#include <sys/lwpctl.h>
#include <sys/sched.h>
#include <sys/lockdebug.h>
#include <sys/pserialize.h>
#include <sys/pcu.h>
#include <sys/syscall_stats.h>
#include <dev/lockstat.h>
#include <machine/bootsched.h>
#include <machine/bootclock.h>
/* These are private mutex details in a normal kernel. */
#define MUTEX_SPIN_OLDSPL(ci) ((ci)->ci_mtx_oldspl)
#define dtrace_vtime_active 0
#define dtrace_vtime_switch_func rvm_no_lwp
static struct lwp *rvm_no_softint(void)
{ rvm_bootsched_unavailable(); }
static void rvm_no_lwp(struct lwp *l __unused)
{ rvm_bootsched_unavailable(); }
static void __dead rvm_backwards_time(void)
{
    extern const char rvm_runtime_bad[];
    panic(rvm_runtime_bad);
}
/* Avoid the unsupported long diagnostic varargs path in this profile. */
#define snprintf(buf, len, fmt, ...) ((void)(buf), rvm_backwards_time())
#define printf(...) rvm_backwards_time()

#define softint_picklwp rvm_no_softint
#define softint_block rvm_no_lwp
#define sched_nextlwp rvm_bootsched_pick
#define lwp_unlock rvm_bootsched_lwp_unlock
#define binuptime rvm_bootclock_binuptime
#define pmap_activate rvm_bootsched_vmcheck
#define pmap_deactivate rvm_bootsched_vmcheck
#define mutex_spin_exit rvm_bootsched_unlock
'''
runtime_start = source.index('void\nupdatertime(')
runtime_end = source.index('\n}\n', runtime_start) + 3
next_start = source.index('static inline lwp_t *\nnextlwp(')
next_end = source.index('\n}\n', next_start) + 3
Path(sys.argv[2]).write_text(source[:license_end] + header +
    source[runtime_start:runtime_end] + source[next_start:next_end] + source[start:end])
