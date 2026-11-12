#!/usr/bin/env python3
"""Check both packed FHR size widths and byte orders."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'stand'))
from fhr_pack import pack, unpack


def main():
    for size in (0, 1):
        for endian in (0, 1):
            width = 4 if size == 0 else 8
            original = b'FHR\0' + bytes((size, endian, 2, 0)) + bytes(8) + b'abc' * 1000
            packed = pack(original)
            assert packed[7] == 1
            assert int.from_bytes(packed[16:16 + width], 'big') == len(packed) - 16 - 2 * width
            assert int.from_bytes(packed[16 + width:16 + 2 * width], 'big') == len(original)
            assert unpack(packed) == original
            damaged = bytearray(packed)
            damaged[-1] ^= 1  # zlib Adler-32 checksum
            for invalid in (packed[:-1], packed + b'X', bytes(damaged)):
                try:
                    unpack(invalid)
                except ValueError:
                    pass
                else:
                    raise AssertionError('invalid packed FHR accepted')
    print('PASS: FHR32/FHR64 zlib wrappers, both endian markers and corrupt data')


if __name__ == '__main__':
    main()
