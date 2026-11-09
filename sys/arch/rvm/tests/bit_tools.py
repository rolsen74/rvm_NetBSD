#!/usr/bin/env python3
"""Check bit-op encodings, operands and GAS/VMAsm/VMDis round trips."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]


def code_section(obj):
    data = obj.read_bytes()
    assert data[:8] == b'FHR\0\x01\x01\x01\0'
    offset = 16
    while offset + 12 <= len(data):
        size = int.from_bytes(data[offset+4:offset+12], 'big')
        if data[offset:offset+3] == b'COD':
            return data[offset+18:offset+12+size]
        offset += 12 + size
    raise AssertionError('Missing code section')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assembler', default=str(ROOT/'../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-as'))
    args = parser.parse_args()
    cases = []
    for i, name in enumerate(['bclr', 'bset', 'bchg', 'btst']):
        test = name == 'btst'
        for suffix in ['', '.q']:
            cases += [(f'{name}{suffix} #63,r3', (0x164+i).to_bytes(2, 'big')+bytes([56, 63, 0, 3])),
                      (f'{name}{suffix} r2,(r3)', (0x164+i).to_bytes(2, 'big')+bytes([0, 2, 1, 3])),
                      (f'{name}{suffix} (r2),(r3)', (0x164+i).to_bytes(2, 'big')+bytes([1, 2, 1, 3])),
                      (f'r{name}{suffix} r2,r3'+('' if test else ',r4'),
                       (0x168+i).to_bytes(2, 'big')+bytes([2, 3] if test else [2, 3, 4]))]
            for bit in [0, 7, 8, 15, 16, 31, 32, 63]:
                cases.append((f'v{name}{suffix} #{bit},r3'+('' if test else ',r4'),
                              (0x6c+i).to_bytes(2, 'big')+bytes([bit, 3] if test else [bit, 3, 4])))
    with tempfile.TemporaryDirectory(prefix='rvm-bit-tools-') as directory:
        work = Path(directory)
        source = '\n'.join('\t'+line for line, _ in cases)+'\n'
        expected = b''.join(data for _, data in cases)
        (work/'test.s').write_text('.text\n'+source)
        (work/'test.vma').write_text(source)
        subprocess.run([args.assembler, str(work/'test.s'), '-o', str(work/'test.o')], check=True)
        assert code_section(work/'test.o') == expected, 'GAS bit-op encoding mismatch'
        subprocess.run(['VMAsm', '-i', str(work/'test.vma'), '-o', str(work/'test.bin'), '--bin'], check=True)
        assert (work/'test.bin').read_bytes() == expected, 'VMAsm bit-op encoding mismatch'
        subprocess.run(['VMDis', '--bin', '-y', '-i', str(work/'test.bin'), '-o', str(work/'roundtrip.vma')], check=True)
        subprocess.run(['VMAsm', '-i', str(work/'roundtrip.vma'), '-o', str(work/'roundtrip.bin'), '--bin'], check=True)
        assert (work/'roundtrip.bin').read_bytes() == expected, 'Bit disassembly did not round trip'
        invalid = []
        for name in ['bclr', 'bset', 'bchg', 'btst']:
            test = name == 'btst'
            invalid += [f'{name} #64,r1', f'{name} #-1,r1', f'{name} #3,#1',
                        f'{name} #1', f'{name} #1,r1,r2',
                        f'r{name} r64,r1'+('' if test else ',r2'),
                        f'r{name} #1,r1'+('' if test else ',r2'),
                        f'v{name} #64,r1'+('' if test else ',r2'),
                        f'v{name} #-1,r1'+('' if test else ',r2'),
                        f'v{name} r0,r1'+('' if test else ',r2'),
                        f'{name}.l #1,r1']
        for line in invalid:
            (work/'test.s').write_text('.text\n\t'+line+'\n')
            result = subprocess.run([args.assembler, str(work/'test.s'), '-o', str(work/'bad.o')], capture_output=True)
            assert result.returncode > 0, 'Accepted invalid instruction: '+line
    print('PASS: bit-op encodings, invalid operands and GAS/VMAsm/VMDis round trip')


if __name__ == '__main__':
    main()
