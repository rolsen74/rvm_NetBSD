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
              'if (flag_builtin_checks() != 0) return 31;',
              'if (wide_return_checks() != 0 || wide_scratch_probe() != 0) return 44;',
              'if (regcall_eight(1,2,3,4,5,6,7,8) != 204 || regcall_oldstyle(1,2,3,4,5,6,7,8) != 204) return 37;',
              'if (regcall_address(37) != (37UL ^ 0x123456789abcdef0UL) || call_unprototyped() != (37UL ^ 0x123456789abcdef0UL)) return 38;',
              'if (regcall_indirect(regcall_inc, 99) != 100 || !regcall_narrow(1,-7,240,-1234,60000,-300000,4000000000U,-0x123456789L)) return 39;',
              'if (stack_args(1,2,3,4,5,6,7,8,9,10) != 55) return 2;',
              'if (read_args(4, 9) != 13 || saved_register_probe() != 0) return 16;',
              'if (call_with_home(0) != 37 || call_only() != 37 || stack_only(0xfeedface12345678UL) != 0xfeedface12345678UL) return 29;',
              'if (stacked_after_call(1,2,3,4,5,6,7,8,9) != 9) return 30;',
              'v = 17; mask_add(&v, 12); if (v != 29 || guard_result != 37) return 17;',
              'if (mask_cas(&v, 28, 41) != 29 || v != 29) return 18;',
              'if (mask_cas(&v, 29, 41) != 29 || v != 41 || guard_result != 37) return 19;']
    checks += ['unsigned long pair_a[2], pair_b[2];',
               'if (return_scratch_probe() != 0 || return_live(7,11) != 55) return 40;',
               'unsigned long stacked_result; regcall_void_stacked(&stacked_result,1,2,3,4,5,6,7,8,9); if (stacked_result != 285) return 43;']
    pair_values = [0, 1, 65535, 0x7fffffffffffffff, 0x8000000000000000, 0xffffffffffffffff]
    for i, x in enumerate(pair_values):
        for j, y in enumerate(pair_values):
            for a, b in [((x, y), (y, x)), ((x, y), (x, x)), ((x, y), (x, y))]:
                expected = (a > b) - (a < b)
                checks += [f'pair_a[0]={literal(a[0])}; pair_a[1]={literal(a[1])}; pair_b[0]={literal(b[0])}; pair_b[1]={literal(b[1])};',
                           f'if (return_pair_compare(0,pair_a,pair_b) != {expected}) return 41;']
    for value in [-2147483648,-8,-7,-1,0,1,7,8,2147483647]:
        expected = 1 if value > 7 else -1 if value < -7 else 0
        checks.append(f'if (return_sign({value}) != {expected}) return 42;')
    values = [0, 1, 2, 127, 128, 255, 256, 257, 511, 512, 32767, 32768, 65535, 65536, 0x7fffffff, 0x80000000,
              0xffffffff, 0x100000000, 0xfeedface00000000, 0xffffffffffffffff]
    for x in values:
        low = x & 0xffffffff
        signed8 = (x & 255) - (256 if x & 128 else 0)
        signed16 = (x & 65535) - (65536 if x & 32768 else 0)
        checks += [f'if (cast_u8({literal(x)}) != {literal(x & 255)} || cast_u8_chain({literal(x)}) != {literal(x & 255)}) return 32;',
                   f'if (cast_u16({literal(x)}) != {literal(x & 65535)} || cast_u16_chain({literal(x)}) != {literal(x & 65535)}) return 33;',
                   f'if (cast_s8({literal(x)}) != (long){literal(signed8)} || cast_s16({literal(x)}) != (long){literal(signed16)}) return 34;',
                   f'if (cast_s16_u32({literal(x)}) != {literal(signed16 & 0xffffffff)} || cast_s8_u16({literal(x)}) != {literal(signed8 & 65535)}) return 35;',
                   f'c = (unsigned char){literal(x)}; h = (unsigned short){literal(x)};',
                   f'if (load_s8((signed char *)&c) != (long){literal(signed8)} || load_s16((short *)&h) != (long){literal(signed16)}) return 36;']
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
extern int return_scratch_probe(void);
extern int wide_scratch_probe(void);
extern int wide_return_checks(void);
unsigned long wide_scratch_slot[2] __attribute__((aligned(16)));
unsigned long return_scratch_slot;
extern int frame_abi_checks(void);
char test_stack[65536] __attribute__((aligned(65536)));
int yes(unsigned x) { (void)x; return 11; }
int no(unsigned x) { (void)x; return 17; }
static volatile int current;
static volatile unsigned long guard_result;
unsigned long frame_plain_callee(void) { return 37; }
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
int flag_builtin_checks(void);
unsigned long regcall_eight(unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long);
unsigned long regcall_oldstyle(unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long);
unsigned long regcall_address(unsigned long);
unsigned long regcall_inc(unsigned long);
unsigned long regcall_indirect(unsigned long (*)(unsigned long), unsigned long);
int regcall_narrow(_Bool,signed char,unsigned char,short,unsigned short,int,unsigned,long);
void regcall_void_stacked(unsigned long *,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long);
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
 xdef return_scratch_probe
 xref return_scratch
 xref return_scratch_slot
return_scratch_probe:
 pushm r2-r4
 lea return_scratch_slot,r2
 move.q #7,r3
 move.q #3,r4
 move.q #0xfedcba9876543210,r0
 jsr return_scratch
 cmp.q #0xfedcba9876543210,r0
 bne .Lreturn_bad
 cmp.q #11,return_scratch_slot
 bne .Lreturn_bad
 rclr.l r0
 jmp .Lreturn_done
.Lreturn_bad:
 move.l #1,r0
.Lreturn_done:
 popm r2-r4
 rts
 xdef wide_scratch_probe
 xref wide_store
 xref wide_scratch_slot
wide_scratch_probe:
 pushm r2-r4
 lea wide_scratch_slot,r2
 move.q #0x123456789abcdef0,r3
 move.q #0xfedcba9876543210,r4
 move.q #0xdeadbeefcafebabe,r0
 move.q #0x0123456789abcdef,r1
 jsr wide_store
 cmp.q #0xdeadbeefcafebabe,r0
 bne .Lwide_bad
 cmp.q #0x0123456789abcdef,r1
 bne .Lwide_bad
 cmp.q #0x123456789abcdef0,(r2)
 bne .Lwide_bad
 cmp.q #0xfedcba9876543210,(8,r2)
 bne .Lwide_bad
 rclr.l r0
 jmp .Lwide_done
.Lwide_bad:
 move.l #1,r0
.Lwide_done:
 popm r2-r4
 rts
 xdef frame_home_callee
frame_home_callee:
 clr.q (8,r63)
 clr.q (16,r63)
 clr.q (24,r63)
 clr.q (32,r63)
 clr.q (40,r63)
 clr.q (48,r63)
 clr.q (56,r63)
 clr.q (64,r63)
 move.q #37,r0
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
                         (source.with_name('gcc_flags.c'), 'flags.o'),
                         (source.with_name('gcc_regcall_abi.c'), 'regcalls.o'),
                         (source.with_name('gcc_wide_return.c'), 'wide.o'),
                         (work/'driver.c', 'driver.o')]:
            subprocess.run(['python3', str(STAND/'kernel_cc.py'), level,
                            '-ffreestanding', '-fno-builtin', '-nostdinc'] + extra + frame +
                           ['-c', str(src), '-o', str(work/obj)], env=env, check=True)
        assembly = (work/'functions.raw.s').read_text()
        if not args.baseline:
            flags_assembly = (work/'flags.raw.s').read_text()
            write_only = flags_assembly.split('flag_write_only:\n', 1)[1].split('\n\t.size', 1)[0]
            assert 'mfctrl' not in write_only, write_only
            for flag in ('c', 'v', 'z', 'n', 'x', 'i'):
                assert re.search(r'\bset'+flag+r'\b', write_only), write_only
                assert re.search(r'\bclr'+flag+r'\b', write_only), write_only
            snapshot = flags_assembly.split('flag_snapshot:\n', 1)[1].split('\n\t.size', 1)[0]
            assert re.search(r'\bsetx\s*\n\s*mfctrl\s+sr,', snapshot), snapshot
            assert re.search(r'\bclrs\b', flags_assembly), flags_assembly
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
            for name, size in [('call_with_home', 64), ('stack_only', 8), ('stacked_after_call', 64)]:
                text = body(name)
                assert ('pushf' in text) == bool(frame), text
                assert ('popf' in text) == bool(frame), text
                assert re.search(r'\b(?:v)?sub\.q\s+#'+str(size)+r',sp(?:,sp)?', text), text
                restores = re.search(r'\b(?:v)?add\.q\s+#'+str(size)+r',sp(?:,sp)?', text)
                assert bool(restores) == (not frame), text
            assert not re.search(r'\b(?:v)?(?:add|sub)\.q[^\n]*sp', body('read_args')), body('read_args')
            assert ('pushf' in body('call_only')) == bool(frame), body('call_only')
            assert ('popf' in body('call_only')) == bool(frame), body('call_only')
            assert not re.search(r'\b(?:v)?(?:add|sub)\.q[^\n]*sp', body('call_only')), body('call_only')
            assert re.search(r'\b(?:v)?sub\.q\s+#64,sp', body('call_unprototyped')), body('call_unprototyped')
            regcalls = (work/'regcalls.raw.s').read_text()
            for name in ('regcall_address', 'regcall_indirect'):
                text = regcalls.split(name+':\n', 1)[1].split('\n\t.size', 1)[0]
                assert not re.search(r'\b(?:v)?sub\.q\s+#64,sp', text), text
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
            text = body('return_pair_compare')
            assert re.search(r'\bvmove\.l\s+#-1,r0', text), text
            assert re.search(r'\bvmove\.l\s+#1,r0', text), text
            assert not re.search(r'\brmove\.l\s+r[1-9][0-9]*,r0', text), text
            assert not re.search(r'\brclr\.l\s+r(?:[1-9][0-9]*)\b', text), text
            assert ('pushf' in body('return_sign')) == bool(frame), body('return_sign')
            assert 0 in saved('return_scratch'), body('return_scratch')
            for name, opcode in [('cast_u8', 'rextu.b'), ('cast_u16', 'rextu.w'),
                                 ('cast_u8_chain', 'rextu.b'), ('cast_u16_chain', 'rextu.w'),
                                 ('cast_s8', 'rexts.b'), ('cast_s16', 'rexts.w'),
                                 ('wide8', 'extu.b'), ('wide16', 'extu.w'),
                                 ('load_s8', 'exts.b'), ('load_s16', 'exts.w')]:
                text = body(name)
                assert opcode in text, text
                assert len(re.findall(r'\b(?:r)?ext[su]\.[bwl]\b', text)) == 1, text
        subprocess.run(['python3', str(STAND/'kernel_link.py'), '--cc', env['RVM_REAL_CC'],
                        '--ld', args.cross+'ld', '--nm', args.cross+'nm', '--ar', args.cross+'ar',
                        '-o', 'netbsd.fhr', 'entry.o', 'functions.o', 'frames.o', 'flags.o', 'regcalls.o', 'wide.o', 'driver.o'],
                       cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
        try:
            result = subprocess.run([args.vmemu, '-c', 'vm.config'], cwd=work,
                                    env=env, capture_output=True, timeout=5)
            raise AssertionError('VM exited: '+str(result.returncode))
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'').decode(errors='replace')
            assert marker in output and 'FAILED' not in output, output
        print('PASS: RVM GCC output and guest edge cases '+level+' '+' '.join(frame), flush=True)
