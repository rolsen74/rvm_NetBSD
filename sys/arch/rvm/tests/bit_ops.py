#!/usr/bin/env python3
"""Compile bit operations and execute their boundary cases in VMEmu."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
STAND = ROOT/'sys/arch/rvm/stand'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cross', default=str(ROOT/'../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-'))
parser.add_argument('--compiler-dir', type=Path, help='directory containing candidate cc1 and as')
parser.add_argument('--loader', type=Path, default=ROOT/'obj.rvm-bootstrap/bootloader.bin')
parser.add_argument('--vmemu', default='VMEmu')
args = parser.parse_args()
env = dict(os.environ, RVM_REAL_CC=args.cross+'gcc', SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
if args.compiler_dir:
    env['COMPILER_PATH'] = str(args.compiler_dir.resolve())
marker = 'RVM bit ops: masks, bit numbers, old-bit flags, memory widths and branches verified'


def literal(x):
    return f'0x{x % (1 << 64):016x}UL'


with tempfile.TemporaryDirectory(prefix='rvm-bit-ops-') as temporary:
    work = Path(temporary)
    functions, prototypes, checks = [], [], []

    def function(result, name, parameters, body):
        declaration = f'{result} {name}({parameters})'
        prototypes.append(declaration+';')
        functions.append(declaration+' { '+body+' }')

    # Every legal bit in each C integer width. Include both old-bit states
    # and a result that is nonzero even when the tested/modified bit is zero.
    for ctype, size, bits in [('unsigned char', 'b', 8), ('unsigned short', 'w', 16),
                               ('unsigned', 'l', 32), ('unsigned long', 'q', 64)]:
        full = (1 << bits)-1
        for bit in range(bits):
            one = 1 << bit
            for op, symbol, mask in [('clr', '&', full ^ one), ('set', '|', one), ('chg', '^', one)]:
                name = f'{op}_{size}_{bit}'
                function(ctype, name, ctype+' x', f'return x {symbol} {literal(mask)};')
                for value in [0, one, full ^ one, full]:
                    expected = {'clr': value & ~one, 'set': value | one, 'chg': value ^ one}[op] & full
                    checks.append(f'if ({name}(({ctype}){literal(value)}) != {literal(expected)}) return 1;')
            name = f'test_{size}_{bit}'
            function('int', name, ctype+' x', f'if (x & {literal(one)}) return yes(); return no();')
            checks += [f'if ({name}(({ctype}){literal(one)}) != 11) return 2;',
                       f'if ({name}(({ctype}){literal(full ^ one)}) != 17) return 2;']
            memory_name = f'test_memory_{size}_{bit}'
            function('int', memory_name, 'volatile '+ctype+' *p',
                     f'if (*p & {literal(one)}) return yes(); return no();')
            checks += [f'{{ {ctype} a[3] = {{ 1, ({ctype}){literal(one)}, 0 }};',
                       f'if ({memory_name}(&a[1]) != 11) return 8;',
                       f'a[1]=({ctype}){literal(full ^ one)}; if ({memory_name}(&a[1]) != 17) return 8; }}']
        # A byte/word/long memory access must not become a quad RMW.
        function('void', f'memory_{size}', ctype+' *p', '*p &= ~8UL;')
        function('int', f'clear_branch_{size}', ctype+' x',
                 f'{ctype} y = x & ~8UL; if (y) return consume(y); return no();')
        function('int', f'change_branch_{size}', ctype+' x',
                 f'{ctype} y = x ^ 8UL; if (y) return consume(y); return no();')
        function(ctype, f'clear_value_{size}', ctype+' x',
                 f'{ctype} y = x & ~8UL; if (y) return y; return no();')
        function(ctype, f'change_value_{size}', ctype+' x',
                 f'{ctype} y = x ^ 8UL; if (y) return y; return no();')
        for value in [0, 1, 8, 9, full]:
            checks += [f'if (clear_branch_{size}(({ctype}){literal(value)}) != {11 if value & ~8 else 17}) return 3;',
                       f'if (change_branch_{size}(({ctype}){literal(value)}) != {11 if value ^ 8 else 17}) return 3;',
                       f'if (clear_value_{size}(({ctype}){literal(value)}) != {literal((value & ~8) or 17)}) return 3;',
                       f'if (change_value_{size}(({ctype}){literal(value)}) != {literal((value ^ 8) or 17)}) return 3;']
        checks += [f'{{ {ctype} a[3] = {{ 0x55, ({ctype})-1, 0xaa }}; memory_{size}(&a[1]);',
                   f'if (a[0] != 0x55 || a[1] != {literal(full ^ 8)} || a[2] != ({ctype})0xaa) return 4; }}']
    for bits, clear, name, ctype in [(32, 8, 'low_byte', 'unsigned'),
                                      (32, 16, 'low_word', 'unsigned'),
                                      (64, 32, 'low_long', 'unsigned long')]:
        low = (1 << clear)-1
        function(ctype, name, ctype+' x', f'return x & {literal(((1 << bits)-1) ^ low)};')
        function('int', name+'_branch', ctype+' x',
                 f'{ctype} y=x & {literal(((1 << bits)-1) ^ low)}; if (y) return consume(y); return no();')
        function(ctype, name+'_value', ctype+' x',
                 f'{ctype} y=x & {literal(((1 << bits)-1) ^ low)}; if (y) return y; return no();')
        for x in [0, 1, low, low+1, (1 << bits)-1]:
            result = x & ~low
            checks += [f'if ({name}({literal(x)}) != {literal(result)}) return 5;',
                       f'if ({name}_branch({literal(x)}) != {11 if result else 17}) return 5;',
                       f'if ({name}_value({literal(x)}) != {literal(result or 17)}) return 5;']
    for op, expression in [('clr', 'x & ~(1UL << n)'), ('set', 'x | (1UL << n)'), ('chg', 'x ^ (1UL << n)')]:
        function('unsigned long', 'dynamic_'+op, 'unsigned long x, unsigned long n', 'return '+expression+';')
    function('int', 'dynamic_test', 'unsigned long x, unsigned long n',
             'if (x & (1UL << n)) return yes(); return no();')
    for bit in [0, 1, 7, 8, 15, 16, 31, 32, 62, 63]:
        for x in [0, 1 << bit, (1 << 64)-1, ((1 << 64)-1) ^ (1 << bit)]:
            for op, expected in [('clr', x & ~(1 << bit)), ('set', x | (1 << bit)), ('chg', x ^ (1 << bit))]:
                checks.append(f'if (dynamic_{op}({literal(x)},{bit}) != {literal(expected)}) return 6;')
            checks.append(f'if (dynamic_test({literal(x)},{bit}) != {11 if x & (1 << bit) else 17}) return 6;')
    # Raw register and general memory instructions, including their OLD-bit Z.
    assembly = ['section ".text",code', 'SecAlign 4', 'xdef _start', 'xref test_stack', 'xref run',
                '_start:', 'lea test_stack,r63', 'add.q #65536,r63', 'sub.q #64,r63', 'jsr run',
                'stop:', 'halt', 'jmp stop', 'xdef emit', 'emit:', 'move.q r2,r3',
                'move.q #3,r2', 'emufunc', 'rts']
    for op in ['clr', 'set', 'chg', 'tst']:
        for form in ['register', 'memory']:
            name = f'raw_{op}_{form}'
            prototypes.append(f'int {name}(unsigned long *, unsigned long);')
            instruction = f'b{op} r3,(r2)' if form == 'memory' else f'rb{op} r3,r1'+('' if op == 'tst' else ',r1')
            assembly += ['xdef '+name, name+':', 'move.q (r2),r1', instruction,
                         'beq '+name+'_zero', 'vmove.q #11,r0', 'jmp '+name+'_done',
                         name+'_zero:', 'vmove.q #17,r0', name+'_done:']
            if form == 'register' and op != 'tst':
                assembly.append('move.q r1,(r2)')
            assembly.append('rts')
            for bit in [0, 7, 8, 15, 16, 31, 32, 63]:
                for x in [1 << bit, ((1 << 64)-1) ^ (1 << bit)]:
                    y = {'clr': x & ~(1 << bit), 'set': x | (1 << bit), 'chg': x ^ (1 << bit), 'tst': x}[op]
                    checks.append(f'v={literal(x)}; if ({name}(&v,{bit}) != {11 if x & (1 << bit) else 17} || v != {literal(y)}) return 7;')
    (work/'entry.vma').write_text('\n'.join(line if line.endswith(':') else '\t'+line for line in assembly)+'\n')
    (work/'functions.c').write_text('extern int yes(void), no(void), consume(unsigned long);\n'+'\n'.join(functions))
    (work/'driver.c').write_text('\n'.join(prototypes)+'''
extern void emit(int);
char test_stack[65536] __attribute__((aligned(65536)));
int yes(void) { return 11; } int no(void) { return 17; }
int consume(unsigned long x) { return x ? 11 : 17; }
void *memcpy(void *d, const void *s, unsigned long n) {
 volatile unsigned char *dst=d; const volatile unsigned char *src=s;
 while (n--) *dst++=*src++; return d;
}
int check(void) { unsigned long v;
'''+'\n'.join(checks)+'''
return 0; }
void run(void) {
 int r=check(); const char *p=r ? "RVM bit ops: FAILED " : "'''+marker+'''\\n";
 while (*p) emit(*p++); if(r) { emit('0'+r); emit('\\n'); }
}
''')
    subprocess.run(['VMAsm', '-i', 'entry.vma', '-o', 'entry.o', '--obj'], cwd=work, check=True)
    shutil.copyfile(args.loader, work/'bootloader.bin')
    (work/'vm.config').write_text((ROOT/'sys/arch/rvm/conf/vmemu.conf').read_text())
    for optimization in ['-O0', '-O2', '-Os']:
        for name in ['functions', 'driver']:
            subprocess.run(['python3', str(STAND/'kernel_cc.py'), optimization, '-ffreestanding',
                            '-fno-builtin', '-nostdinc', '-c', str(work/(name+'.c')),
                            '-o', str(work/(name+'.o'))], env=env, check=True)
        output = (work/'functions.raw.s').read_text()
        if optimization != '-O0':
            def body(name):
                return output.split(name+':\n', 1)[1].split('\n\t.size', 1)[0]
            assert 'rclr.w' in body('low_word'), body('low_word')
            for op in ['clr', 'set', 'chg']:
                for bit in [0, 31, 32, 63]:
                    assert re.search(r'\bvb'+op+rf'\s+#{bit},', body(f'{op}_q_{bit}')), body(f'{op}_q_{bit}')
                assert 'rb'+op in body('dynamic_'+op), body('dynamic_'+op)
            assert 'rbtst' in body('dynamic_test'), body('dynamic_test')
            memory_test = body('test_memory_q_32')
            assert re.search(r'\bv?btst\s+#32,', memory_test), memory_test
            assert not re.search(r'\bmove\.[bwl]\s', memory_test), memory_test
            # GCC may use a shorter sign test for bit 63.
            for bit in [0, 31, 62]:
                assert re.search(r'\bvbtst\s+#'+str(bit)+',', body('test_q_'+str(bit))), body('test_q_'+str(bit))
        subprocess.run(['python3', str(STAND/'kernel_link.py'), '--cc', args.cross+'gcc',
                        '--ld', args.cross+'ld', '--nm', args.cross+'nm', '--ar', args.cross+'ar',
                        '-o', 'netbsd.fhr', 'entry.o', 'functions.o', 'driver.o'],
                       cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
        try:
            result = subprocess.run([args.vmemu, '-c', 'vm.config'], cwd=work, env=env,
                                    capture_output=True, timeout=5)
            raise AssertionError('VM exited: '+str(result.returncode))
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'').decode(errors='replace')
            assert marker in output and 'FAILED' not in output, output
        print('PASS: bit-op compiler patterns and guest edge cases '+optimization, flush=True)
