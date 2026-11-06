#!/usr/bin/env python3
"""Select the real MI idle loop for the UP, IRQ-off bootstrap scheduler.

Running-cpuset bookkeeping and background UVM work await normal kernel startup.
The CPU idle boundary can poll a bootstrap event source, then executes HALT.
"""
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
start = source.index('void\nidle_loop(void *dummy)\n{')
end = source.index('\n}\n', start) + 3
license_end = source.index('#include <sys/cdefs.h>')
header = '''
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/lockdebug.h>
#include <machine/bootsched.h>
#include <machine/bootclock.h>
#define kcpuset_running 0
#define kcpuset_atomic_set(set, index) do { } while (0)

static void rvm_idle_background(void) { }
#define binuptime rvm_bootclock_binuptime
#define sched_idle rvm_idle_background
#define uvm_idle rvm_idle_background
#define lwp_lock rvm_bootsched_idle_lock
#define lwp_unlock rvm_bootsched_idle_unlock
#define mi_switch rvm_bootsched_idle_switch
'''
Path(sys.argv[2]).write_text(source[:license_end] + header + source[start:end])
