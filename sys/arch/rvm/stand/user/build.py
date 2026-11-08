#!/usr/bin/env python3
"""Build static FHR C programs with the RVM bootstrap user runtime."""
import argparse
import os
from pathlib import Path
import subprocess
if __package__:
    from .libc import build as libc_objects
else:
    from libc import build as libc_objects


class UserBuilder:
    def __init__(self, toolchain, obj, native_fpu=True):
        self.root = Path(__file__).resolve().parents[5]
        self.stand = self.root/'sys/arch/rvm/stand'
        self.obj = Path(obj).resolve()
        self.obj.mkdir(parents=True, exist_ok=True)
        includes = self.obj/'include'
        includes.mkdir(exist_ok=True)
        if not (includes/'machine').exists():
            (includes/'machine').symlink_to(self.root/'sys/arch/rvm/include', target_is_directory=True)
        for name in ('fcntl.h', 'stdint.h', 'termios.h'):
            if not (includes/name).exists():
                (includes/name).symlink_to(self.root/'sys/sys'/name)
        self.prefix = str(Path(toolchain).resolve()/'bin/rvm-netbsd-')
        gcc_includes = subprocess.check_output([self.prefix+'gcc', '-print-file-name=include'], text=True).strip()
        self.env = dict(os.environ, RVM_REAL_CC=self.prefix+'gcc')
        self.flags = ['-O2', '-ffreestanding', '-fno-builtin', '-fno-pic',
                      '-fno-pie', '-fno-stack-protector', '-D__NetBSD__',
                      '-D_NETBSD_SOURCE', '-fno-zero-initialized-in-bss', '-nostdinc',
                      '-I', str(includes), '-I', str(self.root/'sys'),
                      '-I', str(self.root/'include'),
                      '-I', str(self.root/'common/include'),
                      '-I', str(self.root/'lib/libc/include'),
                      '-I', str(self.root/'lib/csu/common'),
                      '-I', str(self.root/'lib/libc/stdio'),
                      '-I', str(self.root/'lib/libc/locale'),
                      '-I', str(self.root/'lib/libc/citrus'),
                      '-I', str(self.root/'lib/libc/gen'),
                      '-I', str(self.root/'lib/libc/gdtoa'),
                      '-I', str(self.root/'lib/libc/arch/rvm/gdtoa'),
                      '-I', str(self.root/'common/lib/libc/stdlib'),
                      '-isystem', gcc_includes,
                      '-I', str(self.root/'lib/libpthread'), '-I', str(self.stand/'user')]
        if native_fpu:
            self.flags.append('-mfpu')

    def compile(self, source, target, extra=()):
        subprocess.run(['python3', str(self.stand/'kernel_cc.py')] + self.flags + list(extra) +
                       ['-c', str(source), '-o', str(target)], check=True, env=self.env)

    def assemble(self, source, target):
        assembly = Path(target).with_suffix('.s')
        subprocess.run([self.prefix+'gcc', '-nostdinc', '-I', str(self.root/'sys'),
                        '-I', str(self.obj/'include'),
                        '-E', '-P', '-x', 'assembler-with-cpp', str(source),
                        '-o', str(assembly)], check=True)
        subprocess.run([self.prefix+'as', str(assembly), '-o', str(target)], check=True)

    def runtime(self):
        self.compile(self.stand/'user/runtime.c', self.obj/'runtime.o')
        self.compile(self.stand/'user/heap.c', self.obj/'heap.o')
        self.compile(self.stand/'user/process.c', self.obj/'process.o')
        self.compile(self.stand/'user/passwd.c', self.obj/'passwd.o')
        self.compile(self.stand/'user/random.c', self.obj/'random.o')
        self.assemble(self.stand/'user/syscall.S', self.obj/'syscall.o')
        self.assemble(self.stand/'user/setjmp.S', self.obj/'setjmp.o')
        self.assemble(self.stand/'user/fpu_regs.S', self.obj/'fpu_regs.o')
        self.assemble(self.stand/'user/crt0.S', self.obj/'crt0.o')
        self.assemble(self.stand/'user/end.S', self.obj/'end.o')
        library = self.obj/'librvm.a'
        if library.exists():
            library.unlink()
        members = [self.obj/(name+'.o') for name in ('runtime', 'heap', 'process', 'passwd', 'random', 'syscall', 'setjmp', 'fpu_regs')]
        for name in ('strlen', 'strcmp', 'strncmp', 'strchr', 'memcmp', 'memcpy', 'memmove', 'memset', 'strcspn', 'strspn', 'strlcpy', 'memchr', 'strnlen', 'strrchr', 'strpbrk', 'strncpy', 'strlcat', 'bzero', 'strstr', 'strcpy', 'strcat', 'strsep', 'strcasecmp', 'strncasecmp'):
            target = self.obj/(name+'.o')
            self.compile(self.root/'common/lib/libc/string'/(name+'.c'), target,
                         ['-D_LIBC'] if name in ('strlcat', 'strsep', 'strcasecmp', 'strncasecmp') else [])
            members.append(target)
        for source in ('stdlib/malloc.c', 'stdlib/atexit.c', 'stdlib/_env.c',
                       'stdlib/getenv.c', 'stdlib/setenv.c', 'stdlib/unsetenv.c',
                       'stdlib/putenv.c', 'stdlib/reallocarr.c',
                       'gen/getpagesize.c', 'gen/getprogname.c', 'gen/setprogname.c', 'gen/execvp.c'):
            target = self.obj/Path(source).with_suffix('.o').name
            self.compile(self.root/'lib/libc'/source, target,
                         ['-Dalloca=__builtin_alloca'] if source == 'gen/execvp.c' else [])
            members.append(target)
        target = self.obj/'rbtree.o'
        self.compile(self.root/'common/lib/libc/gen/rbtree.c', target, ['-D_LIBC'])
        members.append(target)
        members.extend(libc_objects(self))
        subprocess.run([self.prefix+'ar', 'crs', str(library)] + list(map(str, members)), check=True)

    def program(self, sources, output, extra=()):
        objects = []
        for i, source in enumerate(sources):
            target = self.obj/f'program-{i}-{Path(source).stem}.o'
            if Path(source).suffix == '.S':
                self.assemble(source, target)
            else:
                self.compile(source, target, extra)
            objects.append(str(target))
        subprocess.run([self.prefix+'ld', '-e', '_start', '-o', str(output),
                        str(self.obj/'crt0.o')] + objects + [str(self.obj/'librvm.a'), str(self.obj/'end.o')], check=True)
        Path(output).chmod(0o755)


def main():
    root = Path(__file__).resolve().parents[5]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, default=root/'../rvm_gcc/rvm_cross_netbsd')
    parser.add_argument('--objdir', type=Path, default=root/'obj.rvm-user')
    parser.add_argument('-o', '--output', type=Path, required=True)
    parser.add_argument('--software-float', action='store_true',
                        help='use compiler software arithmetic helpers for comparison builds')
    parser.add_argument('sources', type=Path, nargs='+')
    args = parser.parse_args()
    builder = UserBuilder(args.toolchain, args.objdir, native_fpu=not args.software_float)
    builder.runtime()
    builder.program(args.sources, args.output)


if __name__ == '__main__':
    main()
