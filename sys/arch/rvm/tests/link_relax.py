#!/usr/bin/env python3
"""Execute cross-object PC shrinking and verify maps, addends and opt-outs."""
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
    parser.add_argument('--ar', default=cross+'ar')
    parser.add_argument('--baseline-ld')
    parser.add_argument('--baseline-as')
    parser.add_argument('--loader', type=Path, default=ROOT/'obj.rvm-bootstrap/bootloader.bin')
    args = parser.parse_args()
    marker = 'RVM final-link shrinking and pointers verified'
    with tempfile.TemporaryDirectory(prefix='rvm-link-relax-') as directory:
        work = Path(directory)
        sources = {
            'entry': '.text\n.global _start\n_start:\n lea stack+4096,sp\n jmp body\n',
            'before': '.text\n.global before\nbefore:\n move.q #37,r0\n rts\n .space 2600\n',
            'body': '''.text
.global body,lea_negative,lea_word,lea_far,tail,alias
body:
lea_negative:
 lea before,r19
lea_word:
 lea after,r24
lea_far:
 lea far,r30
 lea pointers,r11
 move.q (r11)+,r10
 cmp.q r10,r19
 bne failed
 move.q (r11)+,r10
 cmp.q r10,r24
 bne failed
 move.q (r11)+,r10
 cmp.q r10,r30
 bne failed
 move.q (r11)+,r10
 lea tail,r12
 cmp.q r10,r12
 bne failed
 jsr (r19)
 cmp.q #37,r0
 bne failed
 jsr (r24)
 cmp.q #37,r0
 bne failed
 jsr (r30)
 cmp.q #37,r0
 bne failed
 jsr (r12)
 cmp.q #23,r0
 bne failed
 lea message,r10
 rclr.q r3
loop:
 move.b (r10)+,r3
 beq stopped
 move.q #3,r2
 emufunc
 jmp loop
failed:
 halt
 jmp failed
stopped:
 halt
 jmp stopped
tail:
alias = tail
 move.q #23,r0
 rts
''',
            'filler1': '.text\n.space 28000\n',
            'after': '.text\n.global after\nafter:\n jsr before\n rts\n',
            'filler2': '.text\n.space 1700000\n',
            'far': '.text\n.global far\nfar:\n move.q #37,r0\n rts\n',
            # Symbolic data addends must be mapped through code shrinkage.
            'data': '.data\n.global pointers,stack,message\npointers:\n .quad before,after,far,alias\nmessage:\n .asciz "'+marker+'\\n"\nstack:\n .space 4096\n',
            # Inline pointer bytes that resemble LEA must not become code.
            'unsafe': '.text\n.global unsafe,aligned,literal\nunsafe:\n lea far,r20\n .balign 16\naligned:\n .byte 0,0x5f,0x37\nliteral:\n .quad tail\n',
            # Explicit PC offsets and code-label differences opt out.
            'protected': '.text\n.global protected,operand\nprotected:\n lea far,r23\noperand = protected+5\n rts\n',
            'numeric': '.text\n.global numeric\nnumeric:\n lea far,r21\n bne (-29,pc)\n',
            'difference': '.text\n.global difference,difference_end\ndifference:\n lea far,r22\n rts\ndifference_end:\n.data\n .long difference_end-difference\n',
        }
        def assemble(name, text, assembler=None):
            (work/(name+'.s')).write_text(text)
            subprocess.run([assembler or args.assembler, name+'.s', '-o', name+'.o'], cwd=work, check=True)
        for name,text in sources.items(): assemble(name,text)
        # An archive member exercises lazy inclusion and per-member symbols.
        subprocess.run([args.ar,'rcs','after.a','after.o'], cwd=work, check=True)
        inputs = ['entry.o','before.o','body.o','filler1.o','after.a','filler2.o',
                  'far.o','unsafe.o','numeric.o','difference.o','protected.o','data.o']
        (work/'link.ld').write_text('SECTIONS { . = 0; .text : { *(.text) } .data : { data.o(.data) *(.data) } }')
        def link(name, extra=(), linker=None, objects=None):
            subprocess.run([linker or args.ld,*extra,'-T','link.ld','-e','_start',
                            '-Map',name+'.map','-o',name+'.fhr',*(objects or inputs)], cwd=work, check=True)
            symbols = {n:int(v,16) for v,n in re.findall(
                r'^\s+0x([0-9a-f]+)\s+(\w+)\s*$', (work/(name+'.map')).read_text(), re.M)}
            return (*executable(work/(name+'.fhr')), symbols)
        old, oldrel, oldsym = link('unrelaxed', ['--no-relax'])
        new, newrel, sym = link('relaxed')
        code = new[0][1]
        for name,target,width in [('lea_negative','before',2),('lea_word','after',2),('lea_far','far',3)]:
            at = sym[name]
            assert code[at+2] == 43+width, (name, code[at:at+13].hex())
            delta = int.from_bytes(code[at+3:at+3+width],'big',signed=True)
            assert at+delta == sym[target], (name,delta,sym[target])
            assert old[0][1][oldsym[name]+2] == 0x37
            assert not any(src == 0 and field == at+3 for src,field,_ in newrel)
        assert len(code) < len(old[0][1])-17
        assert sym['aligned'] % 16 == 0
        for name in ('unsafe','numeric','difference','protected'):
            assert code[sym[name]+2] == 0x33  # Convert to PC64, retain width.
        assert (0,sym['literal'],0) in newrel
        assert int.from_bytes(code[sym['literal']:sym['literal']+8],'big') == sym['tail']
        for i,name in enumerate(('before','after','far','tail')):
            field = i*8
            assert int.from_bytes(new[1][1][field:field+8],'big') == sym[name], name
        assert sym['alias'] == sym['tail']
        # A numeric addend into code must follow the original input offset,
        # even when its base symbol and target move by different amounts.
        offset = oldsym['tail']-oldsym['body']
        assemble('data', sources['data'].replace('far,alias','far,body+'+str(offset)))
        added, _, add_sym = link('addend')
        assert int.from_bytes(added[1][1][24:32],'big') == add_sym['tail']
        if args.baseline_ld:
            baseline, baseline_rel, _ = link('baseline', ['--no-relax'], linker=args.baseline_ld)
            control, control_rel, _ = link('baseline_control', ['--no-relax'])
            assert baseline == control and baseline_rel == control_rel
        if args.baseline_as:
            assemble('legacy', '.text\n.global legacy\nlegacy:\n lea far,r23\n rts\n', args.baseline_as)
            legacy, _, legacy_sym = link('legacy', objects=inputs+['legacy.o'])
            at = legacy_sym['legacy']
            assert legacy[0][1][at+2] == 0x33
            delta = int.from_bytes(legacy[0][1][at+3:at+11], 'big', signed=True)
            assert at+delta == legacy_sym['far']
        # A negative code addend before an input section must retain its
        # literal offset; it must not wrap around and subtract every deletion.
        assemble('data', sources['data'].replace('message:',
            '.global negative_pointer\nnegative_pointer:\n .quad body-1\nmessage:'))
        negative, _, negative_sym = link('negative_addend')
        assert negative[0][1][negative_sym['lea_word']+2] == 0x33
        assert int.from_bytes(negative[1][1][32:40], 'big') == negative_sym['body']-1
        # Source code pinned by a linker-script alignment can need a wider
        # backward displacement after preceding sections shrink.
        for name,text in {
            'pe': '.text\n.global pe,_start\n_start = pe\npe:\n lea pt,r10\n jmp ps\n',
            'pf': '.text\n.space '+str(32768-24)+'\n',
            'pt': '.text\n.global pt\npt:\n rts\n',
            'ps': '.text\n.global ps\nps:\n jsr pt\n rts\n',
        }.items(): assemble(name,text)
        (work/'link.ld').write_text('SECTIONS { . = 0; .text : { pe.o(.text) pf.o(.text) pt.o(.text) . = ALIGN(65536); ps.o(.text) } }')
        padded, _, pad_sym = link('padded',objects=['pe.o','pf.o','pt.o','ps.o'])
        at = pad_sym['ps']
        assert at == 65536 and padded[0][1][at+2] == 0x2e
        delta = int.from_bytes(padded[0][1][at+3:at+6],'big',signed=True)
        assert at+delta == pad_sym['pt']
        shutil.copyfile(work/'addend.fhr', work/'netbsd.fhr')
        shutil.copyfile(args.loader, work/'bootloader.bin')
        (work/'vm.config').write_text((ROOT/'sys/arch/rvm/conf/vmemu.conf').read_text())
        try:
            result = subprocess.run(['VMEmu','-c','vm.config'],cwd=work,
                env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy'),
                capture_output=True,timeout=8)
            raise AssertionError('VM exited: '+str(result.returncode))
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'').decode(errors='replace')
            assert marker in output,output[-5000:]
        print(f'PASS: final-link PC16/PC24 LEAs, calls, branches, aliases, pointer addends, archives, alignment and opt-outs; {len(old[0][1])-len(code)} code bytes saved')


if __name__ == '__main__':
    main()
