#!/usr/bin/env python3
"""Select real UP run-queue operations; no timers, balancing or preemption."""
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
# Preserve both copyright notices in kern_runq.c.
license_end = source.index('#include <sys/cdefs.h>')
header = '''
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/kernel.h>
#define BITMAP_BITS 32
#define BITMAP_SHIFT 5
#define BITMAP_MSB 0x80000000U
#define BITMAP_MASK (BITMAP_BITS - 1)
#define sched_rrticks 1
const int schedppq = 1;
static void rvm_queue_hook(struct lwp *l __unused) { }
#define sched_newts rvm_queue_hook
#define sched_oncpu rvm_queue_hook
'''
parts = []
for signature in ('static inline void *\nsched_getrq(', 'void\nsched_enqueue(',
                  'void\nsched_dequeue(', 'struct lwp *\nsched_nextlwp(',
                  'bool\nsched_curcpu_runnable_p('):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    parts.append(source[start:end])
# sched_preempted() is an empty hook in the normal UP implementation too.
start = source.rindex('void\nsched_preempted(')
end = source.index('\n}\n', start) + 3
parts.append(source[start:end])
Path(sys.argv[2]).write_text(source[:license_end] + header + '\n'.join(parts))
