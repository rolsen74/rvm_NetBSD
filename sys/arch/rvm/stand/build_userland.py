#!/usr/bin/env python3
"""Build the native static FHR libc/csu with NetBSD headers and BSD make.

This is a static, single-LWP configuration using NetBSD's non-jemalloc
allocator. It does not build a release, libpthread, shared libraries or an
ELF dynamic linker. Link programs with stand/user_link.py and retain the
archive's adjacent assembly and binding/initializer metadata.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess
import sys


def run(command, root):
    subprocess.run(list(map(str, command)), cwd=root, check=True)


def main():
    root = Path(__file__).resolve().parents[4]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, default=root/'obj.rvm-userland')
    parser.add_argument('--tooldir', type=Path)
    parser.add_argument('--libm', action='store_true', help='also build the normal static libm catalog')
    parser.add_argument('--library', action='append', default=[],
                        help='also build a static library source directory such as lib/libutil')
    parser.add_argument('-V', '--make-variable', action='append', default=[],
                        metavar='NAME=VALUE', help='set a target configuration variable in mk.conf')
    parser.add_argument('--toolchain', type=Path, default=root/'../rvm_gcc/rvm_cross_netbsd')
    parser.add_argument('-j', '--jobs', type=int, default=4)
    parser.add_argument('--program', action='append', default=[],
                        help='also build a source directory such as bin/echo with bsd.prog.mk')
    args = parser.parse_args()
    obj, cross = args.objdir.resolve(), args.toolchain.resolve()
    tools = args.tooldir.resolve() if args.tooldir else obj/'hosttools'
    obj.mkdir(parents=True, exist_ok=True)
    # BSD make uses the source directory if the mapped object directory does
    # not exist. These two libraries are built before the optional libraries
    # and programs, whose object directories are created below.
    for directory in ('lib/libc', 'lib/csu'):
        (obj/directory).mkdir(parents=True, exist_ok=True)
    cc = cross/'bin/rvm-netbsd-gcc'
    macros = subprocess.check_output([str(cc), '-dM', '-E', '-x', 'c', '-'],
                                      input='', text=True)
    for expected in ('#define __NetBSD__ 1', '#define __WINT_TYPE__ int',
                     '#define __UINT_LEAST16_TYPE__ short unsigned int',
                     '#define __RVM_CALLER_ARG_HOME__ 64'):
        if expected not in macros:
            raise SystemExit('Rebuild GCC with the RVM NetBSD type/caller ABI patches: '+expected)
    gcc_include = subprocess.check_output([str(cc), '-print-file-name=include'], text=True).strip()
    config = obj/'mk.conf'
    content = f'''# Native static RVM userland bring-up configuration.
MKPIC=no
MKPROFILE=no
MKLINT=no
MKDEBUG=no
MKCTF=no
MKMAN=no
MKDOC=no
MKINFO=no
MKNLS=no
MKYP=no
MKHESIOD=no
MKDTB=no
MKKMOD=no
USE_SSP=no
USE_FORT=no
USE_JEMALLOC=no
HAVE_LIBGCC_EH=yes
CPPFLAGS+= -nostdinc -isystem ${{DESTDIR}}/usr/include -isystem {gcc_include}
COPTS+= -mfpu
CWARNFLAGS+= -Wno-error=attributes
'''
    for setting in args.make_variable:
        name, separator, value = setting.partition('=')
        if not separator or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.]*', name) or '\n' in value or '\r' in value:
            raise SystemExit('Expected a single NAME=VALUE make assignment: '+setting)
        content += name+'='+value+'\n'
    if not config.exists() or config.read_text() != content:
        config.write_text(content)
    shared_wrapper = tools/'bin/nbmake-rvm'
    saved_wrapper = (shared_wrapper.read_bytes() if shared_wrapper.is_file() and
                     not tools.is_relative_to(obj) else None)
    make = obj/'nbmake-rvm'
    try:
        run([root/'build.sh', '-m', 'rvm', '-U', '-u', '-O', obj, '-T', tools,
             '-V', 'EXTERNAL_TOOLCHAIN='+str(cross), '-V', 'MAKECONF='+str(config),
             '-V', 'MKGDB=no', 'makewrapper'], root)
        make.write_bytes(shared_wrapper.read_bytes())
        make.chmod(0o755)
    finally:
        # Reusing kernel host tools must not leave their make wrapper pointing
        # at the userland object directory and configuration.
        if saved_wrapper is not None:
            shared_wrapper.write_bytes(saved_wrapper)
    subset = ('host-mkdep compat binstall .WAIT date mktemp sed m4 yacc '
              '.WAIT lex pax .WAIT awk genassym lorder tsort join rpcgen mkdep cksum stat makefs')
    if 'lib/libterminfo' in args.library:
        subset += ' nbperf .WAIT tic'
    if 'usr.bin/login' in args.program:
        subset += ' pwd_mkdb'
    # obj and build_install must be separate invocations: make cannot change
    # its working directory after the obj target has created that directory.
    run([make, '-C', 'tools', 'SUBDIR='+subset, 'MAKECONF=/dev/null', 'obj'], root)
    run([make, '-C', 'tools', 'SUBDIR='+subset, 'MAKECONF=/dev/null', '-j', args.jobs, 'build_install'], root)
    dest = obj/'destdir.rvm'
    headers = dest/'usr/include'
    # Older bring-up SDKs used source-directory symlinks. Replace those
    # directory links before install rules can follow them into the sources.
    if headers.is_symlink():
        headers.unlink()
    headers.mkdir(parents=True, exist_ok=True)
    for path in headers.rglob('*'):
        if path.is_symlink() and path.is_dir() and not path.resolve().is_relative_to(headers.resolve()):
            path.unlink()
            path.mkdir()
    for line in (root/'etc/mtree/NetBSD.dist.base').read_text().splitlines():
        fields = line.split()
        if fields and fields[0].startswith('./usr/include'):
            (dest/fields[0][2:]).mkdir(parents=True, exist_ok=True)
    for tree in ('include', 'sys', 'common/include', 'lib/libpthread', 'lib/libcurses',
                 'external/bsd/elftoolchain/common', 'external/bsd/blocklist/include',
                 'external/bsd/libevent/include'):
        for path in (root/tree).rglob('Makefile'):
            (obj/path.parent.relative_to(root)).mkdir(parents=True, exist_ok=True)
    for directory in ('include', 'sys', 'lib/libpthread', 'lib/libcurses',
                      'external/bsd/elftoolchain/common/sys',
                      'external/bsd/blocklist/include',
                      'external/bsd/libevent/include'):
        run([make, '-C', directory, '-j', args.jobs, 'includes'], root)
    # A changed external compiler or FHR adapter must invalidate normal make's
    # objects too, even when their C source timestamps have not changed.
    digest = hashlib.sha256(config.read_bytes())
    paths = [cc]
    for name in ('cc1', 'as', 'ld'):
        program = subprocess.check_output([str(cc), '-print-prog-name='+name], text=True).strip()
        paths.append(Path(program) if '/' in program else Path(shutil.which(program) or program))
    stand = root/'sys/arch/rvm/stand'
    paths += [stand/name for name in ('kernel_cc.py', 'user_cc.py', 'fhr_csu.py',
                                      'fhr_asm.py', 'static_init.py', 'user/emutls.py')]
    for path in paths:
        digest.update(path.read_bytes())
    stamp = obj/'userland-toolchain.stamp'
    value = digest.hexdigest()+'\n'
    if not stamp.exists() or stamp.read_text() != value:
        for directory in ('lib', 'bin', 'sbin', 'usr.bin', 'usr.sbin',
                          'libexec', 'external', 'crypto'):
            for pattern in ('*.o', '*.a'):
                for path in (obj/directory).rglob(pattern):
                    path.unlink()
        stamp.write_text(value)
    run([make, '-C', 'lib/libc', 'NOSUBDIR=yes', '-j', args.jobs, 'depend'], root)
    run([make, '-C', 'lib/libc', '-j', args.jobs, 'libc.a'], root)
    run([make, '-C', 'lib/csu', '-j', args.jobs, 'all'], root)
    if args.libm:
        (obj/'lib/libm').mkdir(parents=True, exist_ok=True)
        run([make, '-C', 'lib/libm', '-j', args.jobs, 'depend'], root)
        run([make, '-C', 'lib/libm', '-j', args.jobs, 'libm.a'], root)
    members = subprocess.check_output([str(cross/'bin/rvm-netbsd-ar'), 't',
                                       str(obj/'lib/libc/libc.a')], text=True).splitlines()
    for required in ('initfini.o', 'rvm_atomic.o', 'printf.o', 'malloc.o', 'gcc_emutls.o'):
        if required not in members:
            raise SystemExit('Incomplete libc archive: missing '+required)
    # This is a build-tree SDK. Resolve its symlinks when linking so the
    # FHR binding/initializer sidecars remain associated with their archive.
    lib = dest/'usr/lib'
    lib.mkdir(parents=True, exist_ok=True)
    libraries = [('libc.a', obj/'lib/libc/libc.a')]
    if args.libm:
        libraries.append(('libm.a', obj/'lib/libm/libm.a'))
    os.environ['RVM_REAL_CC'] = str(cc)
    for library in args.library:
        directory = Path(library)
        if (directory.is_absolute() or '..' in directory.parts or
                not (root/directory/'Makefile').is_file()):
            raise SystemExit('Expected a library source directory containing a Makefile: '+library)
        (obj/directory).mkdir(parents=True, exist_ok=True)
        name = subprocess.check_output([str(make), '-C', str(directory),
            '-V', '${LIB}'], cwd=root, text=True).strip()
        if not name or not name.replace('_', '').isalnum():
            raise SystemExit('Expected a BSD static library Makefile: '+library)
        overrides = ['CC='+str(stand/'user_cc.py'), 'OBJCOPY=true']
        run([make, '-C', directory, '-j', args.jobs, 'includes'] + overrides, root)
        run([make, '-C', directory, '-j', args.jobs, 'depend'] + overrides, root)
        run([make, '-C', directory, '-j', args.jobs, 'lib'+name+'.a'] + overrides, root)
        libraries.append(('lib'+name+'.a', obj/directory/('lib'+name+'.a')))
    for name, origin in libraries + [
            (name+'.o', obj/'lib/csu'/(name+'.o'))
            for name in ('crt0', 'crtbegin', 'crtend')]:
        path = lib/name
        if path.is_symlink() and path.resolve() == origin.resolve():
            continue
        if path.is_symlink() or path.is_file():
            path.unlink()
        elif path.exists():
            raise SystemExit('SDK library path is a directory: '+str(path))
        path.symlink_to(origin)
    def build_program(program):
        directory = Path(program)
        if directory.is_absolute() or '..' in directory.parts or not (root/directory/'Makefile').exists():
            raise SystemExit('Expected a source directory containing a Makefile: '+program)
        (obj/directory).mkdir(parents=True, exist_ok=True)
        command = shlex.join([sys.executable, str(stand/'user_prog.py'),
                              '--toolchain', str(cross), '--sysroot', str(dest)])
        names = subprocess.check_output([str(make), '-C', str(directory), 'MKRUMP=no',
            '-V', '${PROGS} ${PROGS_CXX}'], cwd=root, text=True).split()
        links = ['_CCLINK.'+name+'='+command for name in names]
        run([make, '-C', directory, '-j', args.jobs,
             'MKRUMP=no',
             'CC='+str(stand/'user_cc.py'), 'RVM_REAL_CC='+str(cc),
             '_CCLINK='+command, 'LDSTATIC=-static', 'LDFLAGS=', '_PROGLDOPTS=',
             'LIBCRTBEGIN='+str(lib/'crtbegin.o'), 'LIBCRTEND='+str(lib/'crtend.o'),
             'LIBCRTI=', 'MKPIE=no', 'MKSTRIPIDENT=no', 'all'] + links, root)
    # Each program has its own object directory; the installed headers and
    # libraries above are read-only throughout this phase.
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        for result in executor.map(build_program, args.program):
            pass
    if args.libm:
        math_members = subprocess.check_output([str(cross/'bin/rvm-netbsd-ar'), 't',
            str(obj/'lib/libm/libm.a')], text=True).splitlines()
        for required in ('fenv.o', 'fenvreg.o', 'e_sqrt.o', 's_lrint.o', 's_fmaf.o'):
            if required not in math_members:
                raise SystemExit('Incomplete libm archive: missing '+required)
        print(f'RVM native libm: {len(math_members)} archive members in {obj}/lib/libm/libm.a')
    print(f'RVM native libc: {len(members)} archive members in {obj}/lib/libc/libc.a')
    print(f'RVM static startup: {obj}/lib/csu/{{crt0,crtbegin,crtend}}.o')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
