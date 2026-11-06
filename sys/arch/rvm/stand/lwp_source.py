#!/usr/bin/env python3
"""Compile the unchanged MI first-entry body without the rest of kern_lwp.c.

The standalone harness has no process table or scheduler yet. Unsupported VM
activation is fatal; kernel LWPs use a process with no private vmspace.
"""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
start = source.index('void\nlwp_startup(struct lwp *prev, struct lwp *new_lwp)\n{')
end = source.index('\n}\n', start) + 3
license_end = source.index('\n */', source.index('/*-')) + 4
header = '''
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/mutex.h>
#include <sys/lockdebug.h>
#include <sys/sdt.h>
#include <uvm/uvm.h>
static void rvm_startup_no_user_vm(struct lwp *l __unused)
{
    extern const char rvm_lwp_bad[];
    panic(rvm_lwp_bad);
}
#define pmap_activate rvm_startup_no_user_vm
'''
parts = [source[start:end]]
for signature in ('pri_t\nlwp_eprio(', 'kmutex_t *\nlwp_setlock('):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    parts.append(source[start:end])
Path(sys.argv[2]).write_text(source[:license_end] + header + '\n'.join(parts))
