#!/usr/bin/env python3
"""Select the MI UP slow-softint queues and dispatcher for RVM bring-up.

Normal kernels link kern_softint.c directly. The bounded bootstrap supplies
initialization/teardown and checked scheduler handoffs instead of kthreads,
xcall barriers and the big kernel lock, which are not initialized yet.
"""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
layout = source[source.index('typedef struct softint {'):source.index('static void\tsoftint_thread')]
header = '''
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/intr.h>
#include <sys/mutex.h>
#include <sys/evcnt.h>
#include <sys/psref.h>
#include <sys/sdt.h>
#include <sys/cpu.h>
#include <uvm/uvm.h>
#include <machine/boot.h>
#include <machine/psl.h>
#include <machine/asyncirq.h>
#include <machine/bootsched.h>
#include <machine/bootsoftint.h>
extern const char rvm_softint_bad[];
static void __dead rvm_softint_nobiglock(void) { panic(rvm_softint_bad); }
#undef KERNEL_LOCK
#undef KERNEL_UNLOCK_ONE
#define KERNEL_LOCK(n,l) rvm_softint_nobiglock()
#define KERNEL_UNLOCK_ONE(l) rvm_softint_nobiglock()
#define softint_schedule rvm_mi_softint_schedule
#define softint_schedule_cpu rvm_mi_softint_schedule_cpu
#define softint_establish rvm_mi_softint_establish
#define lwp_lock rvm_softint_lwp_lock
#define mi_switch rvm_softint_switch
static void softint_thread(void *);
static u_int softint_max;
static kmutex_t softint_lock;
u_int softint_bytes;
'''
parts = []
for signature in ('void *\nsoftint_establish(', 'void\nsoftint_schedule(',
                  'static inline void\nsoftint_execute(',
                  'void\nsoftint_block(', 'void\nsoftint_trigger(',
                  'void\nsoftint_thread(', 'lwp_t *\nsoftint_picklwp('):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    parts.append(source[start:end])
footer = '''
#undef softint_establish
#undef softint_schedule
#undef softint_schedule_cpu
#undef lwp_lock
#undef mi_switch
'''
footer += '\n#include "' + str(Path(sys.argv[3]).resolve()) + '"\n'
Path(sys.argv[2]).write_text(source[:source.index('#include "opt_multiprocessor.h"')] + header + layout + '\n'.join(parts) + footer)
