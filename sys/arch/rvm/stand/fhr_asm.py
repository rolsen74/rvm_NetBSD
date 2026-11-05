#!/usr/bin/env python3
"""Normalize GCC assembly for FHR without editing MI C or the toolchain.

FHR currently loses .LC string labels and mishandles .comm storage. Give
string labels ordinary names and emit common storage as zero-filled data,
matching the explicit .data workaround already used by the RVM bootstrap.
"""
from pathlib import Path
import re
import sys


def normalize(source, prefix):
    local = set(re.findall(r'^\s*\.local\s+([\w.]+)\s*$', source, re.MULTILINE))
    for line in source.splitlines(keepends=True):
        common = re.fullmatch(r'\s*\.comm\s+([\w.]+),\s*(\d+),\s*(\d+)\s*', line)
        if common:
            name, size, alignment = common.groups()
            if name not in local:
                yield f'\t.globl {name}\n'
            yield f'\t.data\n\t.balign {max(4, int(alignment))}\n{name}:\n\t.zero {size}\n'
        elif re.match(r'^\s*(?:\.bss\s*$|\.section\s+\.bss(?:,|\s|$))', line):
            yield '\t.data\n'
        elif line.lstrip().startswith(('.string', '.ascii', '.asciz')):
            yield line
        else:
            line = re.sub(r'(?<![\w.])\.LC(\d+)\b', prefix + r'\1', line)
            label = re.fullmatch(r'(' + re.escape(prefix) + r'\d+):\s*', line)
            if label:
                yield f'\t.globl {label[1]}\n'
            yield line


if __name__ == '__main__':
    source, target = (Path(arg) for arg in sys.argv[1:])
    prefix = 'rvm_' + re.sub(r'\W', '_', source.name.split('.')[0]) + '_lc'
    target.write_text(''.join(normalize(source.read_text(), prefix)))
