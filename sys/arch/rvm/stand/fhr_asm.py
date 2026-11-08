#!/usr/bin/env python3
"""Normalize GCC assembly for FHR without editing MI C or the toolchain.

FHR currently loses .LC string labels and mishandles .comm storage. Give
string labels ordinary names and emit common storage as zero-filled data,
matching the explicit .data workaround already used by the RVM bootstrap.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile


def raw_multiply_register(cc):
    """Detect GAS emitting a raw register where MUL expects a mode byte."""
    with tempfile.TemporaryDirectory(prefix='rvm-mul-encoding-') as directory:
        source = Path(directory) / 'mul.s'
        target = source.with_suffix('.o')
        source.write_text('.text\n.global probe\nprobe:\n muls.q #356,r11\n')
        subprocess.run([cc, '-nostdinc', '-c', str(source), '-o', str(target)], check=True)
        obj = target.read_bytes()
        if obj[:8] != b'FHR\0\x01\x01\x01\x00':
            raise SystemExit('Unexpected FHR multiply probe header')
        # Relocatable objects have sequential chunks. COD's payload starts
        # with a four-byte alignment and two-byte section symbol index.
        chunk = 16
        code = None
        while chunk + 12 <= len(obj):
            size = int.from_bytes(obj[chunk + 4:chunk + 12], 'big')
            if obj[chunk:chunk + 3] == b'COD' and size >= 6:
                code = obj[chunk + 18:chunk + 12 + size]
                break
            chunk += 12 + size
        if code is None:
            raise SystemExit('Missing multiply probe code section')
        if code == bytes.fromhex('004b3901640b'):
            return True
        if code == bytes.fromhex('004b390164000b'):
            return False
        raise SystemExit('Unknown multiply encoding; review the FHR assembler adapter')


def _normalize_sections(source, prefix, raw_mul=False):
    local = set(re.findall(r'^\s*\.local\s+([\w.]+)\s*$', source, re.MULTILINE))
    section = '\t.text\n'
    for line in source.splitlines(keepends=True):
        common = re.fullmatch(r'\s*\.comm\s+([\w.]+),\s*(\d+),\s*(\d+)\s*', line)
        if common:
            name, size, alignment = common.groups()
            if name not in local:
                yield f'\t.globl {name}\n'
            yield f'\t.data\n\t.balign {max(4, int(alignment))}\n{name}:\n\t.zero {size}\n'
            # .comm allocates independent storage without changing the active
            # section. Functions can follow it without another .text directive.
            yield section
        elif re.match(r'^\s*(?:\.bss\s*$|\.section\s+\.bss(?:,|\s|$))', line):
            section = '\t.data\n'
            yield '\t.data\n'
        elif re.match(r'^\s*\.section\s+\.data(?:\b|\.)', line):
            section = '\t.data\n'
            yield section
        elif re.match(r'^\s*\.section\s+\.rodata(?:\b|\.)', line):
            section = '\t.section .rodata\n'
            yield section
        elif re.match(r'^\s*\.section\s+\.text(?:\b|\.)', line):
            section = '\t.text\n'
            yield section
        elif line.lstrip().startswith(('.string', '.ascii', '.asciz')):
            yield line
        else:
            if re.match(r'^\s*\.(?:text|data|section)\b', line):
                section = line
            line = re.sub(r'(?<![\w.])\.LC(\d+)\b', prefix + r'\1', line)
            label = re.fullmatch(r'(' + re.escape(prefix) + r'\d+):\s*', line)
            if label:
                yield f'\t.globl {label[1]}\n'
            multiply = re.fullmatch(
                r'\s*(mulu|muls)\.([bwlq])\s+(.+),\s*r([0-9]+)\s*', line)
            if raw_mul and multiply:
                opcode, width, operand, register = multiply.groups()
                # GAS's AT_IReg writes one byte, while the VM's RegMode
                # decoder needs OAM_Reg (0), then the register number.
                # A raw r0 supplies that mode byte; append the real register.
                yield f'\t{opcode}.{width}\t{operand},r0\n\t.byte {register}\n'
            else:
                yield line


def storage_first(source):
    """Define writable symbols before RVM GAS parses absolute memory uses.

    Repeated forward references in absolute destination operands can otherwise
    acquire a relocation to the wrong section. Symbolic LEA/data initializers
    use the normal fixup path. Keep this workaround inside the FHR adapter.
    """
    # Repeat public binding beside the definition: a declaration before a
    # section switch can move after its symbol when storage is partitioned.
    # FHR GAS also needs the declaration in the symbol's own section for
    # generated read-only labels, so a single declaration header is insufficient.
    public = set(re.findall(r'^\s*\.(?:global|globl)\s+([\w.]+)', source, re.M))
    remaining = []
    for line in source.splitlines(keepends=True):
        label = re.fullmatch(r'([\w.]+):\s*', line)
        if label and label[1] in public:
            remaining.append(f'\t.global {label[1]}\n')
        remaining.append(line)
    source = ''.join(remaining)
    header, chunks, lines, kind = [], [], [], None
    for line in source.splitlines(keepends=True):
        section = re.match(r'^\s*\.(text|data|section)\b(.*)', line)
        if section:
            if kind is None:
                header = lines
            else:
                chunks.append((kind, lines))
            writable = section[1] == 'data' or (
                section[1] == 'section' and re.match(r'\s+\.data(?:\b|\.)', section[2]))
            kind, lines = bool(writable), [line]
        else:
            lines.append(line)
    if kind is None:
        return source
    chunks.append((kind, lines))
    return ''.join(header + [line for writable in (True, False)
                             for kind, lines in chunks if kind == writable
                             for line in lines])


def normalize(source, prefix, raw_mul=False):
    yield storage_first(''.join(_normalize_sections(source, prefix, raw_mul)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('target', type=Path)
    parser.add_argument('--cc')
    args = parser.parse_args()
    source, target = args.source, args.target
    prefix = 'rvm_' + re.sub(r'\W', '_', source.name.split('.')[0]) + '_lc'
    raw_mul = raw_multiply_register(args.cc) if args.cc else False
    target.write_text(''.join(normalize(source.read_text(), prefix, raw_mul)))
