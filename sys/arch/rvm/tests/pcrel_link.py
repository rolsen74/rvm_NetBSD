#!/usr/bin/env python3
"""Verify same-output-section address relaxation and loader relocations."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT/'sys/arch/rvm/stand'))
from fhr_asm import normalize


def executable(path):
    data = path.read_bytes()
    count = int.from_bytes(data[14:16], 'big')
    headers = [(int.from_bytes(data[16+12*i:24+12*i], 'big'),
                int.from_bytes(data[26+12*i:28+12*i], 'big')) for i in range(count)]
    position = 16 + 12*count
    sections, relocations = [], set()
    def number():
        nonlocal position
        tag = data[position]; position += 1
        if tag < 128:
            return tag
        value = 0
        for bit in range(8):
            value <<= 8
            if tag & (1 << bit):
                value |= data[position]; position += 1
        return value
    for index, (size, flags) in enumerate(headers):
        tag = data[position:position+4]; position += 4
        if tag == b'BSS\0':
            sections.append((flags, bytes(size)))
            assert data[position:position+4] == b'END\0'; position += 4
            continue
        assert tag in (b'COD\0', b'DAT\0'), tag
        length = int.from_bytes(data[position:position+8], 'big'); position += 8
        end = position + length
        position += 2  # CRC16.
        contents = data[position:position+size]; position += size
        sections.append((flags, contents))
        while True:
            count = number()
            if not count:
                break
            target, kind = number(), number()
            assert kind == 0
            for _ in range(count):
                relocations.add((index, number(), target))
        assert position == end
        assert data[position:position+4] == b'END\0'; position += 4
    assert position == len(data)
    return sections, relocations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cross = str(ROOT/'../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-')
    parser.add_argument('--ld', default=cross+'ld')
    parser.add_argument('--baseline-ld')
    parser.add_argument('--cc', default=cross+'gcc')
    parser.add_argument('--nm', default=cross+'nm')
    parser.add_argument('--ar', default=cross+'ar')
    parser.add_argument('--loader', type=Path, default=ROOT/'obj.rvm-bootstrap/bootloader.bin')
    args = parser.parse_args()
    marker = 'RVM PC-relative links and absolute pointers verified'
    with tempfile.TemporaryDirectory(prefix='rvm-pcrel-test-') as directory:
        work = Path(directory)
        sources = {
            'a': '''.text
.global _start,backward,body,code_lea,cross_call,same_call,data_lea,ro_lea,literal_ptr
_start:
 lea stack+4096,sp
 jmp body
backward:
 move.q #19,r0
 rts
body:
same_call:
 jsr backward
 cmp.q #19,r0
 bne failed
code_lea:
 lea callee,r10
cross_call:
 jsr callee
 cmp.q #37,r0
 bne failed
 lea literal_ptr,r11
 move.q (r11),r11
 cmp.q r10,r11
 bne failed
 jsr (r10)
 cmp.q #37,r0
 bne failed
data_lea:
 lea data_value,r12
 cmp.q #123,(r12)
 bne failed
ro_lea:
 lea ro_value,r13
 cmp.q #456,(r13)
 bne failed
 lea message,r10
 rclr.q r3
print_loop:
 move.b (r10)+,r3
 beq stopped
 move.q #3,r2
 emufunc
 jmp print_loop
failed:
 halt
 jmp failed
stopped:
 halt
 jmp stopped
.byte 0x00,0x5f,0x37
literal_ptr:
 .quad callee
.data
.global data_value,data_code_ptr
.balign 8
data_value:
 .quad 123
data_code_ptr:
 .quad callee
stack:
 .zero 4096
.section .rodata
.global ro_value
.balign 8
ro_value:
 .quad 456
message:
 .asciz "'''+marker+'''\\n"
''',
            'b': '.text\n.global callee,helper\ncallee:\n jsr helper\n rts\nhelper:\n move.q #37,r0\n rts\n',
        }
        for name, text in sources.items():
            path = work/(name+'.s')
            path.write_text(''.join(normalize(text, 'pcrel_test_'+name)))
            subprocess.run([args.cc, '-c', str(path), '-o', str(work/(name+'.o'))], check=True)
        public = subprocess.check_output([args.nm, '-P', '-g', str(work/'a.o')], text=True)
        assert '__rvm_pcrel_' not in public, public
        local = subprocess.check_output([args.nm, '-P', str(work/'a.o')], text=True)
        assert re.search(r'^__rvm_pcrel_\w+ t ', local, re.M), local
        subprocess.run([args.ar, 'rcs', 'b.a', 'b.o'], cwd=work, check=True)
        for split in (False, True):
            script = work/'link.ld'
            code = '.text : { a.o(.text) } .othercode : { b.o(.text) }' if split else '.text : { *(.text) }'
            script.write_text('SECTIONS { . = 0; '+code+' .data : { *(.data) } .rodata : { *(.rodata) } }\n')
            inputs = ['a.o', 'b.o' if split else 'b.a']
            command = ['-T', 'link.ld', '-e', '_start', '-Map', 'image.map', '-o', 'netbsd.fhr'] + inputs
            subprocess.run([args.ld]+command, cwd=work, check=True)
            symbols = {name: int(value, 16) for value, name in re.findall(
                r'^\s+0x([0-9a-f]+)\s+(\w+)\s*$', (work/'image.map').read_text(), re.M)}
            sections, relocs = executable(work/'netbsd.fhr')
            code_bytes = sections[0][1]
            data_index = 2 if split else 1
            ro_index = data_index+1
            for site, target in [('same_call','backward'), ('code_lea','callee'), ('cross_call','callee')]:
                offset = symbols[site]
                if split and target == 'callee':
                    assert code_bytes[offset+2] == 0x37
                    assert (0, offset+3, 1) in relocs
                else:
                    mode = code_bytes[offset+2]
                    assert 0x2c <= mode <= 0x33, (site, code_bytes[offset:offset+11].hex())
                    delta = int.from_bytes(code_bytes[offset+3:offset+4+mode-0x2c], 'big', signed=True)
                    assert offset + delta == symbols[target], (site, delta)
                    assert not any(src == 0 and where == offset+3 for src, where, _ in relocs)
            if split:
                other = sections[1][1]
                assert 0x2c <= other[2] <= 0x33
                delta = int.from_bytes(other[3:4+other[2]-0x2c], 'big', signed=True)
                assert delta == symbols['helper'] - symbols['callee']
                assert not any(src == 1 and where == 3 for src, where, _ in relocs)
            for site, target in [('data_lea',data_index), ('ro_lea',ro_index)]:
                assert code_bytes[symbols[site]+2] == 0x37
                assert (0, symbols[site]+3, target) in relocs
            # This literal is preceded by bytes that look exactly like LEA's
            # opcode and absolute mode. Only mapping metadata may identify code.
            assert (0, symbols['literal_ptr'], 1 if split else 0) in relocs
            assert (data_index, 8, 1 if split else 0) in relocs
            subprocess.run([args.ld, '--no-relax']+command[:command.index('-o')]+[
                '-o', 'unrelaxed.fhr']+inputs, cwd=work, check=True)
            unrelaxed, all_relocs = executable(work/'unrelaxed.fhr')
            assert relocs <= all_relocs
            if not split: assert relocs < all_relocs
            # Assembler-local branches are already compact; --no-relax only
            # disables final-link conversion of unresolved references.
            assert unrelaxed[0][1][symbols['cross_call']+2] == 0x37
            if args.baseline_ld:
                subprocess.run([args.baseline_ld]+command[:command.index('-o')]+[
                    '-o', 'baseline.fhr']+inputs, cwd=work, check=True)
                previous, old_relocs = executable(work/'baseline.fhr')
                assert previous == unrelaxed and old_relocs == all_relocs
                assert [len(buf) for _, buf in previous] == [len(buf) for _, buf in sections]
                assert relocs <= old_relocs
                print(f'Relocations {len(old_relocs)} -> {len(relocs)}; '
                      f'file bytes {(work/"baseline.fhr").stat().st_size} -> {(work/"netbsd.fhr").stat().st_size}')
            if not split:
                shutil.copyfile(args.loader, work/'bootloader.bin')
                (work/'vm.config').write_text((ROOT/'sys/arch/rvm/conf/vmemu.conf').read_text())
                env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
                try:
                    result = subprocess.run(['VMEmu','-c','vm.config'], cwd=work, env=env,
                                            capture_output=True, timeout=5)
                    raise AssertionError('VM exited: '+str(result.returncode))
                except subprocess.TimeoutExpired as error:
                    output = (error.stdout or b'').decode(errors='replace')
                    assert marker in output, output[-5000:]
        print('PASS: same-section PC-relative calls/LEAs, split sections, absolute pointers and guest execution')


if __name__ == '__main__':
    main()
