#!/usr/bin/env python3
"""Build an executable that exercises FHR native shared libraries."""

import argparse
import os
from pathlib import Path
import subprocess
import sys

def run(*command, env=None):
    subprocess.run([str(item) for item in command], check=True, env=env)


def main():
    root = Path(__file__).resolve().parents[4]
    stand = Path(__file__).resolve().parent
    tests = stand.parent / 'tests'
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path, required=True)
    args = parser.parse_args()
    obj, cross = args.objdir.resolve(), args.toolchain.resolve()
    out = obj / 'fhr-demo'
    out.mkdir(parents=True, exist_ok=True)
    cc = cross / 'bin/rvm-netbsd-gcc'
    ld = cross / 'bin/rvm-netbsd-ld'
    sysroot = obj / 'destdir.rvm'
    gcc_include = subprocess.check_output(
        [str(cc), '-print-file-name=include'], text=True).strip()
    objects = {}
    for name in ('first', 'second', 'provider', 'consumer',
                 'lifecycle_provider', 'lifecycle'):
        target = out / (name + '.o')
        run(cc, '-O2', '-c', tests / ('fhr_so_' + name + '.c'), '-o', target)
        objects[name] = target
    libraries = {
        'librvmtest.so': ('first', 'second'),
        'librvmprovider.so': ('provider',),
        'librvmconsumer.so': ('consumer',),
        'librvmunresolved.so': ('consumer',),
        'librvmmissing.so': ('provider',),
        'librvmcyclea.so': ('provider',),
        'librvmcycleb.so': ('provider',),
        'librvmlifecycleprovider.so': ('lifecycle_provider',),
        'librvmlifecycle.so': ('lifecycle',),
    }
    for name, members in libraries.items():
        command = [sys.executable, stand / 'fhr_so.py', '--ld', ld]
        if name == 'librvmconsumer.so':
            command.extend(('--needed', 'librvmprovider.so'))
        if name == 'librvmlifecycle.so':
            command.extend(('--needed', 'librvmlifecycleprovider.so'))
        if name == 'librvmmissing.so':
            command.extend(('--needed', 'libnotpresent.so'))
        if name == 'librvmcyclea.so':
            command.extend(('--needed', 'librvmcycleb.so'))
        if name == 'librvmcycleb.so':
            command.extend(('--needed', 'librvmcyclea.so'))
        run(*command, '-o', out / name, *(objects[member] for member in members))
    smoke = out / 'fhr-so-demo.o'
    env = dict(os.environ, RVM_REAL_CC=str(cc))
    run(sys.executable, stand / 'user_cc.py', '-O2', '-D__NetBSD__',
        '-nostdinc', '-isystem', sysroot / 'usr/include',
        '-isystem', gcc_include, '-c', tests / 'fhr_so_smoke.c',
        '-o', smoke, env=env)
    run(sys.executable, stand / 'user_prog.py', '--toolchain', cross,
        '--sysroot', sysroot, '-o', out / 'fhr-so-demo', smoke)
    print('FHR shared-library demo:', out / 'fhr-so-demo')


if __name__ == '__main__':
    main()
