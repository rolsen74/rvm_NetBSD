#!/usr/bin/env python3
"""Build and run an FHR-native shared library with the normal guest libc."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*command, env=None):
    subprocess.run([str(item) for item in command], check=True, env=env)


def main():
    root = Path(__file__).resolve().parents[4]
    tests = Path(__file__).resolve().parent
    stand = tests.parent / 'stand'
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, default=root / 'obj.rvm-native-regcall')
    parser.add_argument('--kernel-objdir', type=Path,
                        help='kernel build containing netbsd.fhr and bootloader.bin')
    parser.add_argument('--toolchain', type=Path, default=root.parent / 'rvm_gcc/rvm_cross_netbsd')
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--pack-all-fhr', action='store_true',
                        help='pack guest programs, shared libraries, and kernel')
    args = parser.parse_args()
    obj = args.objdir.resolve()
    kernel_obj = args.kernel_objdir.resolve() if args.kernel_objdir else obj / 'kernel'
    cross = args.toolchain.resolve()
    cc = cross / 'bin/rvm-netbsd-gcc'
    ld = cross / 'bin/rvm-netbsd-ld'
    include = subprocess.check_output([str(cc), '-print-file-name=include'], text=True).strip()
    sysroot = obj / 'destdir.rvm'
    if not (sysroot / 'usr/lib/libc.a').is_file():
        raise SystemExit('Build the native libc first with ./build-userland.sh')
    with tempfile.TemporaryDirectory(prefix='rvm-fhr-so-guest-') as directory:
        work = Path(directory)
        first, second = work / 'first.o', work / 'second.o'
        shared = work / 'librvmtest.so'
        provider = work / 'librvmprovider.so'
        consumer = work / 'librvmconsumer.so'
        unresolved = work / 'librvmunresolved.so'
        missing = work / 'librvmmissing.so'
        cycle_a = work / 'librvmcyclea.so'
        cycle_b = work / 'librvmcycleb.so'
        lifecycle_provider = work / 'librvmlifecycleprovider.so'
        lifecycle = work / 'librvmlifecycle.so'
        program = work / 'fhr-so-smoke.fhr'
        provider_obj, consumer_obj = work / 'provider.o', work / 'consumer.o'
        lifecycle_provider_obj = work / 'lifecycle_provider.o'
        lifecycle_obj = work / 'lifecycle.o'
        for source, output in ((tests / 'fhr_so_first.c', first),
                               (tests / 'fhr_so_second.c', second),
                               (tests / 'fhr_so_provider.c', provider_obj),
                               (tests / 'fhr_so_consumer.c', consumer_obj),
                               (tests / 'fhr_so_lifecycle_provider.c', lifecycle_provider_obj),
                               (tests / 'fhr_so_lifecycle.c', lifecycle_obj)):
            run(cc, '-O2', '-c', source, '-o', output)
        run(sys.executable, stand / 'fhr_so.py', '--ld', ld,
            '-o', shared, first, second)
        run(sys.executable, stand / 'fhr_so.py', '--ld', ld,
            '-o', provider, provider_obj)
        run(sys.executable, stand / 'fhr_so.py', '--ld', ld,
            '--needed', 'librvmprovider.so',
            '-o', consumer, consumer_obj)
        run(sys.executable, stand / 'fhr_so.py', '--ld', ld,
            '-o', unresolved, consumer_obj)
        run(sys.executable, stand / 'fhr_so.py', '--ld', ld,
            '--needed', 'libnotpresent.so', '-o', missing, provider_obj)
        run(sys.executable, stand / 'fhr_so.py', '--ld', ld,
            '--needed', 'librvmcycleb.so', '-o', cycle_a, provider_obj)
        run(sys.executable, stand / 'fhr_so.py', '--ld', ld,
            '--needed', 'librvmcyclea.so', '-o', cycle_b, provider_obj)
        run(sys.executable, stand / 'fhr_so.py', '--ld', ld,
            '-o', lifecycle_provider, lifecycle_provider_obj)
        run(sys.executable, stand / 'fhr_so.py', '--ld', ld,
            '--needed', 'librvmlifecycleprovider.so',
            '-o', lifecycle, lifecycle_obj)
        source = tests / 'fhr_so_smoke.c'
        smoke_obj = work / 'fhr_so_smoke.o'
        env = dict(os.environ, RVM_REAL_CC=str(cc))
        run(sys.executable, stand / 'user_cc.py', '-O2', '-D__NetBSD__',
            '-nostdinc', '-isystem', sysroot / 'usr/include',
            '-isystem', include, '-c', source, '-o', smoke_obj, env=env)
        run(sys.executable, stand / 'user_prog.py', '--toolchain', cross,
            '--sysroot', sysroot, '-o', program, smoke_obj)
        password = work / 'master.passwd'
        password.write_text('root:$1$saltstri$qQY4WxjABChYG1ccLpfkz/:'
                            '0:0::0:0:System administrator:/root:/bin/sh\n')
        image = work / 'root'
        run(sys.executable, stand / 'make_native_root.py', '--objdir', obj,
            '--kernel-objdir', kernel_obj, '--output-dir', image,
            '--master-passwd', password,
            '--extra-file', f'usr/bin/fhr-so-smoke={program}',
            '--extra-file', f'usr/lib/librvmtest.so={shared}',
            '--extra-file', f'usr/lib/librvmprovider.so={provider}',
            '--extra-file', f'usr/lib/librvmconsumer.so={consumer}',
            '--extra-file', f'usr/lib/librvmunresolved.so={unresolved}',
            '--extra-file', f'usr/lib/librvmmissing.so={missing}',
            '--extra-file', f'usr/lib/librvmcyclea.so={cycle_a}',
            '--extra-file', f'usr/lib/librvmcycleb.so={cycle_b}',
            '--extra-file', f'usr/lib/librvmlifecycleprovider.so={lifecycle_provider}',
            '--extra-file', f'usr/lib/librvmlifecycle.so={lifecycle}',
            *(['--pack-all-fhr'] if args.pack_all_fhr else []))
        run(sys.executable, tests / 'native_root.py', '--objdir', obj,
            '--source-root', image, '--login', '--login-count', '1',
            '--timeout', str(args.timeout),
            '--guest-command', '/usr/bin/fhr-so-smoke',
            '--guest-marker', 'FHR DSO: symbols, dependencies, constructors, destructors and dlclose verified')
    print('PASS: FHR executable and shared-library dependencies mapped in the guest')


if __name__ == '__main__':
    main()
