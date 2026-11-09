#!/usr/bin/env python3
"""Check control-register encodings and the GAS/VMAsm/VMDis round trip."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]


def code_section(obj):
    data = obj.read_bytes()
    assert data[:8] == b'FHR\0\x01\x01\x01\0', 'Expected a relocatable FHR object'
    offset = 16
    while offset+12 <= len(data):
        size = int.from_bytes(data[offset+4:offset+12], 'big')
        if data[offset:offset+3] == b'COD':
            return data[offset+18:offset+12+size]
        offset += 12+size
    raise AssertionError('Missing code section')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assembler', default=str(ROOT/'../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-as'))
    parser.add_argument('--vmasm', default='VMAsm')
    parser.add_argument('--vmdis', default='VMDis')
    args = parser.parse_args()
    names = 'SR PC CR USP SSP VBR MST MUT INST INTE UD1 UD2 FPCR FPSR'.split()
    lines, expected = [], bytearray()
    for number, name in enumerate(names):
        lines += [f'\tmfctrl {name.lower()},r0\n', f'\tMtCtrl sp,{name}\n']
        expected += bytes([0, 0x69, 0, number, 0, 0, 0x68, 63, 0, number])
    for name, opcode in [('fpushm', 0x1c2), ('fpopm', 0x1c0)]:
        lines.append('\t'+name+' f0/f63\n')
        expected += opcode.to_bytes(2, 'big') + (0x8000000000000001).to_bytes(8, 'big')
    with tempfile.TemporaryDirectory(prefix='rvm-control-tools-') as directory:
        work = Path(directory)
        gas, native = work/'gas.s', work/'native.vma'
        gas.write_text('.text\n'+''.join(lines))
        native.write_text(''.join(lines))
        obj, binary = work/'gas.o', work/'native.bin'
        subprocess.run([args.assembler, str(gas), '-o', str(obj)], check=True)
        subprocess.run([args.vmasm, '-i', str(native), '-o', str(binary), '--bin'], check=True)
        assert code_section(obj) == expected, 'GAS changed the control-register ABI'
        assert binary.read_bytes() == expected, 'VMAsm changed the control-register ABI'
        assembly = work/'roundtrip.vma'
        subprocess.run([args.vmdis, '--bin', '-y', '-i', str(binary), '-o', str(assembly)], check=True)
        output = assembly.read_text()
        for name in ('FPCR', 'FPSR'):
            assert output.count(name) == 2, 'Missing disassembler register name '+name
        assert 'FPushm' in output and 'FPopm' in output, 'Missing FP mask mnemonics'
        roundtrip = work/'roundtrip.bin'
        subprocess.run([args.vmasm, '-i', str(assembly), '-o', str(roundtrip), '--bin'], check=True)
        assert roundtrip.read_bytes() == expected, 'Control-register disassembly did not round trip'
        invalid = ['mfctrl FPCRjunk,r0', 'mtctrl r0,FPSR_', 'mtctrl r0,FPCR.foo',
                   'mfctrl r0,FPCR', 'mtctrl FPSR,r0', 'mfctrl FPCR', 'mtctrl r0',
                   'mfctrl FPSR,r64', 'mtctrl f0,FPCR', 'mfctrl FPCR,r0,r1',
                   'fpush f0', 'fpop f0']
        for line in invalid:
            gas.write_text('.text\n\t'+line+'\n')
            native.write_text('\t'+line+'\n')
            for command in ([args.assembler, str(gas), '-o', str(work/'bad.o')],
                            [args.vmasm, '-i', str(native), '-o', str(work/'bad.bin'), '--bin']):
                result = subprocess.run(command, capture_output=True)
                assert result.returncode > 0, 'Accepted invalid control instruction: '+line
                assert b'Segmentation fault' not in result.stderr
    print('PASS: control-register encoding, invalid operands and GAS/VMAsm/VMDis round trip')


if __name__ == '__main__':
    main()
