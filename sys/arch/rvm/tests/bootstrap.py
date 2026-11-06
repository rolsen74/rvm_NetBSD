#!/usr/bin/env python3
"""Exercise the real RVM loader, compiler, allocator and MMU in VMEmu."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, required=True)
    parser.add_argument('--cross', required=True)
    parser.add_argument('--vmasm', default='VMAsm')
    parser.add_argument('--vmemu', default='VMEmu')
    parser.add_argument('--suite', choices=('new', 'lwpvm', 'uarea', 'runtime', 'timecounter', 'hardclock', 'clock', 'irq', 'trap', 'idle', 'runq', 'switch', 'lwp', 'context', 'full'), default='new',
                        help='new/lwpvm: MI LWP VM lifecycle; uarea: guarded stacks; runtime: compiler and accounting; timecounter: MI uptime; hardclock: ticks and callouts; clock: repeating wakeups; irq/trap/idle/runq/switch/lwp/context: earlier CPU work; full: all regressions')
    args = parser.parse_args()
    obj = args.objdir.resolve()
    root = Path(__file__).resolve().parents[4]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    ready = 'UVM page allocator ready; allocation/free verified.'
    wired_ready = 'Early wired allocator ready; zeroed allocation/free verified.'
    map_ready = 'UVM kernel VA map ready; reservations and wired buffers verified.'
    object_ready = 'UVM resident objects ready; page ownership and wired allocation verified.'
    km_ready = 'UVM kernel allocator ready; wired and VA-only allocation/free verified.'
    heap_ready = 'Bootstrap kmem heap ready; subpage allocation and span release verified.'
    pool_ready = 'Bootstrap MI pools ready; item allocation and page release verified.'
    cache_ready = 'Bootstrap MI pool caches ready; constructors and magazine reuse verified.'
    anon_ready = 'UVM anonymous pages ready; references, zeroing and release verified.'
    amap_ready = 'UVM anonymous maps ready; sparse slots, sharing and release verified.'
    anonmap_ready = 'UVM anonymous mappings ready; VM entries, shared aliases and detach verified.'
    context_ready = 'RVM kernel context switching ready; separate LWP stacks and resume verified.'
    preload_ready = 'UVM physical memory registered; bootstrap allocation verified.'
    compiler = [args.cross + 'gcc',
                '-std=gnu11', '-O2', '-ffreestanding', '-fno-builtin',
                '-fno-stack-protector', '-fno-pic', '-fno-pie',
                '-fno-merge-constants', '-nostdinc', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-D_KERNEL', '-I' + str(obj / 'include'),
                '-I' + str(root / 'sys'), '-I' + str(root / 'common/include')]

    def run(directory, timeout=5):
        try:
            result = subprocess.run([args.vmemu, '-c', 'vm.config'], cwd=directory,
                                    env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=timeout)
            raise AssertionError('VM exited unexpectedly: ' + str(result.returncode)
                                 + '\n' + (result.stdout + result.stderr).decode(
                                     errors='replace')[:12000])
        except subprocess.TimeoutExpired as exc:
            # SDL logs come from the GUI thread and can interrupt a boot
            # line between EmuFunc characters. Keep stderr separate and
            # remove the single host display-info printf before parsing.
            out = exc.stdout.decode(errors='replace')
            out = re.sub(r'VMEmu display: SDL video driver [^\r\n]*\r?\n', '', out)
            return out + '\n' + (exc.stderr or b'').decode(errors='replace')

    def configure(directory, pages):
        config = (obj / 'vm.config').read_text()
        config = re.sub(r'Size = \d+', 'Size = ' + str(pages), config, count=1)
        (directory / 'vm.config').write_text(config)
        shutil.copyfile(obj / 'bootloader.bin', directory / 'bootloader.bin')

    with tempfile.TemporaryDirectory(prefix='netbsd-rvm-test-') as tmp:
        work = Path(tmp)
        def build_wrapper(body, assembly='', preload=False, extra_objects=(), trap_probe=None, irq_skip=False, irq_probe=None, clock_skip=False, clock_probe=None, hardclock_skip=False, timecounter_skip=False, runtime_skip=False):
            source = '#define RVM_TEST_PRELOAD_ONLY\n' if preload else ''
            if irq_skip:
                source += '#define RVM_TEST_IRQ_SKIP\n'
            if clock_skip:
                source += '#define RVM_TEST_CLOCK_SKIP\n'
            if hardclock_skip:
                source += '#define RVM_TEST_HARDCLOCK_SKIP\n'
            if timecounter_skip:
                source += '#define RVM_TEST_TIMECOUNTER_SKIP\n'
            if runtime_skip:
                source += '#define RVM_TEST_RUNTIME_SKIP\n'
            source += '#define rvm_early_init rvm_normal_early_init\n'
            source += '#include "' + str(root / 'sys/arch/rvm/rvm/machdep.c') + '"\n'
            source += '#undef rvm_early_init\n' + body
            (work / 'wrapper.c').write_text(source)
            subprocess.run(compiler + ['-c', str(work / 'wrapper.c'), '-o', str(work / 'machdep.o')], check=True)
            objects = [obj / 'locore.o', obj / 'cpuswitch.o', obj / 'cpu_switch.o', obj / 'vm_machdep.o', obj / 'lwp_startup.o', obj / 'bootsched.o', obj / 'mi_switch.o', obj / 'runq.o', obj / 'idle_loop.o', obj / 'trap.o', trap_probe or obj / 'trap_probe.o', obj / 'irq.o', irq_probe or obj / 'irq_probe.o', clock_probe or obj / 'clock_probe.o', obj / 'bootclock.o', obj / 'hardclock.o', obj / 'callout.o', obj / 'timecounter.o', obj / 'uarea.o', obj / 'lwpvm.o', work / 'machdep.o', obj / 'earlycons.o',
                       obj / 'bootmem.o', obj / 'pmap.o', obj / 'uvm_machdep.o',
                       obj / 'uvm_bootstrap.o', obj / 'uvm_support.o', obj / 'bootalloc.o',
                       obj / 'bootmap.o', obj / 'bootobject.o', obj / 'bootkm.o',
                       obj / 'bootheap.o', obj / 'bootpool.o', obj / 'bootcache.o', obj / 'bootanon.o', obj / 'bootamap.o', obj / 'bootanonmap.o']
            objects += [obj / (name + '.o') for name in
                        ('uvm_physseg', 'uvm_page', 'uvm_extent', 'uvm_clock',
                         'uvm_meter', 'uvm_pgflcache', 'uvm_map', 'rbtree',
                         'uvm_object', 'uvm_page_status', 'radixtree', 'uvm_km', 'vmem', 'pool', 'uvm_anon', 'uvm_amap')]
            objects += list(extra_objects)
            if assembly:
                asm = ' section ".text",code\n xdef rvm_test_probe\nrvm_test_probe:\n' + assembly
                (work / 'probe.vma').write_text(asm)
                subprocess.run([args.vmasm, '-i', str(work / 'probe.vma'),
                                '-o', str(work / 'probe.o'), '--obj'], check=True, stdout=subprocess.DEVNULL)
                objects.append(work / 'probe.o')
            subprocess.run([args.cross + 'ld', '-e', '_start', '-o', str(work / 'netbsd.fhr')]
                           + [str(path) for path in objects], check=True)
            configure(work, 256)

        if args.suite in ('new', 'lwpvm', 'full'):
            vm_ready = 'RVM MI LWP VM lifecycle ready; u-area attachment, fork and exit verified.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert vm_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: final kernel reaches MI LWP VM lifecycle (640 MiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'lwpvm.c').read_text())
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM MI u-area attachment, fork, exit and fallback verified.' in out, out
                print(f'PASS: MI attach/fork/exit, unattached/unstarted cleanup, guarded/plain stacks and reuse ({pages * 64} KiB)', flush=True)
            for operation in (
                'uvm_lwp_setuarea(&first, base);',
                'uvm_lwp_fork(curlwp, &first, NULL, 0, entry, NULL); uvm_lwp_exit(&first);',
                'uvm_lwp_exit(&first); uvm_lwp_exit(&first);',
                'uvm_lwp_setuarea(&second, base);',
            ):
                body = r'''
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <uvm/uvm.h>
static struct lwp first __attribute__((section(".data"))) = { 0 };
static struct lwp second __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static void __used entry(void *arg __unused) { rvm_boot_stop(); }
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
    vaddr_t base = uvm_uarea_system_alloc(NULL);
    first.l_cpu = curcpu(); first.l_proc = &process; first.l_stat = LSIDL;
    first.l_flag = LW_SYSTEM; first.l_pflag = LP_MPSAFE;
    second.l_cpu = curcpu(); second.l_proc = &process; second.l_stat = LSIDL;
    second.l_flag = LW_SYSTEM; second.l_pflag = LP_MPSAFE;
    uvm_lwp_setuarea(&first, base);
''' + operation + '\n}\n'
                build_wrapper(body)
                out = run(work)
                assert vm_ready in out and 'NetBSD/RVM UVM panic: Invalid bootstrap MI LWP u-area lifecycle' in out, out
                print('PASS: MI LWP u-area boundary rejects ' + operation, flush=True)
            if args.suite != 'full':
                return

        if args.suite in ('uarea', 'full'):
            ua_ready = 'RVM guarded u-areas ready; LWP stack ownership and release verified.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert ua_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: final kernel reaches guarded u-area lifecycle (640 MiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'uarea.c').read_text())
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM guarded u-area zeroing, lifetime and reuse verified.' in out, out
                print(f'PASS: guarded u-area capacity, zeroing, PTEs, worker execution, reaping and reuse ({pages * 64} KiB)', flush=True)
            for operation, vector in (
                ('*(volatile uint64_t *)(base - PAGE_SIZE) = 1;', 5),
                ('((void (*)(void))base)();', 2),
            ):
                body = r'''
#include <sys/cpu.h>
#include <uvm/uvm.h>
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
    vaddr_t base = (vaddr_t)cpu_uarea_alloc(true);
''' + operation + '\n}\n'
                build_wrapper(body)
                out = run(work)
                assert ua_ready in out and f'fatal startup exception: vector 0x{vector:016x}' in out, out
                print(f'PASS: guarded stack enforces MMU protection (vector {vector})', flush=True)
            build_wrapper(r'''
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <machine/bootsched.h>
static struct lwp child __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static void entry(void *arg __unused) { cpu_uarea_free(curlwp->l_addr); }
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi); rvm_bootsched_init();
    child.l_addr = cpu_uarea_alloc(true); child.l_cpu = curcpu();
    child.l_proc = &process; child.l_stat = LSIDL;
    child.l_flag = LW_SYSTEM; child.l_pflag = LP_MPSAFE;
    cpu_lwp_fork(curlwp, &child, NULL, 0, entry, NULL);
    rvm_bootsched_ready(&child); rvm_bootsched_yield();
}
''')
            out = run(work)
            assert ua_ready in out and 'NetBSD/RVM UVM panic: Invalid RVM guarded u-area ownership' in out, out
            print('PASS: guarded u-area cannot be freed by its running LWP', flush=True)
            build_wrapper((Path(__file__).parent / 'uarea_lowmem.c').read_text())
            out = run(work)
            assert 'RVM guarded u-area partial allocation rollback verified.' in out, out
            print('PASS: second-page failure rolls back object, pages, PTEs and VA reservation', flush=True)
            if args.suite != 'full':
                return

        if args.suite in ('runtime', 'full'):
            runtime_ready = 'RVM MI runtime accounting ready; running and sleeping LWP time verified.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert runtime_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: final rebuilt kernel reaches MI runtime accounting (640 MiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'division.c').read_text())
            out = run(work)
            assert 'RVM signed/unsigned division input constraints verified.' in out, out
            print('PASS: rebuilt GCC signed/unsigned 32/64-bit immediate and memory division', flush=True)
            build_wrapper((Path(__file__).parent / 'runtime.c').read_text(), runtime_skip=True)
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM MI LWP runtime, sleeping exclusion and idle accounting verified.' in out, out
                print(f'PASS: LWP runtime, sleeping/idle exclusion, CPU tick classification and cleanup ({pages * 64} KiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'timecounter.c').read_text(), timecounter_skip=True)
            configure(work, 1024)
            out = run(work)
            assert 'RVM MI uptime, cached timestamps and worker reads verified.' in out, out
            print('PASS: original immediate-divide timestamp reference after compiler rebuild', flush=True)
            build_wrapper(r'''
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <machine/boottime.h>
#include <machine/bootclock.h>
#include <machine/bootsched.h>
#include <machine/irq.h>
const char rebase_ok[] = "RVM runtime epoch rebase preserves pending time.\n";
void rvm_early_init(const struct rvm_bootinfo *bi) {
    struct bintime expected;
    rvm_normal_early_init(bi);
    if (!rvm_bootsched_init()) rvm_boot_stop();
    rvm_bootsched_yield(); expected = curlwp->l_rtime;
    if (!rvm_clock_start() || !rvm_clock_wait() || !rvm_clock_wait() || !rvm_clock_stop()) rvm_boot_stop();
    bintime_addx(&expected, 2 * (UINT64_MAX / RVM_CLOCK_HZ + 1));
    if (!rvm_timecounter_init() || !bintimecmp(&curlwp->l_rtime, &expected, ==)) rvm_boot_stop();
    rvm_bootsched_yield();
    if (!bintimecmp(&curlwp->l_rtime, &expected, ==) || !rvm_bootsched_fini()) rvm_boot_stop();
    rvm_early_puts(rebase_ok);
}
''', timecounter_skip=True)
            out = run(work)
            assert 'RVM runtime epoch rebase preserves pending time.' in out, out
            print('PASS: MI timecounter epoch change commits pending runtime without charging the epoch offset', flush=True)
            build_wrapper(r'''
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <machine/bootsched.h>
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi); rvm_bootsched_init();
    curlwp->l_stime.sec += 10; rvm_bootsched_yield();
}
''', runtime_skip=True)
            out = run(work)
            assert 'NetBSD/RVM UVM panic: RVM LWP runtime clock moved backwards' in out, out
            print('PASS: bootstrap runtime accounting rejects a backwards clock', flush=True)
            if args.suite != 'full':
                return

        if args.suite in ('timecounter', 'full'):
            tc_ready = 'Bootstrap MI timecounter ready; uptime and timestamp reads verified.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert tc_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: final kernel reaches MI timecounter uptime (640 MiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'timecounter.c').read_text(), timecounter_skip=True)
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM MI uptime, cached timestamps and worker reads verified.' in out, out
                print(f'PASS: real IRQ uptime, cached/live reads, conversions, LWP generations and scheduler tick accounting ({pages * 64} KiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'tc_wrap.c').read_text(), timecounter_skip=True)
            out = run(work)
            assert 'RVM MI timecounter narrow/32-bit wraps and timehands rotation verified.' in out, out
            print('PASS: MI timecounter replacement, 8/32-bit wrap, second crossings and timehands generations', flush=True)
            for operation in (
                'tc_init(timecounter);',
                'bad.tc_counter_mask = 255; bad.tc_get_timecount = read_bad; bad.tc_name = name; tc_init(&bad);',
                'struct timespec ts = { 0, 0 }; tc_setclock(&ts);',
            ):
                body = r'''
#include <sys/time.h>
#include <sys/timetc.h>
#include <machine/boottime.h>
static struct timecounter bad __attribute__((section(".data"))) = { 0 };
const char name[] = "invalid";
static u_int __used read_bad(struct timecounter *tc __unused) { return 0; }
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi); rvm_timecounter_init();
''' + operation + '\n}\n'
                # Some rejection cases intentionally do not use the dummy counter.
                body = body.replace('static struct timecounter bad ', 'static struct timecounter bad __used ')
                build_wrapper(body, timecounter_skip=True)
                out = run(work)
                assert 'NetBSD/RVM UVM panic: Invalid bootstrap MI timecounter operation' in out, out
                print('PASS: bootstrap timecounter rejects ' + operation, flush=True)
            if args.suite != 'full':
                return

        if args.suite in ('hardclock', 'full'):
            hc_ready = 'Bootstrap MI clock ready; hardclock ticks and callout deadlines verified.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert hc_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: final kernel reaches MI hardclock and callouts (640 MiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'hardclock.c').read_text(), hardclock_skip=True)
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM MI hardclock, callout lifetime and callback deadlines verified.' in out, out
                print(f'PASS: real IRQ hardclock, reset/cancel/rearm, self-destroy and deferred wakeup ({pages * 64} KiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'callwheel.c').read_text(), hardclock_skip=True)
            out = run(work)
            assert 'RVM MI callout wheel boundaries and rescheduling verified.' in out, out
            print('PASS: deterministic MI timing wheel: 1000 ticks, 256/257/512 boundaries and earlier/later reset', flush=True)
            for operation in (
                'callout_init(&target, CALLOUT_MPSAFE); callout_reset(&target, 0, entry, NULL);',
                'callout_init(&target, CALLOUT_MPSAFE); callout_reset(&target, 3, entry, NULL); callout_destroy(&target);',
                'callout_init(&target, 0);',
            ):
                body = r'''
#include <sys/callout.h>
#include <machine/bootclock.h>
static callout_t target __attribute__((section(".data"))) = { 0 };
static void entry(void *arg __unused) { }
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi); rvm_bootclock_init();
''' + operation + '\n}\n'
                # The non-MPSAFE case otherwise leaves the fixture callback unused.
                body = body.replace('static void entry(', 'static void __used entry(')
                build_wrapper(body, hardclock_skip=True)
                out = run(work)
                assert 'NetBSD/RVM UVM panic: Invalid bootstrap MI callout operation' in out, out
                print('PASS: bootstrap callouts reject ' + operation, flush=True)
            if args.suite != 'full':
                return

        if args.suite in ('clock', 'full'):
            clock_ready = 'RVM repeating timer wakeups ready; idle deadlines and rearming verified.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert clock_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: kernel boots through repeating idle timer wakeups (640 MiB)', flush=True)
            clock_body = (Path(__file__).parent / 'clock.c').read_text()
            build_wrapper(clock_body, clock_skip=True)
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM repeated timer IRQs, idle deadlines and restart verified.' in out, out
                print(f'PASS: 26 alternating IRQs, worker/owner deadlines, odd stop/restart and cleanup ({pages * 64} KiB)', flush=True)
            fixture = (root / 'sys/arch/rvm/rvm/clock_probe.vma').read_text()
            fixture = fixture.replace('        halt\n', '        jmp rvm_clock_halt_pc\n')
            (work / 'clock_probe.vma').write_text(fixture)
            subprocess.run([args.vmasm, '-i', str(work / 'clock_probe.vma'), '-o', str(work / 'clock_probe.o'), '--obj'],
                           check=True, stdout=subprocess.DEVNULL)
            build_wrapper(clock_body, clock_skip=True, clock_probe=work / 'clock_probe.o')
            out = run(work)
            assert 'RVM repeated timer IRQs, idle deadlines and restart verified.' in out, out
            print('PASS: repeated early IRQs preserve both timer continuations and registers', flush=True)
            for operation in (
                'splsched(); int accepted = rvm_clock_wait() || rvm_clock_stop(); spl0(); if (accepted) rvm_boot_stop();',
                'rvm_irq_set_cr(12); int accepted = rvm_clock_wait() || rvm_clock_stop(); rvm_irq_set_cr(8); if (accepted) rvm_boot_stop();',
            ):
                body = r'''
#include <sys/lwp.h>
#include <sys/cpu.h>
#include <machine/irq.h>
const char rejected[] = "RVM repeating timer rejected invalid context.\n";
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
    if (!rvm_clock_start()) rvm_boot_stop();
''' + operation + '\n if (!rvm_clock_stop()) { rvm_boot_stop(); }\n rvm_early_puts(rejected);\n}\n'
                build_wrapper(body, clock_skip=True)
                out = run(work)
                assert 'RVM repeating timer rejected invalid context.' in out and 'UVM panic:' not in out, out
                print('PASS: repeating timer rejects ' + operation, flush=True)
            build_wrapper(r'''
#include <machine/irq.h>
static struct rvm_irqframe invalid __attribute__((section(".data"))) = { 0 };
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi); rvm_clock_start(); rvm_timer_irq_dispatch(&invalid);
}
''', clock_skip=True)
            out = run(work)
            assert 'NetBSD/RVM UVM panic: Invalid RVM timer interrupt frame' in out, out
            print('PASS: repeating timer rejects an unsolicited interrupt outside a wait', flush=True)
            if args.suite != 'full':
                return

        if args.suite in ('irq', 'full'):
            irq_ready = 'RVM timer interrupt ready; HALT wakeup and interrupt return verified.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert irq_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: final kernel wakes from HALT via timer IRQ 60 (640 MiB)', flush=True)
            irq_body = (Path(__file__).parent / 'irq.c').read_text()
            build_wrapper(irq_body, irq_skip=True)
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM timer IRQ worker stack, registers and cleanup verified.' in out, out
                print(f'PASS: hardware IRQ on worker stack, 63 GPRs, SR, SP, masking and cleanup ({pages * 64} KiB)', flush=True)
            fixture = (root / 'sys/arch/rvm/rvm/irq_probe.vma').read_text()
            fixture = fixture.replace('        halt\n', '        jmp rvm_timer_halt_pc\n')
            (work / 'irq_probe.vma').write_text(fixture)
            subprocess.run([args.vmasm, '-i', str(work / 'irq_probe.vma'), '-o', str(work / 'irq_probe.o'), '--obj'],
                           check=True, stdout=subprocess.DEVNULL)
            build_wrapper('#define RVM_TEST_IRQ_EARLY\n' + irq_body, irq_skip=True, irq_probe=work / 'irq_probe.o')
            out = run(work)
            assert 'RVM timer IRQ worker stack, registers and cleanup verified.' in out, out
            print('PASS: IRQ before HALT skips the wait and restores the continuation', flush=True)
            for operation in (
                'splsched(); if (rvm_timer_irq_verify()) rvm_boot_stop(); spl0();',
                'rvm_irq_set_cr(12); int accepted = rvm_timer_irq_verify(); rvm_irq_set_cr(8); if (accepted) rvm_boot_stop();',
            ):
                body = r'''
#include <sys/lwp.h>
#include <sys/cpu.h>
#include <machine/irq.h>
const char rejected[] = "RVM timer setup rejected invalid context.\n";
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
''' + operation + '\n rvm_early_puts(rejected);\n}\n'
                build_wrapper(body, irq_skip=True)
                out = run(work)
                assert 'RVM timer setup rejected invalid context.' in out and 'UVM panic:' not in out, out
                print('PASS: timer setup rejects ' + operation, flush=True)
            build_wrapper(r'''
#include <machine/irq.h>
static struct rvm_irqframe invalid __attribute__((section(".data"))) = { 0 };
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi); rvm_timer_irq_dispatch(&invalid);
}
''', irq_skip=True)
            out = run(work)
            assert 'NetBSD/RVM UVM panic: Invalid RVM timer interrupt frame' in out, out
            print('PASS: IRQ dispatcher rejects an unsolicited/invalid frame', flush=True)
            if args.suite != 'full':
                return

        if args.suite in ('trap', 'full'):
            trap_ready = 'RVM supervisor trap return ready; registers, status and stack restored.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert trap_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: final kernel returns from supervisor software trap (640 MiB)', flush=True)
            trap_body = (Path(__file__).parent / 'trap.c').read_text()
            build_wrapper(trap_body)
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM supervisor trap registers, IPLs and worker stack verified.' in out, out
                print(f'PASS: 63 GPRs, SP, flags, all logical IPLs and worker stack ({pages * 64} KiB)', flush=True)
            for level in (0, 63):
                fixture = (root / 'sys/arch/rvm/rvm/trap_probe.vma').read_text()
                fixture = fixture.replace('#0x1f003f', f'#0x{(level << 16) | 0x3f:x}')
                (work / 'trap_probe.vma').write_text(fixture)
                subprocess.run([args.vmasm, '-i', str(work / 'trap_probe.vma'), '-o', str(work / 'trap_probe.o'), '--obj'],
                               check=True, stdout=subprocess.DEVNULL)
                build_wrapper(trap_body, trap_probe=work / 'trap_probe.o')
                out = run(work)
                assert 'RVM supervisor trap registers, IPLs and worker stack verified.' in out, out
                print(f'PASS: RTE restores hardware SR interrupt priority {level}', flush=True)
            for assembly, vector in ((' move.q #0,r0\n syscall\n rts\n', 10),
                                     (' dc.w $ffff\n rts\n', 0)):
                build_wrapper(r'''
extern void rvm_test_probe(void);
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi); rvm_test_probe();
}
''', assembly)
                out = run(work)
                assert trap_ready in out and f'fatal startup exception: vector 0x{vector:016x}' in out, out
                print(f'PASS: unsupported supervisor service/fault stays fatal (vector {vector})', flush=True)
            if args.suite != 'full':
                return

        if args.suite in ('idle', 'full'):
            idle_ready = 'RVM MI idle LWP ready; empty queues, sleep and wakeup verified.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert idle_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: kernel boots through MI idle/sleep/wakeup (640 MiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'idle.c').read_text())
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM idle stack, sleeping workers and empty-queue exit verified.' in out, out
                print(f'PASS: idle stack, 64 event wakeups, blocked worker, empty-queue exit and cleanup ({pages * 64} KiB)', flush=True)
            halt_body = r'''
#include <sys/lwp.h>
#include <sys/cpu.h>
#include <machine/bootsched.h>
const char halt_marker[] = "RVM idle HALT boundary reached.\n";
const char returned[] = "ERROR: sleeping owner returned.\n";
static void no_event(void *arg __unused) { rvm_early_puts(halt_marker); }
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
    rvm_bootsched_init(); rvm_bootsched_idle_init(no_event, NULL);
    rvm_bootsched_sleep(); rvm_early_puts(returned);
}
'''
            build_wrapper(halt_body)
            out = run(work)
            assert idle_ready in out and out.count('RVM idle HALT boundary reached.') == 1, out
            assert 'ERROR: sleeping owner returned.' not in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: no runnable work reaches idle HALT once and leaves owner asleep', flush=True)
            for operation in (
                'rvm_bootsched_sleep();',
                'rvm_bootsched_init(); rvm_bootsched_sleep();',
                'rvm_bootsched_init(); rvm_bootsched_idle_init(NULL, NULL); cpu_idle();',
            ):
                body = r'''
#include <sys/lwp.h>
#include <sys/cpu.h>
#include <machine/bootsched.h>
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
''' + operation + '\n}\n'
                build_wrapper(body)
                out = run(work)
                assert idle_ready in out and 'NetBSD/RVM UVM panic: Invalid RVM directed MI switch' in out, out
                print('PASS: idle/sleep boundary rejects ' + operation, flush=True)
            if args.suite != 'full':
                return

        if args.suite in ('runq', 'full'):
            runq_ready = 'RVM MI run queues ready; runnable selection, yield and worker exit verified.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert runq_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: kernel boots through MI run queues (640 MiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'runq.c').read_text())
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM MI priorities, FIFO rotation and queue cleanup verified.' in out, out
                print(f'PASS: MI priority selection, FIFO rotation, self selection and reaping ({pages * 64} KiB)', flush=True)
            for operation in (
                'rvm_bootsched_yield();',
                'rvm_bootsched_init(); rvm_bootsched_finish();',
                'rvm_bootsched_init(); splsched(); rvm_bootsched_yield();',
                'rvm_bootsched_init(); curlwp->l_auxprio = PRI_COUNT; rvm_bootsched_yield();',
            ):
                body = r'''
#include <sys/lwp.h>
#include <sys/cpu.h>
#include <machine/bootsched.h>
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
''' + operation + '\n}\n'
                build_wrapper(body)
                out = run(work)
                assert runq_ready in out and 'NetBSD/RVM UVM panic: Invalid RVM directed MI switch' in out, out
                print('PASS: run queue API rejects ' + operation, flush=True)
            if args.suite != 'full':
                return

        if args.suite in ('switch', 'full'):
            switch_ready = 'RVM MI switching ready; resumed LWPs, zombie handoff and reaping verified.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert switch_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: kernel boots through directed MI switching (640 MiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'mi_switch.c').read_text())
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM MI switch ring, zombie release and reaping verified.' in out, out
                print(f'PASS: MI switch ring: 128 switches, heap, zombie handoffs and reaping ({pages * 64} KiB)', flush=True)
            for operation in (
                'rvm_bootsched_switch(&target);',
                'rvm_bootsched_init(); rvm_bootsched_switch(curlwp);',
                'rvm_bootsched_init(); target.l_pflag |= LP_RUNNING; rvm_bootsched_switch(&target);',
                'rvm_bootsched_init(); target.l_stat = LSZOMB; rvm_bootsched_switch(&target);',
            ):
                body = r'''
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <uvm/uvm.h>
#include <machine/switch.h>
#include <machine/bootsched.h>
static struct lwp target __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static void entry(void *arg __unused) { rvm_boot_stop(); }
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
    target.l_addr = (void *)uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
    target.l_cpu = curcpu(); target.l_flag = LW_SYSTEM;
    target.l_stat = LSIDL; target.l_pflag = LP_MPSAFE; target.l_proc = &process;
    cpu_lwp_fork(curlwp, &target, NULL, 0, entry, NULL);
''' + operation + '\n}\n'
                build_wrapper(body)
                out = run(work)
                assert switch_ready in out and 'NetBSD/RVM UVM panic: Invalid RVM directed MI switch' in out, out
                print('PASS: directed MI switching rejects ' + operation, flush=True)
            if args.suite != 'full':
                return

        if args.suite in ('lwp', 'full'):
            lwp_ready = 'RVM kernel LWPs ready; fork setup and MI startup handoff verified.'
            shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
            configure(work, 10240)
            out = run(work)
            assert lwp_ready in out and 'UVM panic:' not in out and 'fatal startup exception' not in out, out
            print('PASS: kernel boots through MI LWP startup (640 MiB)', flush=True)
            build_wrapper((Path(__file__).parent / 'lwp.c').read_text())
            for pages in (256, 1024):
                configure(work, pages)
                out = run(work)
                assert 'RVM MI LWP startup and u-area lifecycle verified.' in out, out
                print(f'PASS: LWP fork, live/zombie handoffs, heap and u-area release ({pages * 64} KiB)', flush=True)
            for operation in (
                'cpu_lwp_fork(curlwp, &target, (void *)1, 1, entry, NULL);',
                'target.l_flag = 0; cpu_lwp_fork(curlwp, &target, NULL, 0, entry, NULL);',
                'cpu_lwp_fork(curlwp, &target, NULL, 0, entry, NULL); target.l_pflag |= LP_RUNNING; cpu_lwp_free2(&target);',
                'cpu_lwp_fork(curlwp, &target, NULL, 0, entry, NULL); splsched(); curcpu()->ci_onproc = &target; cpu_switchto(curlwp, &target, false);',
            ):
                body = r'''
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <uvm/uvm.h>
#include <machine/switch.h>
static struct lwp target __attribute__((section(".data"))) = { 0 };
static struct proc process __attribute__((section(".data"))) = { 0 };
static void entry(void *arg __unused) { rvm_boot_stop(); }
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
    target.l_addr = (void *)uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
    target.l_cpu = curcpu(); target.l_flag = LW_SYSTEM;
    target.l_stat = LSIDL; target.l_pflag = LP_MPSAFE; target.l_proc = &process;
''' + operation + '\n}\n'
                build_wrapper(body)
                out = run(work)
                assert lwp_ready in out and 'NetBSD/RVM UVM panic: Invalid RVM kernel LWP lifecycle' in out, out
                print('PASS: LWP lifecycle rejects ' + operation, flush=True)
            if args.suite != 'full':
                return

        # Focused suite: exercise the actual new MD context-switch path only.
        shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
        configure(work, 10240)
        out = run(work)
        assert context_ready in out and 'fatal startup exception' not in out and 'UVM panic:' not in out, out
        print('PASS: final kernel boots through CPU context switching (640 MiB)', flush=True)
        probe = root / 'sys/arch/rvm/tests/switch_regs.S'
        subprocess.run([args.vmasm, '-i', str(probe), '-o', str(work / 'regs.o'), '--obj'],
                       check=True, stdout=subprocess.DEVNULL)
        build_wrapper((Path(__file__).parent / 'context.c').read_text(), extra_objects=(work / 'regs.o',))
        for pages in (256, 1024):
            configure(work, pages)
            out = run(work)
            assert 'RVM LWP contexts, registers and stack ownership verified.' in out, out
            print(f'PASS: CPU contexts: R10-R62, distinct stacks, prev-LWP, heap and cleanup ({pages * 64} KiB)', flush=True)
        for operation in (
            'splsched(); cpu_switchto(curlwp, NULL, false);',
            'cpu_switchto(curlwp, curlwp, false);',
            '((struct pcb *)lwp_getpcb(&target))->pcb_gpr[RVM_REG_SP] = 0; splsched(); cpu_switchto(curlwp, &target, false);',
            '((struct pcb *)lwp_getpcb(&target))->pcb_mmu_table = 0; splsched(); cpu_switchto(curlwp, &target, false);',
        ):
            body = '''
#include <sys/lwp.h>
#include <sys/cpu.h>
#include <uvm/uvm.h>
#include <machine/switch.h>
#include <machine/pcb.h>
static struct lwp target __attribute__((section(".data"))) = { 0 };
static void entry(void *arg __unused) { rvm_boot_stop(); }
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
    vaddr_t stack = uvm_km_alloc(kernel_map, USPACE, 0, UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);
    target.l_cpu = curcpu(); target.l_flag = LW_SYSTEM;
    rvm_cpu_lwp_prepare(&target, stack, USPACE, entry, NULL);
''' + operation + '\n}\n'
            build_wrapper(body)
            out = run(work)
            assert context_ready in out and 'NetBSD/RVM UVM panic: Invalid RVM kernel context switch' in out, out
            print('PASS: CPU context rejects ' + operation, flush=True)
        if args.suite != 'full':
            return

        # FHR REL records keep addends in the instruction bytes. Losing them
        # sends data references and local jumps to the starts of sections.
        reloc_source = '''.text
.global _start
_start:
 lea reloc_data,r2
 lea reloc_data+8,r3
 jmp .Lreloc_target
 nop
.Lreloc_target:
 rts
.data
 .quad 0
.global reloc_data
reloc_data:
 .quad 1
 .quad 2
'''
        (work / 'reloc.s').write_text(reloc_source)
        subprocess.run([args.cross + 'gcc', '-nostdinc', '-x', 'assembler',
                        '-c', str(work / 'reloc.s'), '-o', str(work / 'reloc.o')], check=True)
        subprocess.run([args.cross + 'ld', '-e', '_start', '-o',
                        str(work / 'reloc.fhr'), str(work / 'reloc.o')],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        image = (work / 'reloc.fhr').read_bytes()
        assert image[:8] == b'FHR\0\x01\x01\x00\x00', 'unexpected FHR encoding'
        count = int.from_bytes(image[14:16], 'big')
        chunk = 16 + 12 * count
        assert image[chunk:chunk + 4] == b'COD\0', 'missing first code section'
        text = image[chunk + 14:chunk + 14 + int.from_bytes(image[16:24], 'big')]
        actual = [int.from_bytes(text[offset:offset + 8], 'big') for offset in (3, 16, 29)]
        assert actual == [8, 16, 39], f'FHR linker lost in-place addends: {actual}'
        print('PASS: linker preserves data offsets, explicit addends and local jump targets', flush=True)

        # The free range must change with RAM size, without including the image,
        # bootstrap tables, scratch page or the loader's final stack page.
        shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
        for pages in (32, 256, 1024, 10240):
            configure(work, pages)
            out = run(work)
            assert context_ready in out and 'fatal startup exception' not in out, out
            reserved = int(re.search(r'Reserved prefix end: 0x([0-9a-f]+)', out)[1], 16)
            rootpa = int(re.search(r'Supervisor MMU root: 0x([0-9a-f]+)', out)[1], 16)
            free = re.search(r'Free physical range for UVM: 0x([0-9a-f]+)\.\.0x([0-9a-f]+)', out)
            first, last = (int(value, 16) for value in free.groups())
            assert reserved <= rootpa < first < last == (pages - 1) * 65536, out
            assert rootpa % 2048 == first % 65536 == last % 65536 == 0, out
            registered = re.search(r'UVM registered PFN range: 0x([0-9a-f]+)\.\.0x([0-9a-f]+)', out)
            assert tuple(int(value, 16) for value in registered.groups()) == (first >> 16, last >> 16), out
            assert f'UVM bootstrap allocation: 0x{first:016x}' in out, out
            print(f'PASS: RAM {pages * 64} KiB, reservations, allocation, translated alias', flush=True)

        # Small RAM may fail while constructing tables or handing off the
        # remaining pages, depending on the current image footprint.
        configure(work, 16)
        out = run(work)
        assert any(message in out for message in (
            'Cannot build or verify bootstrap page tables.',
            'UVM physical memory handoff failed.')) and pool_ready not in out, out
        assert 'fatal startup exception' not in out and 'UVM panic:' not in out, out
        print('PASS: 1 MiB stops cleanly before completing memory startup', flush=True)

        configure(work, reserved // 65536 + 2)
        out = run(work)
        assert 'Cannot build or verify bootstrap page tables.' in out and ready not in out, out
        print('PASS: insufficient RAM stops before MMU activation', flush=True)

        # General MUL instructions have a RegMode destination (mode byte
        # plus register), unlike the raw-register fast three-operand forms.
        multiply = '''.text
.global rvm_test_mul
rvm_test_mul:
 pushf r10/r62
 move.q #7,r10
 muls.b #-3,r10
 cmp.w #-21,r10
 bne .Lmul_fail
 move.q #3,r10
 muls.w #-356,r10
 cmp.l #-1068,r10
 bne .Lmul_fail
 move.q #3,r10
 muls.l #-356,r10
 cmp.q #-1068,r10
 bne .Lmul_fail
 move.q #3,r10
 muls.q #356,r10
 cmp.q #1068,r10
 bne .Lmul_fail
 move.q #7,r10
 mulu.b #3,r10
 cmp.w #21,r10
 bne .Lmul_fail
 move.q #356,r10
 mulu.w #356,r10
 cmp.l #126736,r10
 bne .Lmul_fail
 move.q #3,r10
 mulu.l rvm_mul_operand,r10
 cmp.q #1068,r10
 bne .Lmul_fail
 move.q #3,r10
 mulu.q #356,r10
 cmp.q #1068,r10
 bne .Lmul_fail
 move.l #1,r0
 popf
 rts
.Lmul_fail:
 clr.l r0
 popf
 rts
.data
.global rvm_mul_operand
rvm_mul_operand:
 .long 356
'''
        (work / 'multiply.raw.s').write_text(multiply)
        subprocess.run(['python3', str(root / 'sys/arch/rvm/stand/fhr_asm.py'),
                        str(work / 'multiply.raw.s'), str(work / 'multiply.s'),
                        '--cc', args.cross + 'gcc'], check=True)
        subprocess.run([args.cross + 'gcc', '-c', str(work / 'multiply.s'),
                        '-o', str(work / 'multiply.o')], check=True)
        body = '''
extern int rvm_test_mul(void);
const char mul_good[] = "General multiply encodings verified.\\n";
const char mul_bad[] = "General multiply encoding regression.\\n";
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
    if (!rvm_test_mul()) { rvm_early_puts(mul_bad); rvm_boot_stop(); }
    rvm_early_puts(mul_good);
}
'''
        build_wrapper(body, extra_objects=(work / 'multiply.o',))
        out = run(work)
        assert 'General multiply encodings verified.' in out, out
        print('PASS: signed/unsigned general multiply in all widths, immediates '
              'and memory source with detected GAS compatibility', flush=True)

        # A constant inside a widening extension used to ICE in combine.
        # Compile separately so callers cannot fold away the multiplies.
        functions = []
        checks = []
        for narrow, wide, bits, signed in (
                ('signed char', 'short', 8, True),
                ('short', 'int', 16, True),
                ('int', 'long', 32, True),
                ('unsigned char', 'unsigned short', 8, False),
                ('unsigned short', 'unsigned int', 16, False),
                ('unsigned int', 'unsigned long', 32, False)):
            limit = 1 << (bits - int(signed))
            values = (0, 1, limit - 1, -limit, -1) if signed else (0, 1, limit - 1)
            constants = (-128, -1, 5, 127) if signed else (5, 127, 128, 255)
            if bits > 8:
                constants += (356,)
            for constant in constants:
                name = 'widen_mul_' + str(len(functions))
                functions.append(f'{wide} {name}({narrow} a) '
                                 f'{{ return ({wide})a * ({narrow}){constant}; }}')
                checks.append(f'extern {wide} {name}({narrow});')
                for value in values:
                    expected = value * constant
                    checks.append(f'if ({name}(({narrow}){value}) != '
                                  f'({wide})({expected}L)) return 0;')
            name = 'widen_mul_' + str(len(functions))
            functions.append(f'{wide} {name}({narrow} a, {narrow} b) '
                             f'{{ return ({wide})a * ({wide})b; }}')
            checks.append(f'extern {wide} {name}({narrow}, {narrow});')
            for a in values:
                for b in values:
                    suffix = 'UL' if a * b >= 0 else 'L'
                    checks.append(f'if ({name}(({narrow}){a}, ({narrow}){b}) != '
                                  f'({wide})({a * b}{suffix})) return 0;')
        (work / 'widen.c').write_text('\n'.join(functions))
        subprocess.run(compiler + ['-S', str(work / 'widen.c'), '-o',
                                  str(work / 'widen.raw.s')], check=True)
        subprocess.run(['python3', str(root / 'sys/arch/rvm/stand/fhr_asm.py'),
                        str(work / 'widen.raw.s'), str(work / 'widen.s'),
                        '--cc', args.cross + 'gcc'], check=True)
        subprocess.run([args.cross + 'gcc', '-c', str(work / 'widen.s'),
                        '-o', str(work / 'widen.o')], check=True)
        body = '\n'.join((
            'static int check_widen(void) {', *checks, 'return 1; }',
            'const char widen_good[] = "Widening multiplication verified.\\n";',
            'const char widen_bad[] = "Widening multiplication failed.\\n";',
            'void rvm_early_init(const struct rvm_bootinfo *bi) {',
            'rvm_early_consinit();',
            'if (!check_widen()) { rvm_early_puts(widen_bad); rvm_boot_stop(); }',
            'rvm_early_puts(widen_good); rvm_normal_early_init(bi); }'))
        build_wrapper(body, extra_objects=(work / 'widen.o',))
        out = run(work)
        assert 'Widening multiplication verified.' in out and pool_ready in out, out
        print('PASS: compiler widening multiply, signed/unsigned 8/16/32-bit '
              'constants and registers at boundaries', flush=True)

        # Exercise unsigned comparison branches and symbol addresses at -O2.
        comparisons = '''
volatile unsigned long test_a __attribute__((section(".data"))) = 0;
volatile unsigned long test_b __attribute__((section(".data"))) = 0;
const char cmp_fail[] = "Compiler comparison regression failed.\\n";
const char cmp_pass[] = "Compiler comparisons verified.\\n";
__attribute__((noinline)) int compare(void) {
    return (test_a < test_b) | ((test_a >= test_b) << 1) |
        ((test_a > test_b) << 2) | ((test_a <= test_b) << 3);
}
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_early_consinit();
'''
        for a, b in ((0, 0), (0, 1), (1, 0), (2**64 - 1, 0),
                     (0, 2**64 - 1), (2**63, 2**63 - 1), (2**63 - 1, 2**63)):
            expected = int(a < b) | (int(a >= b) << 1) | (int(a > b) << 2) | (int(a <= b) << 3)
            comparisons += f'test_a = {a}UL; test_b = {b}UL;\n'
            comparisons += f'if (compare() != {expected}) {{ rvm_early_puts(cmp_fail); rvm_boot_stop(); }}\n'
        comparisons += 'rvm_early_puts(cmp_pass); rvm_normal_early_init(bi); }\n'
        build_wrapper(comparisons)
        out = run(work)
        assert 'Compiler comparisons verified.' in out and ready in out, out
        print('PASS: compiler unsigned branches and symbol-address arithmetic', flush=True)

        boundary = '''
#include <uvm/uvm.h>
const char rvm_cross_ok[] = "MMU page boundary verified.\\n";
const char rvm_cross_bad[] = "MMU page boundary regression.\\n";
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
    struct vm_page *a = uvm_pagealloc_strat(0, 0, 0, UVM_PGA_USERESERVE, 0, 0);
    struct vm_page *b = uvm_pagealloc_strat(0, 0, 0, UVM_PGA_USERESERVE, 0, 0);
    struct vm_page *c = uvm_pagealloc_strat(0, 0, 0, UVM_PGA_USERESERVE, 0, 0);
    if (!a || !b || !c) { rvm_early_puts(rvm_cross_bad); rvm_boot_stop(); }
    paddr_t pa = VM_PAGE_TO_PHYS(a);
    volatile uint32_t *last = (volatile uint32_t *)(uintptr_t)(pa + RVM_MMU_PAGE_SIZE - 4);
    volatile uint32_t *middle = (volatile uint32_t *)(uintptr_t)VM_PAGE_TO_PHYS(b);
    volatile uint32_t *next = (volatile uint32_t *)(uintptr_t)VM_PAGE_TO_PHYS(c);
    pmap_kenter_pa(VM_KERNEL_VM_BASE, pa, VM_PROT_READ | VM_PROT_WRITE, 0);
    pmap_kenter_pa(VM_KERNEL_VM_BASE + RVM_MMU_PAGE_SIZE,
        VM_PAGE_TO_PHYS(c), VM_PROT_READ | VM_PROT_WRITE, 0);
    pmap_update(pmap_kernel());
    volatile uint64_t *cross = (volatile uint64_t *)(VM_KERNEL_VM_BASE + RVM_MMU_PAGE_SIZE - 4);
    *last = 0x11223344; *next = 0x55667788; *middle = 0x99aabbcc;
    if (*cross != 0x1122334455667788UL) {
        rvm_early_puts(rvm_cross_bad); rvm_boot_stop();
    }
    *cross = 0xfedcba9876543210UL;
    if (*last != 0xfedcba98 || *next != 0x76543210 || *middle != 0x99aabbcc) {
        rvm_early_puts(rvm_cross_bad); rvm_boot_stop();
    }
    pmap_kremove(VM_KERNEL_VM_BASE, 2 * RVM_MMU_PAGE_SIZE);
    pmap_update(pmap_kernel());
    uvm_pagefree(a); uvm_pagefree(b); uvm_pagefree(c);
    rvm_early_puts(rvm_cross_ok);
}
'''
        build_wrapper(boundary)
        out = run(work)
        assert 'MMU page boundary verified.' in out, out
        print('PASS: unaligned 64-bit reads/writes across virtual pages with '
              'noncontiguous physical backing', flush=True)

        build_wrapper((Path(__file__).parent / 'pmap.c').read_text())
        out = run(work)
        assert 'Kernel pmap operations verified.' in out, out
        print('PASS: kernel mapping replacement, leaf boundaries, statistics, protection, '
              'page zero/copy and sealed allocator', flush=True)

        build_wrapper((Path(__file__).parent / 'pagealloc.c').read_text())
        out = run(work)
        assert 'Managed UVM pages verified.' in out, out
        print('PASS: MI page metadata, allocation/free, zero-on-reuse, unique ownership, '
              'reserve exhaustion, restored counts and bootstrap locks', flush=True)

        build_wrapper((Path(__file__).parent / 'bootmap.c').read_text())
        out = run(work)
        assert 'MI kernel VM map reservations verified.' in out, out
        print('PASS: real MI map reservations, list/tree lookup, alignment, overlap, '
              'entry/VA exhaustion, reuse and reader/writer locks', flush=True)

        build_wrapper((Path(__file__).parent / 'bootobject.c').read_text())
        out = run(work)
        assert 'Resident UVM objects and radix exhaustion verified.' in out, out
        print('PASS: MI object ownership, sparse lookups, dirty tags, wire counts, '
              'map references, radix/object exhaustion and restored resources', flush=True)

        build_wrapper((Path(__file__).parent / 'bootanonmap.c').read_text())
        for pages in (256, 1024):
            configure(work, pages)
            out = run(work, timeout=12)
            assert 'MI anonymous VM entries, aliases and rollback verified.' in out, out
            print(f'PASS: anonymous VM entries, pinned references, shared aliases, '
                  f'ledger/descriptor/heap/VA/RAM limits, eager rollback and MI detach '
                  f'({pages * 64} KiB)', flush=True)

        # A pinned entry's resident page cannot be freed directly, even locked.
        body = '''
#include <uvm/uvm.h>
#include <machine/bootanonmap.h>
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
    vaddr_t va = rvm_bootanonmap_alloc(1);
    struct vm_map_entry *entry;
    vm_map_lock_read(kernel_map);
    uvm_map_lookup_entry(kernel_map, va, &entry);
    struct vm_amap *amap = entry->aref.ar_amap;
    vm_map_unlock_read(kernel_map);
    rw_enter(amap->am_lock, RW_WRITER);
    uvm_pagefree(amap->am_anon[0]->an_page);
}
'''
        build_wrapper(body)
        out = run(work)
        assert context_ready in out and 'NetBSD/RVM UVM panic: Invalid raw UVM page operation' in out, out
        print('PASS: direct page free rejects pinned anonymous VM entries', flush=True)

        for name, operation, assembly, vector in (
            ('released anonymous VM entry', 'rvm_bootanonmap_free(va, 1);',
             ' move.q (r2),r0\n rts\n', 5),
            ('removed shared anonymous VM entry',
             'rvm_bootanonmap_share(va); rvm_bootanonmap_free(va, 1);',
             ' move.q (r2),r0\n rts\n', 5),
            ('non-executable anonymous VM entry', '', ' jsr (r2)\n rts\n', 2),
        ):
            body = '#include <machine/bootanonmap.h>\nextern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); vaddr_t va = rvm_bootanonmap_alloc(1);\n'
            body += operation + '\nrvm_test_probe(va); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert context_ready in out and expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection for ' + name, flush=True)

        build_wrapper((Path(__file__).parent / 'bootamap.c').read_text())
        for pages in (256, 1024):
            configure(work, pages)
            out = run(work)
            assert 'MI anonymous maps, sharing and exhaustion verified.' in out, out
            print(f'PASS: MI amap sparse slots, ownership transfers, full-map sharing, '
                  f'cache/heap/VA/RAM limits and rollback ({pages * 64} KiB)', flush=True)

        for operation, error in (
            ('anon->an_ref = 0; uvm_anfree(anon);',
             'Invalid bootstrap anonymous page operation'),
            ('uvm_pagefree(pg);', 'Invalid raw UVM page operation'),
            ('pg->flags |= PG_BUSY; rvm_bootamap_drop(map);',
             'Invalid bootstrap anonymous page operation'),
        ):
            body = '#include <uvm/uvm.h>\n#include <machine/bootamap.h>\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); struct vm_amap *map = rvm_bootamap_create(PAGE_SIZE);\n'
            body += 'struct vm_anon *anon __unused = uvm_analloc();\n'
            body += 'rvm_bootamap_put(map, 0, anon);\n'
            body += 'struct vm_page *pg __unused = rvm_bootamap_page(map, 0);\n'
            body += operation + '\n}\n'
            build_wrapper(body)
            out = run(work)
            assert context_ready in out and 'NetBSD/RVM UVM panic: ' + error in out, out
            print('PASS: anonymous maps reject ' + operation, flush=True)

        for name, operation, assembly, vector in (
            ('released anonymous map alias', 'rvm_bootamap_drop(map);',
             ' move.q (r2),r0\n rts\n', 5),
            ('non-executable anonymous map page', '', ' jsr (r2)\n rts\n', 2),
        ):
            body = '#include <uvm/uvm.h>\n#include <machine/bootamap.h>\nextern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); struct vm_amap *map = rvm_bootamap_create(PAGE_SIZE);\n'
            body += 'rvm_bootamap_put(map, 0, uvm_analloc());\n'
            body += 'struct vm_page *pg = rvm_bootamap_page(map, 0);\n'
            body += 'vaddr_t va = VM_KERNEL_VM_BASE;\n'
            body += 'pmap_kenter_pa(va, VM_PAGE_TO_PHYS(pg), VM_PROT_READ | VM_PROT_WRITE, 0);\n'
            body += 'pmap_update(pmap_kernel());\n' + operation + '\nrvm_test_probe(va); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert context_ready in out and expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection for ' + name, flush=True)

        build_wrapper((Path(__file__).parent / 'bootanon.c').read_text())
        for pages in (256, 1024):
            configure(work, pages)
            out = run(work)
            assert 'MI anonymous descriptors, pages and exhaustion verified.' in out, out
            print(f'PASS: MI anons, references, page accounting, zeroing, aliases, '
                  f'descriptor/cache/VA/RAM limits and rollback ({pages * 64} KiB)', flush=True)

        for operation, error in (
            ('uvm_anfree(anon);', 'Invalid bootstrap anonymous page operation'),
            ('uvm_anfree((struct vm_anon *)1);', 'Invalid bootstrap anonymous page operation'),
            ('anon->an_ref = 0; uvm_anfree(anon); uvm_anfree(anon);',
             'Invalid bootstrap anonymous page operation'),
            ('uvm_pagealloc_strat(NULL, 0, anon, UVM_PGA_ZERO, 0, 0);',
             'Invalid raw UVM page operation'),
            ('struct vm_page *pg = rvm_bootanon_alloc(anon); '
             'pg->loan_count = 1; rvm_bootanon_drop(anon);',
             'Invalid bootstrap anonymous page operation'),
            ('struct vm_page *pg = rvm_bootanon_alloc(anon); '
             'pg->flags |= PG_BUSY; rvm_bootanon_drop(anon);',
             'Invalid bootstrap anonymous page operation'),
            ('anon->an_swslot = 1; anon->an_ref = 0; uvm_anfree(anon);',
             'Invalid bootstrap anonymous page operation'),
            ('krwlock_t lock; rw_init(&lock); rw_enter(&lock, RW_WRITER); '
             'anon->an_lock = &lock; '
             'uvm_pagealloc_strat(NULL, 0, anon, UVM_PGA_ZERO, 0, 0);',
             'Invalid raw UVM page operation'),
            ('struct vm_page *pg = rvm_bootanon_alloc(anon); uvm_pagefree(pg);',
             'Invalid raw UVM page operation'),
            ('kpreempt_enable();', 'Invalid bootstrap lock/IPL operation'),
        ):
            body = '#include <uvm/uvm.h>\n#include <machine/bootanon.h>\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); struct vm_anon *anon __unused = uvm_analloc();\n'
            body += operation + '\n}\n'
            build_wrapper(body)
            out = run(work)
            assert context_ready in out and 'NetBSD/RVM UVM panic: ' + error in out, out
            print('PASS: anonymous pages reject ' + operation, flush=True)

        for name, operation, assembly, vector in (
            ('released anonymous page alias', 'rvm_bootanon_drop(anon);',
             ' move.q (r2),r0\n rts\n', 5),
            ('non-executable anonymous page', '', ' jsr (r2)\n rts\n', 2),
        ):
            body = '#include <uvm/uvm.h>\n#include <machine/bootanon.h>\nextern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); struct vm_anon *anon = uvm_analloc();\n'
            body += 'struct vm_page *pg = rvm_bootanon_alloc(anon);\n'
            body += 'vaddr_t va = VM_KERNEL_VM_BASE;\n'
            body += 'pmap_kenter_pa(va, VM_PAGE_TO_PHYS(pg), VM_PROT_READ | VM_PROT_WRITE, 0);\n'
            body += 'pmap_update(pmap_kernel());\n' + operation + '\nrvm_test_probe(va); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert context_ready in out and expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection for ' + name, flush=True)

        build_wrapper((Path(__file__).parent / 'bootcache.c').read_text())
        for pages in (256, 1024):
            configure(work, pages)
            out = run(work)
            assert 'MI pool cache magazines, callbacks and exhaustion verified.' in out, out
            print(f'PASS: MI pool cache normal/large magazines, callbacks, reuse, '
                  f'ownership/VA/RAM limits and rollback ({pages * 64} KiB)', flush=True)

        for operation in (
            'pool_cache_put(cache, item + 1);',
            'pool_cache_put(cache, item); pool_cache_put(cache, item);',
            'pool_cache_put(other, item);',
            'pool_cache_put(cache, NULL);',
            'pool_cache_get(cache, PR_WAITOK);',
            'pool_cache_get(cache, PR_NOWAIT | PR_ZERO);',
            'pool_cache_put_paddr(cache, item, 0);',
            'pool_cache_invalidate((pool_cache_t)1);',
        ):
            body = '#include <machine/bootcache.h>\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); pool_cache_t cache = '
            body += 'rvm_bootcache_create(37, 16, 0, NULL, NULL, NULL);\n'
            body += 'pool_cache_t other __unused = '
            body += 'rvm_bootcache_create(37, 16, 0, NULL, NULL, NULL);\n'
            body += 'unsigned char *item __unused = pool_cache_get(cache, PR_NOWAIT);\n'
            body += operation + '\n}\n'
            build_wrapper(body)
            out = run(work)
            assert context_ready in out and ('NetBSD/RVM UVM panic: Invalid bootstrap '
                                          'pool cache operation') in out, out
            print('PASS: pool cache rejects ' + operation, flush=True)

        for name, operation, assembly, vector in (
            ('destroyed pool cache page',
             'pool_cache_put(cache, (void *)va); rvm_bootcache_destroy(cache);',
             ' move.q (r2),r0\n rts\n', 5),
            ('non-executable pool cache item', '', ' jsr (r2)\n rts\n', 2),
        ):
            body = '#include <machine/bootcache.h>\nextern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); pool_cache_t cache = '
            body += 'rvm_bootcache_create(37, 16, 0, NULL, NULL, NULL);\n'
            body += 'vaddr_t va = (vaddr_t)pool_cache_get(cache, PR_NOWAIT);\n'
            body += operation + '\nrvm_test_probe(va); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert context_ready in out and expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection for ' + name, flush=True)

        build_wrapper((Path(__file__).parent / 'bootpool.c').read_text())
        for pages in (256, 1024):
            configure(work, pages)
            out = run(work)
            assert 'MI pool ownership, page lists and exhaustion verified.' in out, out
            print(f'PASS: MI pool linked lists/bitmaps, zero/reuse, ownership, item/backing '
                  f'limits, shared allocator reuse and page release ({pages * 64} KiB)', flush=True)

        for operation in (
            'pool_put(pool, item + 1);',
            'pool_put(pool, item); pool_put(pool, item);',
            'pool_put(other, item);',
            'pool_put(pool, NULL);',
            'pool_get(pool, PR_WAITOK);',
        ):
            body = '#include <sys/pool.h>\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); struct pool *pool = '
            body += 'rvm_bootpool_create(37, 16);\n'
            body += 'struct pool *other __unused = rvm_bootpool_create(37, 16);\n'
            body += 'unsigned char *item __unused = pool_get(pool, PR_NOWAIT);\n'
            body += operation + '\n}\n'
            build_wrapper(body)
            out = run(work)
            assert context_ready in out and ('NetBSD/RVM UVM panic: Invalid bootstrap '
                                         'pool operation') in out, out
            print('PASS: pool rejects ' + operation, flush=True)

        for name, operation, assembly, vector in (
            ('destroyed pool page', 'pool_put(pool, (void *)va); rvm_bootpool_destroy(pool);',
             ' move.q (r2),r0\n rts\n', 5),
            ('non-executable pool item', '', ' jsr (r2)\n rts\n', 2),
        ):
            body = '#include <sys/pool.h>\nextern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); struct pool *pool = '
            body += 'rvm_bootpool_create(37, 16);\n'
            body += 'vaddr_t va = (vaddr_t)pool_get(pool, PR_NOWAIT | PR_ZERO);\n'
            body += operation + '\nrvm_test_probe(va); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert context_ready in out and expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection for ' + name, flush=True)

        build_wrapper((Path(__file__).parent / 'bootheap.c').read_text())
        for pages in (256, 1024):
            configure(work, pages)
            out = run(work)
            assert 'Bootstrap kmem heap fragmentation and exhaustion verified.' in out, out
            print(f'PASS: vmem heap sharing, zeroing, fragmentation/coalescing, '
                  f'tag/import/RAM limits and complete span release ({pages * 64} KiB)', flush=True)

        for operation, error in (
            ('kmem_free(memory, 38);', 'Invalid bootstrap heap operation'),
            ('kmem_free(memory + 1, 36);', 'Invalid bootstrap heap operation'),
            ('kmem_free(memory, 37); kmem_free(memory, 37);',
             'Invalid bootstrap heap operation'),
            ('kmem_free((void *)uvm_km_alloc(kernel_map, PAGE_SIZE, 0, '
             'UVM_KMF_WIRED | UVM_KMF_NOWAIT), PAGE_SIZE);',
             'Invalid bootstrap heap operation'),
            ('kmem_alloc(8, KM_SLEEP);', 'Bootstrap heap requires KM_NOSLEEP'),
        ):
            body = '#include <sys/kmem.h>\n#include <uvm/uvm.h>\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); unsigned char *memory __unused = '
            body += 'kmem_alloc(37, KM_NOSLEEP);\n' + operation + '\n}\n'
            build_wrapper(body)
            out = run(work)
            assert heap_ready in out and 'NetBSD/RVM UVM panic: ' + error in out, out
            print('PASS: heap rejects ' + operation, flush=True)

        for name, operation, assembly, vector in (
            ('released heap span', 'kmem_free((void *)va, 8);',
             ' move.q (r2),r0\n rts\n', 5),
            ('non-executable heap', '', ' jsr (r2)\n rts\n', 2),
        ):
            body = '#include <sys/kmem.h>\nextern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); vaddr_t va = '
            body += '(vaddr_t)kmem_zalloc(8, KM_NOSLEEP);\n' + operation + '\n'
            body += 'rvm_test_probe(va); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert heap_ready in out and expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection for ' + name, flush=True)

        build_wrapper((Path(__file__).parent / 'bootkm.c').read_text())
        for pages in (256, 1024):
            configure(work, pages)
            out = run(work)
            assert 'MI wired kernel allocation and rollback verified.' in out, out
            print(f'PASS: MI km alignment, zero/reuse, VA-only reservations, batched free, '
                  f'limits, NOWAIT rollback and restored counts ({pages * 64} KiB)', flush=True)

        for operation in (
            'uvm_km_free(kernel_map, va, PAGE_SIZE, UVM_KMF_WIRED);',
            'uvm_km_free(kernel_map, va + PAGE_SIZE, PAGE_SIZE, UVM_KMF_WIRED);',
            'uvm_km_free(kernel_map, va, 2 * PAGE_SIZE, UVM_KMF_VAONLY);',
            'uvm_km_free(kernel_map, va, 2 * PAGE_SIZE, UVM_KMF_WIRED); '
            'uvm_km_free(kernel_map, va, 2 * PAGE_SIZE, UVM_KMF_WIRED);',
            'uvm_km_free(kernel_map, rvm_bootalloc(PAGE_SIZE), PAGE_SIZE, UVM_KMF_WIRED);',
        ):
            body = '#include <uvm/uvm.h>\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); vaddr_t va __unused = '
            body += ('uvm_km_alloc(kernel_map, 2 * PAGE_SIZE, 0, '
                     'UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);\n')
            body += operation + '\n}\n'
            build_wrapper(body)
            out = run(work)
            assert km_ready in out and ('NetBSD/RVM UVM panic: Invalid bootstrap '
                                       'UVM kernel allocation operation') in out, out
            print('PASS: MI km rejects invalid free: ' + operation, flush=True)

        for name, operation, assembly, vector in (
            ('freed MI km allocation',
             'uvm_km_free(kernel_map, va, PAGE_SIZE, UVM_KMF_WIRED);',
             ' move.q (r2),r0\n rts\n', 5),
            ('non-executable MI km allocation', '', ' jsr (r2)\n rts\n', 2),
        ):
            body = '#include <uvm/uvm.h>\nextern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); vaddr_t va = '
            body += ('uvm_km_alloc(kernel_map, PAGE_SIZE, 0, '
                     'UVM_KMF_WIRED | UVM_KMF_NOWAIT | UVM_KMF_ZERO);\n')
            body += '*(volatile uint64_t *)va = 0;\n' + operation + '\n'
            body += 'rvm_test_probe(va); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert km_ready in out and expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection for ' + name, flush=True)

        build_wrapper((Path(__file__).parent / 'bootalloc.c').read_text())
        for pages in (256, 1024):
            configure(work, pages)
            out = run(work)
            assert 'Wired buffer ownership and rollback verified.' in out, out
            print(f'PASS: wired buffers, fragmentation, zero-on-reuse, slot limits, '
                  f'RAM/VA exhaustion and restored resources ({pages * 64} KiB)', flush=True)

        for name, operation, assembly, vector in (
            ('freed wired buffer', 'rvm_bootfree(va);', ' move.q (r2),r0\n rts\n', 5),
            ('non-executable wired buffer', '', ' jsr (r2)\n rts\n', 2),
        ):
            body = 'extern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); vaddr_t va = rvm_bootalloc(1);\n'
            body += '*(volatile uint64_t *)va = 0;\n' + operation + '\n'
            body += 'rvm_test_probe(va); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert wired_ready in out and expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection for ' + name, flush=True)

        build_wrapper((Path(__file__).parent / 'uvm.c').read_text(), preload=True)
        out = run(work)
        assert 'UVM preload and stealing verified.' in out, out
        assert ('UVM diagnostic RVM -7 4294967295 -9223372036854775808 '
                '18446744073709551615 -9223372036854775808 '
                '18446744073709551615') in out, out
        assert 'UVM hex fedcba98 fedcba9876543210 fedcba9876543210 %' in out, out
        print('PASS: real UVM segment ownership, contiguous/rounded stealing, '
              'zeroing, duplicate handoff rejection and variadic diagnostics', flush=True)

        for operation, expected in (
            ('uvm_pagealloc_strat((struct uvm_object *)1, 0, NULL, 0, 0, 0);',
             'Object/anonymous page services are not available'),
            ('uvm_pagealloc_strat(NULL, 0, (struct vm_anon *)1, 0, 0, 0);',
             'Object/anonymous page services are not available'),
            ('struct vm_page *pg = uvm_pagealloc_strat(NULL, 0, NULL, '
             'UVM_PGA_USERESERVE, 0, 0); uvm_pagefree(pg); uvm_pagefree(pg);',
             'Invalid raw UVM page operation'),
        ):
            body = '#include <uvm/uvm.h>\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi);\n' + operation + '\n}\n'
            build_wrapper(body)
            out = run(work)
            assert ready in out and 'NetBSD/RVM UVM panic: ' + expected in out, out
            print('PASS: raw-page API rejection: ' + operation.split('(')[0], flush=True)

        for operation, expected in (
            ('uvm_page_physload(5, 5, 5, 5, VM_FREELIST_DEFAULT);',
             'NetBSD/RVM UVM panic: uvm_page_physload: start[5] >= end[5]'),
            ('uvm_page_physload(5, 6, 5, 6, VM_NFREELIST);',
             'NetBSD/RVM UVM panic: uvm_page_physload: bad free list 1'),
            ('pmap_steal_memory((vsize_t)-1, NULL, NULL);',
             'NetBSD/RVM UVM panic: Invalid UVM bootstrap allocation'),
            ('pmap_steal_memory((vsize_t)rvm_bootmem.ram_end, NULL, NULL);',
             'NetBSD/RVM UVM panic: No contiguous UVM bootstrap memory available'),
            ('uvm.page_init_done = true; pmap_steal_memory(PAGE_SIZE, NULL, NULL);',
             'NetBSD/RVM UVM panic: Invalid UVM bootstrap allocation'),
            ('kmutex_t lock; mutex_init(&lock, MUTEX_NODEBUG, IPL_VM); '
             'mutex_enter(&lock); mutex_enter(&lock);',
             'NetBSD/RVM UVM panic: Invalid bootstrap lock/IPL operation'),
        ):
            body = '#include <uvm/uvm.h>\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi);\n' + operation + '\n}\n'
            build_wrapper(body, preload=True)
            out = run(work)
            assert preload_ready in out and expected in out, out
            print('PASS: UVM rejection: ' + expected.split(': ')[-1], flush=True)

        for modification in ('version = 99', 'nsections = 17',
                             'section[0].size = ~0UL', 'loaded_end = 0',
                             'loader_stack = 0'):
            body = 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += '((struct rvm_bootinfo *)(uintptr_t)bi)->' + modification + ';\n'
            body += 'rvm_normal_early_init(bi); }\n'
            build_wrapper(body)
            out = run(work)
            assert 'Invalid loader memory handoff.' in out and ready not in out, out
            print('PASS: rejected boot record with ' + modification, flush=True)

        # All probes run after normal startup has enabled the hardware MMU.
        for name, pointer, assembly, vector in (
            ('null page', '0', ' move.q (r2),r0\n rts\n', 5),
            ('read-only text', '(uintptr_t)rvm_early_init', ' clr.q (r2)\n rts\n', 4),
            ('read-only data', '(uintptr_t)&kernel_pmap_ptr', ' clr.q (r2)\n rts\n', 4),
            ('non-executable data', 'RVM_BOOT_TEST_VA', ' jsr (r2)\n rts\n', 2),
        ):
            body = 'extern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); rvm_test_probe(' + pointer + '); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            assert ready in out, out
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection for ' + name, flush=True)

        # Access first, then change the PTE. This also detects stale translations.
        for name, operation, assembly, vector in (
            ('kernel write downgrade',
             'pmap_protect(pmap_kernel(), va, va + RVM_MMU_PAGE_SIZE, VM_PROT_READ);',
             ' clr.q (r2)\n rts\n', 4),
            ('kernel mapping removal', 'pmap_kremove(va, RVM_MMU_PAGE_SIZE);',
             ' move.q (r2),r0\n rts\n', 5),
            ('kernel PROT_NONE',
             'pmap_protect(pmap_kernel(), va, va + RVM_MMU_PAGE_SIZE, VM_PROT_NONE);',
             ' move.q (r2),r0\n rts\n', 5),
            ('kernel non-executable mapping', '', ' jsr (r2)\n rts\n', 2),
        ):
            body = 'extern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); vaddr_t va = VM_KERNEL_VM_BASE;\n'
            body += ('pmap_kenter_pa(va, rvm_boot_pmap.test_page, '
                     'VM_PROT_READ | VM_PROT_WRITE, 0); pmap_update(pmap_kernel());\n')
            body += '*(volatile uint64_t *)va = 0;\n' + operation + '\n'
            body += 'pmap_update(pmap_kernel()); rvm_test_probe(va); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert ready in out and expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection after ' + name, flush=True)


if __name__ == '__main__':
    main()
