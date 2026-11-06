#!/usr/bin/env python3
"""Reject RVM compilers with known bootstrap code-generation defects.

Moves and arithmetic change the VM's flags. A reverse traversal exposed
GCC emitting CMP; ADD; BNE, which loops past the array and frees garbage.
The maintained backend now emits each compare/branch as one RTL instruction.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='rvm-compiler-check-') as directory:
    source = Path(directory) / 'backend.c'
    assembly = source.with_suffix('.s')
    source.write_text('''unsigned long array_offset(unsigned int n) {
    return (unsigned long)n * 356;
}
extern void release(void *);
void reverse(void **slots, unsigned int n) {
    while (n != 0)
        release(slots[--n]);
}
''')
    result = subprocess.run([args.cc, '-O2', '-ffreestanding', '-fno-builtin',
                    '-fno-pic', '-fno-pie', '-nostdinc', '-S', str(source),
                    '-o', str(assembly)])
    if result.returncode:
        raise SystemExit('RVM GCC failed the -O2 backend probe. Rebuild GCC with '
                         'the widening-multiply fix in rvm-inst.md and '
                         'rvm-inst-v.md, then clean-build NetBSD.')
    previous = ''
    branches = 0
    for line in assembly.read_text().splitlines():
        instruction = line.strip()
        if not instruction or instruction.startswith(('.', '#')) or instruction.endswith(':'):
            continue
        if re.match(r'b(?:eq|ne|lt|cs|gt|hi|ge|cc|le|ls)\s', instruction):
            branches += 1
            if not re.match(r'(?:r|v)?(?:cmp|tst)\.[bwlq]\s', previous):
                raise SystemExit(
                    'RVM GCC has unsafe condition-code ordering: ' + previous +
                    ' ; ' + instruction + '\nRebuild the installed GCC with the '
                    'rvm-brance.md atomic compare/branch fix, then clean-build NetBSD.')
        previous = instruction
    if not branches:
        raise SystemExit('RVM compiler probe emitted no conditional branches; review the check')
