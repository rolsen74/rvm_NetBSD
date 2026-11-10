#!/usr/bin/env python3
"""Check RVM GCC output and execute edge cases in the real FHR loader."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
STAND = ROOT / 'sys/arch/rvm/stand'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cross', default=str(ROOT / '../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-'))
parser.add_argument('--compiler-dir', type=Path, help='GCC -B directory containing a candidate cc1')
parser.add_argument('--loader', type=Path, default=ROOT / 'obj.rvm-bootstrap/bootloader.bin')
parser.add_argument('--vmemu', default='VMEmu')
parser.add_argument('--baseline', action='store_true', help='run behavior checks without output expectations')
args = parser.parse_args()
env = dict(os.environ, RVM_REAL_CC=args.cross + 'gcc',
           SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
marker = 'RVM GCC: flags, zero copies, divmod, constants and frames verified'


def literal(x):
    return f'0x{x % (1 << 64):016x}UL'


with tempfile.TemporaryDirectory(prefix='rvm-gcc-opts-') as directory:
    work = Path(directory)
    source = Path(__file__).with_suffix('.c')
    checks = ['if (leaf() != -1 || zeros() != 0) return 1;',
              'if (stack_args(1,2,3,4,5,6,7,8,9,10) != 55) return 2;',
              'if (read_args(4, 9) != 13 || saved_register_probe() != 0) return 16;',
              'v = 17; mask_add(&v, 12); if (v != 29 || guard_result != 37) return 17;',
              'if (mask_cas(&v, 28, 41) != 29 || v != 29) return 18;',
              'if (mask_cas(&v, 29, 41) != 29 || v != 41 || guard_result != 37) return 19;']
    values = [0, 1, 2, 255, 256, 257, 511, 512, 0x7fffffff, 0x80000000,
              0xffffffff, 0x100000000, 0xfeedface00000000, 0xffffffffffffffff]
    for x in values:
        low = x & 0xffffffff
        signed = low if low < 0x80000000 else low - (1 << 32)
        checks += [f'x = {literal(x)}; u = (unsigned)x; s = (int)x; c = x; h = x;',
                   f'if (dirty_mask(x) != {11 if low & 511 else 17}) return 3;',
                   f'if (dirty_mask2(x) != {11 if low & 2 else 17}) return 4;',
                   f'if (loaded(&u) != {11 if low else 17}) return 5;',
                   f'if (loaded_mask(&u) != {11 if low & 256 else 17}) return 6;',
                   f'if (mask_only(&u) != {11 if low & 256 else 17}) return 6;',
                   f'if (signed_negative(s) != {11 if signed < 0 else 17}) return 7;',
                   f'if (wide(&u) != {literal(low)}) return 8;',
                   f'if (wide8(&c) != {literal(x & 255)}) return 8;',
                   f'if (wide16(&h) != {literal(x & 65535)}) return 8;',
                   f'if (signed_wide(&s) != (long){literal(signed)}) return 8;',
                   f'if (narrow(&u) != {low}U) return 8;',
                   'v = 19;',
                   f'if (limit(x, &v) != {literal(min(x, 256))} || v != {256 if x > 256 else 19}) return 9;',
                   f'if (local_frame(x) != {literal(2*x+7)}) return 10;']
        for y in [0, 256, 0x7fffffff, 0x80000000, 0xffffffff]:
            sy = y if y < 0x80000000 else y - (1 << 32)
            checks += [f'if (loaded_compare(&u, {y}U) != {11 if low >= y else 17}) return 11;',
                       f'if (signed_compare(&s, (int){y}U) != {11 if signed >= sy else 17}) return 12;']
    for bits in [32, 64]:
        mask = (1 << bits) - 1
        signed_values = [-(1 << (bits-1)), -10001, -257, -1, 0, 1, 257, 10001, (1 << (bits-1))-1]
        for a in signed_values:
            for b in [-257, -7, -1, 1, 7, 257]:
                if a == -(1 << (bits-1)) and b == -1:
                    continue  # C division overflow is undefined.
                q = abs(a) // abs(b) * (-1 if (a < 0) != (b < 0) else 1)
                r = a - q*b
                name, ctype, rem = ('divides', 'long', 'sr') if bits == 64 else ('divides32', 'int', 'sr32')
                checks.append(f'if ({name}(({ctype}){literal(a)}, ({ctype}){literal(b)}, &{rem}) != ({ctype}){literal(q)} || {rem} != ({ctype}){literal(r)}) return 13;')
        for a in [0, 1, 256, 0x80000000, mask-1, mask]:
            for b in [1, 7, 256, mask]:
                name, ctype, rem = ('udivides', 'unsigned long', 'ur') if bits == 64 else ('udivides32', 'unsigned', 'ur32')
                checks.append(f'if ({name}(({ctype}){literal(a)}, ({ctype}){literal(b)}, &{rem}) != {literal(a//b)} || {rem} != {literal(a%b)}) return 14;')
    # Turn the separately compiled definitions into declarations.
    prototypes = re.sub(r'\{[^{}]*\}', ';', source.read_text(), flags=re.S)
    prototypes = re.sub(r'\{[^{}]*\}', ';', prototypes, flags=re.S)
    (work/'driver.c').write_text(prototypes + '''
extern int dirty_mask(unsigned long), dirty_mask2(unsigned long);
extern void emit(int);
extern int saved_register_probe(void);
extern int frame_abi_checks(void);
char test_stack[65536] __attribute__((aligned(65536)));
int yes(unsigned x) { (void)x; return 11; }
int no(unsigned x) { (void)x; return 17; }
static volatile int current;
static volatile unsigned long guard_result;
unsigned long irq_enter(void) { return 37; }
void irq_leave(unsigned long value) { guard_result = value; }
void *memcpy(void *to, const void *from, unsigned long length) {
    volatile unsigned char *d = to;
    const volatile unsigned char *s = from;
    while (length--) *d++ = *s++;
    return to;
}
int getvalue(void) { return current; }
unsigned long sink(unsigned long a, unsigned long b, unsigned long c) { return a | b | c; }
int check(void) {
    int abi = frame_abi_checks();
    if (abi) return 20 + abi;
    unsigned long x, v, ur; unsigned u, ur32; int s, sr32; long sr;
    unsigned char c; unsigned short h;
    current = 0; if (move_branch() != 17) return 15;
    current = -1; if (move_branch() != 11) return 15;
    current = (int)0x80000000U; if (move_branch() != 11) return 15;
''' + '\n'.join(checks) + '''
    return 0;
}
void run(void) {
    int result = check();
    const char *p = result ? "RVM GCC: FAILED code " : "''' + marker + '''\\n";
    while (*p) emit(*p++);
    if (result) { emit('0'+result/10); emit('0'+result%10); emit('\\n'); }
}
''')
    (work/'entry.vma').write_text('''
 section ".text",code
 SecAlign 4
 xdef _start
 xref test_stack
 xref run
_start:
 lea test_stack,r63
 add.q #65536,r63
 sub.q #64,r63
 jsr run
stop:
 halt
 jmp stop
 xdef emit
emit:
 move.q r2,r3
 move.q #3,r2
 emufunc
 rts
 xdef dirty_mask
 xref mask
dirty_mask:
 jmp mask
 xdef dirty_mask2
 xref mask2
dirty_mask2:
 jmp mask2
 xdef saved_register_probe
 xref mask_asm
saved_register_probe:
 pushm r21/r37
 sub.q #64,r63
 move.q #0x123456789abcdeff,r21
 move.q #0xfedcba9876543210,r37
 jsr mask_asm
 rclr.l r0
 cmp.q #0x123456789abcdeff,r21
 bne .Lbad_saved
 cmp.q #0xfedcba9876543210,r37
 beq .Lsaved_done
.Lbad_saved:
 move.l #1,r0
.Lsaved_done:
 add.q #64,r63
 popm r21/r37
 rts
''')
    subprocess.run(['VMAsm', '-i', 'entry.vma', '-o', 'entry.o', '--obj'],
                   cwd=work, check=True, stdout=subprocess.DEVNULL)
    shutil.copyfile(args.loader, work/'bootloader.bin')
    (work/'vm.config').write_text((ROOT/'sys/arch/rvm/conf/vmemu.conf').read_text())
    for level, frame in [('-O0', []), ('-O2', []), ('-Os', []),
                         ('-O2', ['-fno-omit-frame-pointer'])]:
        extra = ['-B' + str(args.compiler_dir.resolve()) + '/'] if args.compiler_dir else []
        for src, obj in [(source, 'functions.o'), (source.with_name('gcc_frame_abi.c'), 'frames.o'),
                         (work/'driver.c', 'driver.o')]:
            subprocess.run(['python3', str(STAND/'kernel_cc.py'), level,
                            '-ffreestanding', '-fno-builtin', '-nostdinc'] + extra + frame +
                           ['-c', str(src), '-o', str(work/obj)], env=env, check=True)
        assembly = (work/'functions.raw.s').read_text()
        def body(name):
            return assembly.split(name+':\n', 1)[1].split('\n\t.size', 1)[0]
        if not args.baseline and level != '-O0':
            def saved(name):
                match = re.search(r'\bpushf\s+([^\n]+)', body(name))
                registers = set()
                if match:
                    for part in match[1].split('/'):
                        ends = [int(x) for x in re.findall(r'r(\d+)', part)]
                        registers.update(range(ends[0], ends[-1]+1))
                return registers
            assert 2 in saved('mask_add') and 3 not in saved('mask_add'), body('mask_add')
            assert 2 in saved('mask_cas') and not {3, 4} & saved('mask_cas'), body('mask_cas')
            assert saved('mask_asm') == {21, 37, 62}, body('mask_asm')
            assert not {2, 3} & saved('read_args'), body('read_args')
            assert ('pushf' in body('leaf')) == bool(frame), body('leaf')
            assert ('popf' in body('leaf')) == bool(frame), body('leaf')
            for name, size, sign in [('divides', 'q', 's'), ('udivides', 'q', 'u'),
                                     ('divides32', 'l', 's'), ('udivides32', 'l', 'u')]:
                assert re.search(r'\brdiv'+sign+r'\.'+size+r'\s+[^\n]+:', body(name)), body(name)
                assert not re.search(r'\b(?:r|v)?mod[su]\.', body(name)), body(name)
            for name in ['mask', 'mask2']:
                assert 'rtst.' not in body(name), body(name)
            assert 'rtst.' not in body('move_branch'), body('move_branch')
            assert 'extu.l' not in body('mask_only'), body('mask_only')
            assert 'extu.l' not in body('narrow'), body('narrow')
            assert body('zeros').count('rclr.q') >= 3, body('zeros')
            assert body('limit').count('#256') == 2, body('limit')
            assert 'extu.l' in body('wide'), body('wide')
        subprocess.run(['python3', str(STAND/'kernel_link.py'), '--cc', env['RVM_REAL_CC'],
                        '--ld', args.cross+'ld', '--nm', args.cross+'nm', '--ar', args.cross+'ar',
                        '-o', 'netbsd.fhr', 'entry.o', 'functions.o', 'frames.o', 'driver.o'],
                       cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
        try:
            result = subprocess.run([args.vmemu, '-c', 'vm.config'], cwd=work,
                                    env=env, capture_output=True, timeout=5)
            raise AssertionError('VM exited: '+str(result.returncode))
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'').decode(errors='replace')
            assert marker in output and 'FAILED' not in output, output
        print('PASS: RVM GCC output and guest edge cases '+level+' '+' '.join(frame), flush=True)
