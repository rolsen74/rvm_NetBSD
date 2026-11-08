#!/usr/bin/env python3
"""Build static FHR C programs with the RVM bootstrap user runtime."""
import argparse
import os
from pathlib import Path
import subprocess


class UserBuilder:
    def __init__(self, toolchain, obj):
        self.root = Path(__file__).resolve().parents[5]
        self.stand = self.root/'sys/arch/rvm/stand'
        self.obj = Path(obj).resolve()
        self.obj.mkdir(parents=True, exist_ok=True)
        includes = self.obj/'include'
        includes.mkdir(exist_ok=True)
        if not (includes/'machine').exists():
            (includes/'machine').symlink_to(self.root/'sys/arch/rvm/include', target_is_directory=True)
        for name in ('fcntl.h', 'stdint.h'):
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
                      '-isystem', gcc_includes,
                      '-I', str(self.root/'lib/libpthread'), '-I', str(self.stand/'user')]

    def compile(self, source, target, extra=()):
        subprocess.run(['python3', str(self.stand/'kernel_cc.py')] + self.flags + list(extra) +
                       ['-c', str(source), '-o', str(target)], check=True, env=self.env)

    def assemble(self, source, target):
        assembly = Path(target).with_suffix('.s')
        subprocess.run([self.prefix+'gcc', '-nostdinc', '-I', str(self.root/'sys'),
                        '-E', '-P', '-x', 'assembler-with-cpp', str(source),
                        '-o', str(assembly)], check=True)
        subprocess.run([self.prefix+'as', str(assembly), '-o', str(target)], check=True)

    def runtime(self):
        self.compile(self.stand/'user/runtime.c', self.obj/'runtime.o')
        self.compile(self.stand/'user/heap.c', self.obj/'heap.o')
        self.assemble(self.stand/'user/syscall.S', self.obj/'syscall.o')
        self.assemble(self.stand/'user/crt0.S', self.obj/'crt0.o')
        self.assemble(self.stand/'user/end.S', self.obj/'end.o')
        library = self.obj/'librvm.a'
        if library.exists():
            library.unlink()
        members = [self.obj/'runtime.o', self.obj/'heap.o', self.obj/'syscall.o']
        for name in ('strlen', 'strcmp', 'strncmp', 'strchr', 'memcmp', 'memcpy', 'memmove', 'memset', 'strcspn', 'strspn', 'strlcpy'):
            target = self.obj/(name+'.o')
            self.compile(self.root/'common/lib/libc/string'/(name+'.c'), target)
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
        subprocess.run([self.prefix+'ar', 'crs', str(library)] + list(map(str, members)), check=True)

    def program(self, sources, output):
        objects = []
        for i, source in enumerate(sources):
            target = self.obj/f'program-{i}-{Path(source).stem}.o'
            self.compile(source, target)
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
    parser.add_argument('sources', type=Path, nargs='+')
    args = parser.parse_args()
    builder = UserBuilder(args.toolchain, args.objdir)
    builder.runtime()
    builder.program(args.sources, args.output)


if __name__ == '__main__':
    main()
