#!/usr/bin/env python3
"""Select MI timecounter publication/read logic; static UP bootstrap storage."""
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
start = source.index('struct timehands {')
end = source.index('\n};', start) + 3
header = '''
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/mutex.h>
#include <sys/lock.h>
#include <sys/atomic.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <sys/syslog.h>
#include <machine/clock.h>
#include <machine/asyncirq.h>
#include <machine/boot.h>
#include <machine/pmap.h>
#include <machine/psl.h>
#include <machine/boottime.h>
#include <machine/bootclock.h>
#define LARGE_STEP 200
#define TC_COUNT(var) do { } while (0)
#define aprint_verbose(...) do { } while (0)
#undef bootverbose
#define bootverbose 0
#define tc_init rvm_mi_tc_init
#define tc_setclock rvm_mi_tc_setclock
#define timestepwarnings 0
'''
header += source[start:end] + '''
static struct timehands hands[10] __attribute__((section(".data"))) = { 0 };
static struct timehands *volatile timehands __attribute__((section(".data"))) = NULL;
struct timecounter *timecounter __attribute__((section(".data"))) = NULL;
static struct timecounter *timecounters __attribute__((section(".data"))) = NULL;
static int timecounter_removals __attribute__((section(".data"))) = 1;
static u_int timecounter_mods __attribute__((section(".data"))) = 0;
static u_int timecounter_bad __attribute__((section(".data"))) = 0;
static int tc_tick __attribute__((section(".data"))) = 1;
kmutex_t timecounter_lock __attribute__((section(".data"))) = { 0 };
volatile time_t time__second __attribute__((section(".data"))) = 0;
volatile time_t time__uptime __attribute__((section(".data"))) = 0;
time_t time_second_legacy asm("time_second") __attribute__((section(".data"))) = 0;
static struct { struct bintime bin; volatile unsigned gen; }
    timebase __attribute__((section(".data"))) = { { 0, 0 }, 0 };
static void tc_windup(void);
static void rvm_no_ntp(int64_t *adjustment, time_t *seconds __unused)
{ *adjustment = 0; }
#define ntp_update_second rvm_no_ntp
static u_int rvm_tc_swap(volatile u_int *p, u_int val)
{ u_int old = *p; *p = val; return old; }
#define atomic_swap_uint rvm_tc_swap
'''
parts = []
# The first setrealuptime body is the native 64-bit load/store variant.
for signature in ('static inline void\nsetrealuptime(', 'static inline u_int\ntc_delta(',
                  'void\nbinuptime(', 'void\nnanouptime(', 'void\nmicrouptime(',
                  'void\ngetbinuptime(', 'void\ngetnanouptime(', 'void\ngetmicrouptime(',
                  'void\ngetbinboottime(', 'void\ngetnanoboottime(', 'void\ngetmicroboottime(',
                  'void\nbintime(', 'void\nnanotime(', 'void\nmicrotime(',
                  'void\ngetbintime(', 'static inline void\ndogetnanotime(',
                  'void\ngetnanotime(', 'void\ngetmicrotime(',
                  'static void\ntc_pick(', 'void\ntc_init(', 'uint64_t\ntc_getfrequency(',
                  'static void\ntc_windup(', 'void\ntc_ticktock(', 'void\ntc_setclock('):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    parts.append(source[start:end])
footer = '\n#undef tc_init\n#undef tc_setclock\n#include "' + str(Path(sys.argv[3]).resolve()) + '"\n'
Path(sys.argv[2]).write_text(source[:source.index('#include <sys/cdefs.h>')] + header + '\n'.join(parts) + footer)
