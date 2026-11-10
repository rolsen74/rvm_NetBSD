#!/usr/bin/env python3
"""Check signed operand widths, relaxation boundaries and VM execution."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from pcrel_link import ROOT, executable


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cross = str(ROOT/'../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-')
    parser.add_argument('--as', dest='assembler', default=cross+'as')
    parser.add_argument('--ld', default=cross+'ld')
    parser.add_argument('--loader', type=Path, default=ROOT/'obj.rvm-bootstrap/bootloader.bin')
    args = parser.parse_args()
    marker = 'RVM compact signed operands verified'
    with tempfile.TemporaryDirectory(prefix='rvm-compact-test-') as directory:
        work = Path(directory)
        lines = ['.text', '.global _start', '_start:', ' jmp runtime']
        expected = {}
        def emit(instruction, data):
            name = f'check_{len(expected)}'
            lines.extend(['.global '+name, name+':', ' '+instruction])
            expected[name] = bytes(data)
        # Immediate values are independent of the operation size. Positive
        # values must not acquire the sign bit of the smaller signed mode.
        values = [0, 127, 128, 255, 256, 32767, 32768, 65535, 65536,
                  2**31-1, 2**31, 2**32-1, 2**32, 2**63-1, 2**63, 2**64-1,
                  -1, -128, -129, -32768, -32769, -65536,
                  -2**31, -2**31-1, -2**63]
        for value in values:
            signed_value = value if value < 2**63 else value-2**64
            signed = signed_value < 0
            width = next(w for w in (1, 2, 4, 8)
                         if (-2**(w*8-1) <= signed_value < 2**(w*8-1) if signed
                             else value < 2**(w*8)))
            mode = (60 if signed else 56) + (1, 2, 4, 8).index(width)
            for mnemonic, opcode in [('move.q', 0x27), ('cmp.q', 0x0f),
                                     ('add.q', 0x07), ('sub.q', 0x0b),
                                     ('eor.q', 0x1b), ('ori.q', 0x17)]:
                encoded = (value % (1 << (width*8))).to_bytes(width, 'big')
                emit(f'{mnemonic} #{value},r13', opcode.to_bytes(2,'big') + bytes([mode]) + encoded + b'\x00\x0d')
        displacements = {-65536, -29, 0}
        for width in range(1, 8):
            edge = 2**(width*8-1)
            displacements.update((edge-1, edge, -edge, -edge-1))
        displacements.update((-2**63, 2**63-1))
        for value in sorted(displacements):
            width = next(w for w in range(1, 9) if -2**(w*8-1) <= value < 2**(w*8-1))
            encoded = (value % (1 << (width*8))).to_bytes(width, 'big')
            emit(f'lea ({value},r14),r0', b'\x00\x5f'+bytes([19+width,14])+encoded+b'\x00\x00')
            emit(f'bne ({value},pc)', b'\x00\x87'+bytes([43+width])+encoded)
        # The reported backward -29 case, plus exact signed boundaries and
        # alignment which can absorb growth during assembler relaxation.
        lines += ['.global back29,branch29', 'back29:', '.space 29',
                  'branch29:', ' bne back29']
        branches = [('branch29', 'back29'), ('alignment_branch', 'aligned')]
        for index, gap in enumerate((0, 123, 124, 125, 126, 127, 128, 32760, 32767, 32768)):
            begin, end = f'forward_{index}', f'target_{index}'
            lines += [f'.global {begin},{end}', begin+':', ' bne '+end,
                      *([f' .space {gap}'] if gap else []), end+':', ' bne '+begin]
            branches.extend(((begin,end), (end,begin)))
        lines += ['.global alignment_branch', 'alignment_branch:', ' bne aligned',
                  '.balign 64', '.global aligned', 'aligned:', ' lea runtime,r10',
                  ' jsr helper', ' jmp runtime', 'helper:', ' rts']
        lines += ['runtime:', ' lea stack+4096,sp', ' move.q #-1,r10',
                  ' cmp.q #-1,r10', ' bne failed', ' move.q #-129,r10',
                  ' cmp.q #-129,r10', ' bne failed', ' add.q #129,r10',
                  ' bne failed', ' move.q #32768,r10', ' cmp.q #32768,r10',
                  ' bne failed', ' sub.q #65536,r10', ' cmp.q #-32768,r10',
                  ' bne failed', ' lea (65536,sp),r14', ' lea (-65536,r14),r11',
                  ' cmp.q sp,r11', ' bne failed', ' lea helper,r10', ' jsr (r10)',
                  ' lea message,r10', ' rclr.q r3',
                  'print_loop:', ' move.b (r10)+,r3', ' beq halt_loop',
                  ' move.q #3,r2', ' emufunc', ' jmp print_loop',
                  'halt_loop:', ' halt', ' jmp halt_loop',
                  'failed:', ' halt', ' jmp failed',
                  '.data', 'message:', f' .asciz "{marker}\\n"',
                  '.balign 8', 'stack:', ' .space 4096']
        text = '\n'.join(lines)+'\n'
        (work/'input.s').write_text(text)
        subprocess.run([args.assembler, 'input.s', '-o', 'input.o'], cwd=work, check=True)
        (work/'link.ld').write_text('SECTIONS { . = 0; .text : { *(.text) } .data : { *(.data) } }')
        subprocess.run([args.ld,'-T','link.ld','-e','_start','-Map','image.map',
                        '-o','netbsd.fhr','input.o'], cwd=work, check=True)
        symbols = {name: int(value,16) for value,name in re.findall(
            r'^\s+0x([0-9a-f]+)\s+(\w+)\s*$', (work/'image.map').read_text(), re.M)}
        sections, _ = executable(work/'netbsd.fhr')
        code = sections[0][1]
        for name, data in expected.items():
            actual = code[symbols[name]:symbols[name]+len(data)]
            assert actual == data, (name, actual.hex(), data.hex())
        for source, target in branches:
            offset = symbols[source]
            width = code[offset+2]-43
            assert 1 <= width <= 8
            delta = int.from_bytes(code[offset+3:offset+3+width], 'big', signed=True)
            assert offset+delta == symbols[target], (source, target, delta)
        assert code[symbols['branch29']:symbols['branch29']+4] == bytes.fromhex('00872ce3')
        assert code[symbols['forward_0']+2] == 0x2c
        assert code[symbols['target_9']+2] == 0x2e  # backward beyond signed16
        shutil.copyfile(args.loader, work/'bootloader.bin')
        (work/'vm.config').write_text((ROOT/'sys/arch/rvm/conf/vmemu.conf').read_text())
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        try:
            result = subprocess.run(['VMEmu','-c','vm.config'], cwd=work, env=env,
                                    capture_output=True, timeout=8)
            raise AssertionError('VM exited: '+str(result.returncode))
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'').decode(errors='replace')
            assert marker in output, output[-5000:]
        print(f'PASS: {len(expected)} immediate/displacement encodings, signed branch boundaries and VM execution')


if __name__ == '__main__':
    main()
