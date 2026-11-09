#!/usr/bin/env python3
"""Build one FHR crt0 object without the unsupported ld -r operation."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from kernel_cc import adapt
from fhr_asm import normalize, raw_multiply_register
from static_init import extract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--entry', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('-o', type=Path, required=True)
    parser.add_argument('flags', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    flags = args.flags[1:] if args.flags[:1] == ['--'] else args.flags
    flags += ['-fno-pic', '-fno-pie', '-fno-stack-protector',
              '-fno-zero-initialized-in-bss']
    entry = args.o.with_suffix('.entry.s')
    raw = args.o.with_suffix('.raw.s')
    subprocess.run([args.cc, '-E', '-P'] + flags +
                   [str(args.entry), '-o', str(entry)], check=True)
    subprocess.run([args.cc, '-S'] + flags +
                   [str(args.source), '-o', str(raw)], check=True)
    source, weak = adapt(entry.read_text().replace('RVM_ASM_NL', '\n') +
                         raw.read_text(), '__rvm_csu_local__')
    source, entries = extract(source)
    assembly = args.o.with_suffix('.fhr.s')
    assembly.write_text(''.join(normalize(source, '__rvm_csu_lc',
                                         raw_multiply_register(args.cc))))
    subprocess.run([args.cc, '-c', str(assembly), '-o', str(args.o)], check=True)
    args.o.with_suffix('.weak.json').write_text(json.dumps(weak) + '\n')
    args.o.with_suffix('.init.json').write_text(json.dumps(entries) + '\n')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
