#!/usr/bin/env python3
"""Run the normal static-kernel binding/data adapters in the real FHR loader."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cross', default='rvm-netbsd-')
parser.add_argument('--vmemu', default='VMEmu')
parser.add_argument('--vmasm', default='VMAsm')
parser.add_argument('--loader', type=Path, default=Path('obj.rvm-bootstrap/bootloader.bin'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[4]
stand = root / 'sys/arch/rvm/stand'
env = dict(os.environ, RVM_REAL_CC=shutil.which(args.cross + 'gcc'),
           SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
marker = 'RVM normal tools: binding, private symbols, pointer tables and alignment verified'
with tempfile.TemporaryDirectory(prefix='rvm-normal-tools-') as temporary:
    work = Path(temporary)
    (work/'a.c').write_text('''
static long local = 17;
static const long * const pointers[] = { &local };
long a(void) { return *pointers[0]; }
int base(void) { return 7; }
int alias(void) __attribute__((weak,alias("base")));
__attribute__((weak)) int choose(void) { return 5; }
''')
    (work/'b.c').write_text('''
static long local = 29;
long b(void) { return local; }
#ifdef STRONG
int choose(void) { return 9; }
int alias(void) { return 21; }
#endif
''')
    (work/'main.c').write_text('''
extern long a(void), b(void);
extern int choose(void), alias(void), base(void);
extern unsigned long stack_address(void);
extern void emit(int);
char test_stack[65536] __attribute__((aligned(65536)));
int check(void) {
    const char *s;
    int ok = a() == 17 && b() == 29 && !(stack_address() & 65535);
#if STRONG
    ok = ok && choose() == 9 && alias() == 21 && alias != base;
#else
    ok = ok && choose() == 5 && alias() == 7 && alias == base;
#endif
    s = ok ? "''' + marker + '''\\n" : "RVM normal tools: FAILED\\n";
    while (*s) emit(*s++);
    return ok;
}
''')
    (work/'entry.vma').write_text('''
 section ".text",code
 SecAlign 4
 xdef _start
 xref test_stack
 xref check
_start:
 lea test_stack,r63
 move.q #65536,r1
 radd.q r1,r63,r63
 sub.q #64,r63
 jsr check
stop:
 halt
 jmp stop
 xdef emit
emit:
 move.q r2,r3
 move.q #3,r2
 emufunc
 rts
 xdef stack_address
stack_address:
 lea test_stack,r0
 rts
''')
    def compile(source, target, strong=False):
        subprocess.run([str(stand/'kernel_cc.py'), '-O2', '-ffreestanding',
                        '-fno-builtin', '-fno-pic', '-fno-pie', '-nostdinc'] +
                       (['-DSTRONG=1'] if strong else []) +
                       ['-c', source, '-o', target], cwd=work, env=env, check=True)
    compile('a.c', 'a.o')
    compile('b.c', 'b.o')
    compile('b.c', 'b_strong.o', True)
    compile('main.c', 'main.o')
    compile('main.c', 'main_strong.o', True)
    subprocess.run([args.vmasm, '-i', 'entry.vma', '-o', 'entry.o', '--obj'],
                   cwd=work, check=True, stdout=subprocess.DEVNULL)
    subprocess.run([args.cross+'ar', 'crs', 'test.a', 'a.o', 'b_strong.o'],
                   cwd=work, check=True)
    shutil.copyfile(args.loader.resolve(), work/'bootloader.bin')
    config = (root/'sys/arch/rvm/conf/vmemu.conf').read_text()
    (work/'vm.config').write_text(re.sub(r'Size = \d+', 'Size = 256', config, count=1))
    for label, objects in (
        ('weak default/alias identity', ['a.o', 'b.o', 'main.o']),
        ('strong override', ['a.o', 'b_strong.o', 'main_strong.o']),
        ('reverse object order', ['b_strong.o', 'a.o', 'main_strong.o']),
        ('archive members', ['main_strong.o', 'test.a']),
    ):
        subprocess.run(['python3', str(stand/'kernel_link.py'), '--cc', env['RVM_REAL_CC'],
                        '--ld', args.cross+'ld', '--nm', args.cross+'nm',
                        '--ar', args.cross+'ar', '-o', 'netbsd.fhr', 'entry.o'] + objects,
                       cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
        try:
            result = subprocess.run([args.vmemu, '-c', 'vm.config'], cwd=work,
                                    env=env, capture_output=True, timeout=2)
            raise AssertionError('VM exited: '+str(result.returncode))
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'').decode(errors='replace')
            output = re.sub(r'VMEmu display: SDL video driver [^\r\n]*\r?\n', '', output)
            assert marker in output and 'FAILED' not in output, output
        print('PASS: normal kernel tools: ' + label, flush=True)
