#!/usr/bin/env python3
"""Select MI sleepq/CV and wait-channel algorithms with checked UP hooks."""
from pathlib import Path
import sys
sq, cv, synch = [Path(p).read_text() for p in sys.argv[1:4]]
header = r'''
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/callout.h>
#include <sys/sleepq.h>
#include <sys/syncobj.h>
#include <sys/signalvar.h>
#include <sys/errno.h>
#include <machine/boot.h>
#include <machine/pmap.h>
#include <sys/sdt.h>
#include <machine/psl.h>
#include <machine/irq.h>
#include <machine/asyncirq.h>
#include <machine/bootclock.h>
#include <machine/bootwait.h>
extern const char rvm_wait_bad[];
static void __dead rvm_wait_unsupported(void) { panic(rvm_wait_bad); }
#define ktrcsw(a, b, c) do { } while (0)
#define lwp_lock rvm_wait_lwp_lock
#define lwp_unlock rvm_wait_lwp_unlock
#define lwp_unlock_to rvm_wait_lwp_unlock_to
#define mi_switch rvm_wait_switch
#define sched_takecpu(l) curcpu()
#define sched_wakeup(l) do { } while (0)
#define sched_slept(l) do { } while (0)
#define sched_setrunnable(l) do { } while (0)
#define sched_resched_lwp rvm_wait_resched
#undef KERNEL_UNLOCK_ALL
#undef KERNEL_LOCK
#define KERNEL_UNLOCK_ALL(l,n) rvm_wait_unsupported()
#define KERNEL_LOCK(n,l) rvm_wait_unsupported()
#define sigispending(l,f) (rvm_wait_unsupported(), 0)
#define issignal(l) (rvm_wait_unsupported(), 0)
#define sleepq_sigtoerror(l,s) (rvm_wait_unsupported(), 0)
#undef sleepq_dontsleep
#define sleepq_dontsleep(l) false
#undef cold
#define cold 0
'''
sqnames = ('sleepq_init', 'sleepq_remove', 'sleepq_enter', 'sleepq_enqueue',
           'sleepq_block', 'sleepq_wake', 'sleepq_unsleep', 'sleepq_timeout',
           'sleepq_changepri', 'sleepq_lendpri')
cvnames = ('cv_init', 'cv_destroy', 'cv_wait', 'cv_timedwait', 'cv_signal',
           'cv_broadcast', 'cv_has_waiters', 'cv_is_valid', 'cv_timedwaitbt')
for name in sqnames + cvnames + ('kpause', 'mtsleep', 'wakeup'):
    header += f'#define {name} rvm_mi_{name}\n'
header += r'''
#define CV_SLEEPQ(cv) ((sleepq_t *)(cv)->cv_opaque)
#define CV_WMESG(cv) ((const char *)(cv)->cv_opaque[1])
#define CV_SET_WMESG(cv,v) (cv)->cv_opaque[1] = __UNCONST(v)
static const char deadcv[] = "deadcv";
static __noinline void cv_wakeup_one(kcondvar_t *);
static __noinline void cv_wakeup_all(kcondvar_t *);
void rvm_cv_unsleep(lwp_t *, bool);
static void sleepq_insert(sleepq_t *, lwp_t *, syncobj_t *);
static void sleepq_reinsert(sleepq_t *, lwp_t *);
bool rvm_mi_cv_has_waiters(kcondvar_t *);
bool rvm_mi_cv_is_valid(kcondvar_t *);
static int rvm_mi_sleepq_block(int, unsigned int, syncobj_t *, int);
'''
def body(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end]
parts = []
for signature in ('void\nsleepq_init(', 'void\nsleepq_remove(',
                  'static void\nsleepq_insert(', 'int\nsleepq_enter(',
                  'void\nsleepq_enqueue(', 'int\nsleepq_block(',
                  'void\nsleepq_wake(', 'void\nsleepq_unsleep(',
                  'void\nsleepq_timeout(', 'static void\nsleepq_reinsert(',
                  'void\nsleepq_changepri(', 'void\nsleepq_lendpri('):
    part = body(sq, signature)
    # Preserve MI bodies; use padded scalar homes in the generated C profile.
    for arg in ('catch_p', 'wakeup', 'unlock'):
        part = part.replace('bool ' + arg, 'unsigned int ' + arg)
    if signature == 'int\nsleepq_block(':
        part = part.replace('int\nsleepq_block(', 'static int\nsleepq_block(', 1)
    parts.append(part)
for signature in ('void\ncv_init(', 'void\ncv_destroy(', 'static inline int\ncv_enter(',
                  'static void\ncv_unsleep(', 'void\ncv_wait(', 'int\ncv_timedwait(',
                  'void\ncv_signal(', 'static __noinline void\ncv_wakeup_one(',
                  'void\ncv_broadcast(', 'static __noinline void\ncv_wakeup_all(',
                  'bool\ncv_has_waiters(', 'bool\ncv_is_valid(',
                  'static unsigned\nbintime2timo(', 'static struct bintime\ntimo2bintime(',
                  'int\ncv_timedwaitbt('):
    part = body(cv, signature).replace('bool catch_p', 'unsigned int catch_p')
    if signature == 'static void\ncv_unsleep(':
        part = part.replace('static void\ncv_unsleep(', 'void\nrvm_cv_unsleep_core(', 1)
        part = part.replace('bool unlock', 'unsigned int unlock')
    parts.append(part)
for signature in ('int\nkpause(', 'int\nmtsleep(', 'void\nwakeup('):
    parts.append(body(synch, signature).replace('bool intr', 'unsigned int intr'))
# These MI calls must enter the checked unsigned-flag boundary, not recurse.
header += 'void rvm_wait_unsleep(struct lwp *, unsigned int);\n#define lwp_unsleep rvm_wait_unsleep\n'
footer = '\n' + '\n'.join('#undef ' + name for name in sqnames + cvnames + ('kpause','mtsleep','wakeup'))
footer += '\n#undef cold\n#undef mi_switch\n#include "' + str(Path(sys.argv[5]).resolve()) + '"\n'
licenses = '\n'.join(s[:s.index('#include <sys/cdefs.h>')] for s in (sq,cv,synch))
Path(sys.argv[4]).write_text(licenses + header + '\n'.join(parts) + footer)
