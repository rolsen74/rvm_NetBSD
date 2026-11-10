#!/usr/bin/env python3
"""Run the normal static-kernel binding/data adapters in the real FHR loader."""
import argparse
import os
from pathlib import Path
import re
import shutil
import struct
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
long common __attribute__((common));
static volatile long zero_a, zero_b;
volatile long *volatile global_pointer __attribute__((section(".data"))) = 0;
volatile long *volatile global_zero;
static volatile long *volatile zero_pointer = &zero_a;
long common_read(void) {
    if (zero_a != 0 || zero_b != 0) return -1;
    zero_a = 11; zero_b = 23;
    int ok = zero_a == 11 && zero_b == 23;
    zero_pointer = &zero_b;
    *zero_pointer = 24;
    ok = ok && zero_b == 24;
    zero_pointer = &zero_a;
    *zero_pointer = 12;
    ok = ok && zero_a == 12;
    global_pointer = &zero_a;
    global_zero = &zero_b;
    ok = ok && *global_pointer == 12 && *global_zero == 24;
    zero_a = 0; zero_b = 0;
    return ok ? common : -1;
}
static long local = 17;
long * const probe_a __attribute__((section("link_set_probe"))) = &local;
static const long * const pointers[] = { &local };
static long other = 31;
static const long *volatile current = &other;
long a(void) { current = pointers[0]; return *current; }
int base(void) { return 7; }
__asm__(".weak spaced_alias\\n  spaced_alias = base\\n");
__asm__(".weak later_alias\\n later_alias = later_base\\n");
int alias(void) __attribute__((weak,alias("base")));
__attribute__((weak)) int choose(void) { return 5; }
''')
    (work/'b.c').write_text('''
extern long common;
void common_set(void) { common = 42; }
static long local = 29;
long * const probe_b __attribute__((section("link_set_probe"))) = &local;
long b(void) { return local; }
int later_base(void) { return 43; }
int later_alias(void) __attribute__((weak,alias("later_base")));
#ifdef STRONG
int choose(void) { return 9; }
int alias(void) { return 21; }
int spaced_alias(void) { return 23; }
#endif
''')
    (work/'main.c').write_text('''
extern long a(void), b(void), common_read(void);
extern void common_set(void);
extern int choose(void), alias(void), base(void), spaced_alias(void);
extern int later_alias(void);
extern int first_same(void), second_same(void);
extern unsigned long stack_address(void);
extern void emit(int);
volatile long initialized_scalar = 41;
char test_stack[65536] __attribute__((aligned(65536)));
extern long * const __start_link_set_probe[], * const __stop_link_set_probe[];
int check(void) {
    const char *s;
    int zero = common_read() == 0 && test_stack[0] == 0 && test_stack[65535] == 0;
    zero = zero && initialized_scalar == 41;
    test_stack[0] = 1;
    test_stack[65535] = 2;
    common_set();
    int ok = zero && common_read() == 42 && a() == 17 && b() == 29 && !(stack_address() & 65535);
    ok = ok && test_stack[0] == 1 && test_stack[65535] == 2;
    ok = ok && __stop_link_set_probe - __start_link_set_probe == 2;
    ok = ok && *__start_link_set_probe[0] + *__start_link_set_probe[1] == 46;
    ok = ok && later_alias() == 43;
    ok = ok && first_same() == 11 && second_same() == 13;
#if STRONG
    ok = ok && spaced_alias() == 23 && choose() == 9 && alias() == 21 && alias != base;
#else
    ok = ok && spaced_alias() == 7 && choose() == 5 && alias() == 7 && alias == base;
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
                        '-DRVM_KERNEL',
                        '-fno-builtin', '-fno-pic', '-fno-pie', '-nostdinc'] +
                       (['-DSTRONG=1'] if strong else []) +
                       ['-c', source, '-o', target], cwd=work, env=env, check=True)
    compile('a.c', 'a.o')
    compile('b.c', 'b.o')
    compile('b.c', 'b_strong.o', True)
    compile('main.c', 'main.o')
    compile('main.c', 'main_strong.o', True)
    for directory, value in (('first', 11), ('second', 13)):
        (work/directory).mkdir()
        (work/directory/'same.c').write_text(
            f'static volatile int local = {value};\n'
            f'int {directory}_same(void) {{ return local; }}\n')
        compile(directory+'/same.c', directory+'/same.o')
    # GCC can place a .comm between functions at -O0. Its explicit-storage
    # replacement must not move the following function into non-executable data.
    (work/'common.c').write_text('int before(void) { return 1; }\n'
                                'static long common;\n'
                                'long after_common(void) { return ++common; }\n'
                                'long dedicated __attribute__((section(".data.cacheline_aligned")));\n')
    subprocess.run([str(stand/'kernel_cc.py'), '-O0', '-ffreestanding',
                    '-fno-builtin', '-fno-pic', '-fno-pie', '-nostdinc',
                    '-c', 'common.c', '-o', 'common.o'], cwd=work, env=env, check=True)
    symbols = subprocess.check_output([args.cross+'nm', '-P', 'common.o'], cwd=work, text=True)
    assert re.search(r'^after_common T ', symbols, re.MULTILINE), symbols
    assert re.search(r'^dedicated D ', symbols, re.MULTILINE), symbols
    print('PASS: common storage preserves the active text section', flush=True)
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
                        '--ar', args.cross+'ar', '-o', 'netbsd.fhr', 'entry.o',
                        'first/same.o', 'second/same.o'] + objects,
                       cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
        mapping = (work/'netbsd.fhr.map').read_text()
        sections = re.findall(r'^(\.\S+)\s+0x[0-9a-f]+\s+0x[0-9a-f]+', mapping, re.M)
        assert sections == ['.text', '.data', '.rodata', '.bss'], sections
        assert re.search(r'^\.bss\s+0x[0-9a-f]+\s+0x[1-9a-f][0-9a-f]{4,}', mapping, re.M), mapping
        executable = (work/'netbsd.fhr').read_bytes()
        assert int.from_bytes(executable[14:16], 'big') == 4
        headers = [struct.unpack_from('>QHH', executable, 16 + 12*i) for i in range(4)]
        assert [flags for size, alignment, flags in headers] == [4, 3, 2, 3], headers
        assert executable[-8:] == b'BSS\0END\0', 'BSS must have no stored zero payload'
        try:
            result = subprocess.run([args.vmemu, '-c', 'vm.config'], cwd=work,
                                    env=env, capture_output=True, timeout=2)
            raise AssertionError('VM exited: '+str(result.returncode))
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'').decode(errors='replace')
            output = re.sub(r'VMEmu display: SDL video driver [^\r\n]*\r?\n', '', output)
            assert marker in output and 'FAILED' not in output, output
        print('PASS: normal kernel tools: ' + label, flush=True)
