#!/usr/bin/env python3
"""Compile static RVM userland with the FHR binding/initializer adapters.

RVM GAS uses semicolons for comments. Machine assembly macros therefore use
RVM_ASM_NL, which this driver expands after the C preprocessor has run.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
from kernel_cc import adapt, private_prefix
from fhr_asm import normalize, raw_multiply_register
from static_init import extract


def main():
    cc = os.environ.get('RVM_REAL_CC', 'rvm-netbsd-gcc')
    args = sys.argv[1:]
    sources = [x for x in args if x.endswith(('.c', '.S')) and not x.startswith('-')]
    if '-c' not in args or len(sources) != 1:
        os.execvp(cc, [cc] + args)
    if '-o' not in args:
        args += ['-o', Path(sources[0]).with_suffix('.o').name]
    target = Path(args[args.index('-o') + 1])
    # Initialized zero data keeps FHR's storage ordering deterministic.
    flags = ['-D__NetBSD__', '-fno-pic', '-fno-pie', '-fno-stack-protector',
             '-fno-zero-initialized-in-bss'] + args
    os.environ['RVM_USER_INITIALIZERS'] = '1'
    if sources[0].endswith('.c'):
        os.execv(sys.executable, [sys.executable, str(Path(__file__).with_name('kernel_cc.py'))] + flags)
    raw = target.with_suffix('.raw.s')
    flags[flags.index('-c')] = '-E'
    flags[flags.index('-o') + 1] = str(raw)
    subprocess.run([cc, '-P'] + flags, check=True)
    prefix = private_prefix(target)
    source, weak = adapt(raw.read_text().replace('RVM_ASM_NL', '\n'), prefix)
    source, entries = extract(source)
    assembly = target.with_suffix('.fhr.s')
    assembly.write_text(''.join(normalize(source, prefix + 'lc', raw_multiply_register(cc))))
    subprocess.run([cc, '-c', str(assembly), '-o', str(target)], check=True)
    target.with_suffix('.init.json').write_text(json.dumps(entries) + '\n')
    target.with_suffix('.weak.json').write_text(json.dumps(weak) + '\n')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
