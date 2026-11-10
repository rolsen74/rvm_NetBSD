#!/usr/bin/env python3
"""Verify opt-in section-base operands, literal pointers and VM execution."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from pcrel_link import ROOT, executable, normalize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cross = str(ROOT/'../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-')
    parser.add_argument('--as', dest='assembler', default=cross+'as')
    parser.add_argument('--ld', default=cross+'ld')
    parser.add_argument('--loader', type=Path, default=ROOT/'obj.rvm-bootstrap/bootloader.bin')
    args = parser.parse_args()
    marker = 'RVM section bases and stored pointers verified'
    with tempfile.TemporaryDirectory(prefix='rvm-section-bases-test-') as directory:
        work = Path(directory)
        main_source = '''.text
.global _start,__rvm_section_bases_v1
__rvm_section_bases_v1:
_start:
 lea __rvm_rodata_base,r50
 lea __rvm_data_base,r51
 lea __rvm_bss_base,r52
 lea stack+4096,sp
.global clear_bss,store_data,copy_memory,test_bss,read_ro,lea_ro,read_pointer
clear_bss:
 clr.q zero
store_data:
 move.q #17,datum
copy_memory:
 move.q datum,zero
 cmp.q #17,zero
 bne failed
test_bss:
 tst.l zero+4
 beq failed
read_ro:
 cmp.q #456,constant
 bne failed
lea_ro:
 lea constant,r10
read_pointer:
 move.q pointer,r11
 cmp.q r10,r11
 bne failed
 jsr unsafe
 cmp.q #456,r0
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
.data
.global datum,pointer
datum:
 .quad 0
pointer:
 .quad constant
stack:
 .space 4096
.section .rodata
.global constant
constant:
 .quad 456
message:
 .asciz "MARKER\\n"
'''.replace('MARKER', marker)
        sources = {
            'main': main_source,
            'bounds': '.text\n.global neg128,neg129,pos127,pos128,pos32767,pos32768\nneg128:\n lea datum-128,r20\nneg129:\n lea datum-129,r20\npos127:\n move.q datum+127,r20\npos128:\n move.q datum+128,r20\npos32767:\n move.q datum+32767,r20\npos32768:\n move.q datum+32768,r20\n rts\n',
            'zero': '.data\n.global zero\nzero:\n .space 16\n',
            'unsafe': '.text\n.balign 8\n.global unsafe\nunsafe:\n move.q constant,r0\n rts\n.byte 0x00,0x23,0x37\n.global literal\nliteral:\n .quad constant\n',
        }
        def assemble(name, source):
            (work/(name+'.s')).write_text(''.join(normalize(source, 'bases_'+name)))
            subprocess.run([args.assembler, name+'.s', '-o', name+'.o'], cwd=work, check=True)
        for name, source in sources.items():
            assemble(name, source)
        (work/'link.ld').write_text('''SECTIONS { . = 0;
.text : { main.o(.text) unsafe.o(.text) bounds.o(.text) }
.data : { __rvm_data_base = .; main.o(.data) }
.rodata : { __rvm_rodata_base = .; *(.rodata) }
.bss (NOLOAD) : { __rvm_bss_base = .; zero.o(.data) }
}''')
        def link(name, *options):
            subprocess.run([args.ld, *options, '-T', 'link.ld', '-Map', name+'.map',
                            '-o', name+'.fhr', 'main.o', 'unsafe.o', 'bounds.o', 'zero.o'], cwd=work, check=True)
            symbols = {n: int(v, 16) for v, n in re.findall(
                r'^\s+0x([0-9a-f]+)\s+(\w+)(?:\s+=.*)?\s*$', (work/(name+'.map')).read_text(), re.M)}
            return (*executable(work/(name+'.fhr')), symbols)
        sections, relocs, sym = link('bases')
        original, original_relocs, oldsym = link('absolute', '--no-relax')
        code = sections[0][1]
        # Decode the actual operand, including immediate/register operands
        # which precede a destination. Assert the final section-relative value.
        for name, mode_offset, reg, target in [
            ('clear_bss', 2, 52, 'zero'), ('store_data', 4, 51, 'datum'),
            ('copy_memory', 2, 51, 'datum'), ('copy_memory', 5, 52, 'zero'),
            ('test_bss', 2, 52, 'zero'), ('read_ro', 5, 50, 'constant'),
            ('lea_ro', 2, 50, 'constant'), ('read_pointer', 2, 51, 'pointer')]:
            at = sym[name]+mode_offset
            width = code[at]-19
            assert 1 <= width <= 7, (name, code[at:at+12].hex())
            assert code[at+1] == reg, (name, code[at:at+12].hex())
            value = int.from_bytes(code[at+2:at+2+width], 'big', signed=True)
            base = sym[{50:'__rvm_rodata_base',51:'__rvm_data_base',52:'__rvm_bss_base'}[reg]]
            expected = sym[target]-base + (4 if name == 'test_bss' else 0)
            assert value == expected, (name, value, expected)
        for name, value, width in [('neg128',-128,1),('neg129',-129,2),
                                   ('pos127',127,1),('pos128',128,2),
                                   ('pos32767',32767,2),('pos32768',32768,3)]:
            at = sym[name]+2
            assert code[at:at+2] == bytes([19+width, 51]), name
            assert int.from_bytes(code[at+2:at+2+width], 'big', signed=True) == value
        assert code[sym['unsafe']+2:sym['unsafe']+4] == b'\x1a\x32'
        assert int.from_bytes(code[sym['unsafe']+4:sym['unsafe']+11], 'big') == 0
        # Startup base LEAs and genuine stored pointers keep loader relocations.
        assert {(0, 3, 2), (0, 16, 1), (0, 29, 3)} <= relocs
        assert (1, sym['pointer']-sym['__rvm_data_base'], 2) in relocs
        assert (0, sym['literal'], 2) in relocs
        assert len(relocs) == 5, relocs
        assert len(original_relocs) > len(relocs)
        assert len(original[0][1]) > len(code)
        # An older startup never opts into using the reserved base registers.
        assemble('main', main_source.replace(',__rvm_section_bases_v1', '').replace('__rvm_section_bases_v1:\n', ''))
        legacy, legacy_relocs, legacy_sym = link('legacy')
        assert legacy[0][1][legacy_sym['clear_bss']+2] == 0x37
        assert len(legacy_relocs) > len(relocs)
        assert legacy[0][1][legacy_sym['lea_ro']+2] == 0x37
        # The heap anchor must follow rodata and all BSS. Placing _end in
        # .data lets brk discard later sections when malloc first extends it.
        assemble('crtend', (ROOT/'lib/csu/arch/rvm/crtend.S').read_text())
        assemble('heap_pointer', '.data\n.global heap_anchor\nheap_anchor:\n .quad _end\n')
        subprocess.run([args.ld, '-T', str(ROOT/'sys/arch/rvm/stand/sections.ld'),
                        '-Map', 'heap.map', '-o', 'heap.fhr', 'main.o', 'unsafe.o',
                        'bounds.o', 'zero.o', 'heap_pointer.o', 'crtend.o'], cwd=work, check=True)
        heap_sections, heap_relocs = executable(work/'heap.fhr')
        assert len(heap_sections) == 4 and len(heap_sections[3][1]) == 9
        heap_symbols = {n: int(v, 16) for v, n in re.findall(
            r'^\s+0x([0-9a-f]+)\s+(\w+)(?:\s+=.*)?\s*$',
            (work/'heap.map').read_text(), re.M)}
        anchor = heap_symbols['heap_anchor']-heap_symbols['__rvm_data_base']
        assert (1, anchor, 3) in heap_relocs
        assert int.from_bytes(heap_sections[1][1][anchor:anchor+8], 'big') == 8
        assert heap_symbols['_end'] == heap_symbols['__rvm_bss_base']+8
        shutil.copyfile(work/'bases.fhr', work/'netbsd.fhr')
        shutil.copyfile(args.loader, work/'bootloader.bin')
        (work/'vm.config').write_text((ROOT/'sys/arch/rvm/conf/vmemu.conf').read_text())
        try:
            result = subprocess.run(['VMEmu', '-c', 'vm.config'], cwd=work,
                env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'),
                capture_output=True, timeout=8)
            raise AssertionError('VM exited: '+str(result.returncode))
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'').decode(errors='replace')
            assert marker in output, output[-5000:]
        print(f'PASS: R50/R51/R52 clears, tests, loads, stores, memory copies, LEAs, pointers, legacy startup, --no-relax and VM execution; {len(original[0][1])-len(code)} code bytes and {len(original_relocs)-len(relocs)} relocations saved')


if __name__ == '__main__':
    main()
