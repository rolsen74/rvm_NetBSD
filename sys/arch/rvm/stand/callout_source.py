#!/usr/bin/env python3
"""Select actual MI timing-wheel and dispatch bodies for one bootstrap CPU."""
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
header = '''
#define _CALLOUT_PRIVATE
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/intr.h>
#include <sys/lwp.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sleepq.h>
#include <sys/evcnt.h>
#include <sys/cpu.h>
#include <sys/sdt.h>
#include <machine/boot.h>
#include <machine/pmap.h>
#include <machine/psl.h>
#include <machine/bootclock.h>
'''
layout_start = source.index('#define BUCKETS')
layout_end = source.index('\n#ifdef DDB', layout_start)
functions = ('callout_halt', 'callout_init', 'callout_destroy', 'callout_reset', 'callout_schedule',
             'callout_stop', 'callout_setfunc', 'callout_expired', 'callout_active',
             'callout_pending', 'callout_invoking', 'callout_ack', 'callout_hardclock')
for name in functions:
    header += f'#define {name} rvm_mi_{name}\n'
header += '''
static void callout_wait(callout_impl_t *c __unused, void *interlock __unused, kmutex_t *lock __unused) { panic(rvm_callout_bad); }
static struct callout_cpu callout_cpu0 __attribute__((section(".data"))) = { 0 };
static unsigned int soft_pending __attribute__((section(".data"))) = 0;
static void *real_softclock __attribute__((section(".data"))) = NULL;
static void *callout_sih __attribute__((section(".data"))) = NULL;
static void rvm_soft_schedule(void *cookie)
{
    if (cookie != callout_sih) panic(rvm_callout_bad);
    if (real_softclock != NULL) softint_schedule(real_softclock);
    else soft_pending = 1;
}
static void rvm_no_sleepq(sleepq_t *sq __unused, wchan_t wc __unused,
    u_int count __unused, kmutex_t *lock __unused)
{ panic(rvm_callout_bad); }
#define softint_schedule rvm_soft_schedule
#define sleepq_wake rvm_no_sleepq
'''
header = header.replace('#define callout_init', 'extern const char rvm_callout_bad[];\n#define callout_init', 1)
# Types and queue macros must precede per-CPU storage.
macro_start = header.index('static struct callout_cpu')
header = header[:macro_start] + source[layout_start:layout_end] + '\n' + header[macro_start:]
parts = []
for signature in ('static inline kmutex_t *\ncallout_lock(',
                  'static inline bool\ncallout_running_somewhere_else(',
                  'static void\ncallout_schedule_locked('):
    start = source.index(signature); end = source.index('\n}\n', start) + 3
    parts.append(source[start:end])
for name in functions + ('callout_softclock',):
    # Definitions have no preceding tabs, unlike prototypes.
    start = source.index('\n' + name + '(')
    start = source.rfind('\n', 0, start) + 1
    end = source.index('\n}\n', start) + 3
    parts.append(source[start:end])
footer = '\n' + '\n'.join('#undef ' + name for name in functions)
footer += '\n#include "' + str(Path(sys.argv[3]).resolve()) + '"\n'
Path(sys.argv[2]).write_text(source[:source.index('#include <sys/cdefs.h>')] + header + '\n'.join(parts) + footer)
