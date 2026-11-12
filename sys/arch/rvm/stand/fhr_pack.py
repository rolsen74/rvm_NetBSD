#!/usr/bin/env python3
"""Pack or unpack an FHR file using the FHR zlib wrapper."""

import argparse
import os
from pathlib import Path
import zlib

HEADER_SIZE = 16
PACK_ZLIB = 1


def _header(data):
    if len(data) < HEADER_SIZE or data[:4] != b'FHR\0' or data[4] not in (0, 1):
        raise ValueError('invalid FHR header')
    if data[5] not in (0, 1):
        raise ValueError('invalid FHR byte order')
    return 4 if data[4] == 0 else 8


def pack(data, level=9):
    width = _header(data)
    if data[7] != 0:
        raise ValueError('FHR file is already packed')
    compressed = zlib.compress(data[HEADER_SIZE:], level)
    limit = 1 << (8 * width)
    if len(compressed) >= limit or len(data) >= limit:
        raise ValueError('FHR size does not fit its size fields')
    header = bytearray(data[:HEADER_SIZE])
    header[7] = PACK_ZLIB
    return (bytes(header) + len(compressed).to_bytes(width, 'big') +
            len(data).to_bytes(width, 'big') + compressed)


def unpack(data):
    width = _header(data)
    if data[7] != PACK_ZLIB:
        raise ValueError('FHR file is not zlib packed')
    offset = HEADER_SIZE + 2 * width
    if len(data) < offset:
        raise ValueError('truncated packed FHR header')
    packed_size = int.from_bytes(data[HEADER_SIZE:HEADER_SIZE + width], 'big')
    full_size = int.from_bytes(data[HEADER_SIZE + width:offset], 'big')
    if packed_size != len(data) - offset or full_size < HEADER_SIZE:
        raise ValueError('invalid packed FHR lengths')
    decompressor = zlib.decompressobj()
    try:
        body = decompressor.decompress(data[offset:], full_size - HEADER_SIZE + 1)
    except zlib.error as exc:
        raise ValueError('invalid packed FHR zlib stream') from exc
    if (len(body) != full_size - HEADER_SIZE or not decompressor.eof or
            decompressor.unused_data or decompressor.unconsumed_tail):
        raise ValueError('invalid packed FHR zlib stream')
    header = bytearray(data[:HEADER_SIZE])
    header[7] = 0
    return bytes(header) + body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('-d', '--decompress', action='store_true')
    parser.add_argument('-o', '--output', required=True, type=Path)
    parser.add_argument('input', type=Path)
    args = parser.parse_args()
    source = args.input.read_bytes()
    result = unpack(source) if args.decompress else pack(source)
    staged = args.output.with_name(args.output.name + '.new')
    try:
        staged.write_bytes(result)
        staged.chmod(args.input.stat().st_mode & 0o777)
        os.replace(staged, args.output)
    finally:
        staged.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
