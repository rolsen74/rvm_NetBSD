#!/usr/bin/env python3
"""Combine RVM FHR objects into a native FHR shared-library image.

The shared image uses the COD1/DAT1/BSS1 and SYM0/REL0 records from a
relocatable FHR object.  Its header identifies type 2 (shared), and records
the actual number of loadable sections.  The runtime loader resolves the
remaining imports when it maps the image.  An optional LIB0 record after SYM0
names dependencies by SYM0 index, as defined by SnakeOS FHR.h.
"""

import argparse
from pathlib import Path
import os
import subprocess
import tempfile
from fhr_pack import pack as pack_zlib

SECTION_IDS = {b'COD\x01', b'DAT\x01', b'BSS\x01'}


def integer(data, cursor):
    if cursor >= len(data):
        raise ValueError('truncated FHR integer')
    mask = data[cursor]
    cursor += 1
    if mask < 0x80:
        return mask, cursor
    value = 0
    for bit in range(8):
        value <<= 8
        if mask & (1 << bit):
            if cursor >= len(data):
                raise ValueError('truncated FHR integer')
            value |= data[cursor]
            cursor += 1
    return value, cursor


def encode_integer(value):
    if value < 0x80:
        return bytes((value,))
    return b'\xff' + value.to_bytes(8, 'big')


def add_libraries(data, section_count, dependencies):
    start = 16 + 12 * section_count
    if data[start:start + 4] != b'SYM\0' or start + 12 > len(data):
        raise ValueError('shared image lacks the FHR string table')
    length = int.from_bytes(data[start + 4:start + 12], 'big')
    end = start + 12 + length
    if end > len(data):
        raise ValueError('truncated FHR string table')
    names = bytes(data[start + 12:end])
    count, cursor = integer(names, 0)
    entries_start = cursor
    for _ in range(count):
        size, cursor = integer(names, cursor)
        if not size or cursor + size > len(names) or names[cursor + size - 1] != 0:
            raise ValueError('invalid FHR string table')
        cursor += size
    entries_end = cursor
    terminator, cursor = integer(names, cursor)
    if terminator or cursor != len(names) or count + len(dependencies) > 65536:
        raise ValueError('invalid FHR string table')
    extra = b''.join(encode_integer(len(name) + 1) + name + b'\0'
                     for name in dependencies)
    names = (encode_integer(count + len(dependencies)) +
             names[entries_start:entries_end] + extra + b'\0')
    symbol = b'SYM\0' + len(names).to_bytes(8, 'big') + names
    libraries = encode_integer(len(dependencies)) + b''.join(
        encode_integer(count + i) + encode_integer(0)
        for i in range(1, len(dependencies) + 1))
    record = b'LIB\0' + len(libraries).to_bytes(8, 'big') + libraries
    data[start:end] = symbol + record


def check_image(data):
    if (len(data) < 16 or data[:8] != b'FHR\0\x01\x01\x01\0'
            or data[8:14] != b'\0\0\x01\0\x01\0'):
        raise ValueError('expected a big-endian RVM FHR64 relocatable object')
    cursor = 16
    sections = []
    if data[14:16] != b'\0\0':
        raise ValueError('relocatable FHR input unexpectedly has a section table')
    seen_strings = False
    while cursor < len(data):
        if cursor + 12 > len(data):
            raise ValueError('truncated FHR chunk header')
        kind = bytes(data[cursor:cursor + 4])
        length = int.from_bytes(data[cursor + 4:cursor + 12], 'big')
        end = cursor + 12 + length
        if end > len(data):
            raise ValueError('FHR chunk exceeds file size')
        if kind == b'SYM\0' and not seen_strings and not sections:
            seen_strings = True
            cursor = end
            continue
        if kind not in SECTION_IDS or not seen_strings:
            raise ValueError('unexpected FHR chunk outside a section')
        payload = data[cursor + 12:end]
        if kind == b'BSS\x01':
            if length != 14:
                raise ValueError('invalid FHR BSS1 section')
            size = int.from_bytes(payload[:8], 'big')
            align_and_flags = payload[8:12]
        else:
            if length < 6:
                raise ValueError('invalid FHR code/data section')
            size = length - 6
            align_and_flags = payload[:4]
        if not size:
            raise ValueError('zero-sized FHR section')
        sections.append(size.to_bytes(8, 'big') + align_and_flags)
        cursor = end
        while True:
            if cursor + 4 > len(data):
                raise ValueError('section lacks END0')
            kind = bytes(data[cursor:cursor + 4])
            if kind == b'END\0':
                cursor += 4
                break
            if cursor + 12 > len(data):
                raise ValueError('truncated section subchunk')
            length = int.from_bytes(data[cursor + 4:cursor + 12], 'big')
            if kind != b'REL\0' or length > len(data) - cursor - 12:
                raise ValueError('invalid section relocation chunk')
            cursor += 12 + length
    if not sections or len(sections) > 32:
        raise ValueError('shared image needs 1 to 32 sections')
    return sections


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ld', type=Path, required=True)
    parser.add_argument('-o', '--output', type=Path, required=True)
    parser.add_argument('--zlib', action='store_true',
                        help='wrap the shared library in the FHR zlib format')
    parser.add_argument('--needed', action='append', default=[], metavar='LIBRARY',
                        help='load this library from the same directory before relocation')
    parser.add_argument('objects', nargs='+', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='rvm-fhr-so-') as directory:
        linked = Path(directory) / 'linked.o'
        # Keep GCC's startup/exit code sections distinct.  The current RVM
        # partial linker loses section-relative addends when folding them
        # into .text, which otherwise redirects .ctors/.dtors to offset zero.
        subprocess.run([str(args.ld.resolve()), '-r', '--unique=.text.*',
                        *map(lambda path: str(path.resolve()), args.objects),
                        '-o', str(linked)], check=True)
        data = bytearray(linked.read_bytes())
    sections = check_image(data)
    data[6] = 2  # FHR_TYPE_Shared
    data[14:16] = len(sections).to_bytes(2, 'big')
    data[16:16] = b''.join(sections)
    if args.needed:
        if len(args.needed) > 32 or len(set(args.needed)) != len(args.needed):
            raise SystemExit('expected at most 32 distinct FHR dependencies')
        names = []
        for name in args.needed:
            if (not name or name in ('.', '..') or '/' in name or '\\' in name or
                    len(name) > 127 or
                    any(ord(char) < 0x21 or ord(char) > 0x7e for char in name)):
                raise SystemExit('FHR dependencies must be short library filenames')
            names.append(name.encode('ascii'))
        add_libraries(data, len(sections), names)
    if args.zlib:
        data = pack_zlib(data)
    staged = output.with_name(output.name + '.new')
    try:
        staged.write_bytes(data)
        os.replace(staged, output)
    finally:
        staged.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
