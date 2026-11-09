#!/usr/bin/env python3
"""Build and boot programs linked with the normal BSD-make libc/csu."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
STAND = ROOT/'sys/arch/rvm/stand'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, default=ROOT/'obj.rvm-native-userland')
    parser.add_argument('--kernel-objdir', type=Path, default=ROOT/'obj.rvm-diskroot')
    parser.add_argument('--toolchain', type=Path, default=ROOT/'../rvm_gcc/rvm_cross_netbsd')
    parser.add_argument('--vmemu', default='VMEmu')
    parser.add_argument('--export-tree', type=Path)
    parser.add_argument('--reuse-tree', type=Path,
                        help='repack an explicitly exported fixture tree after relinking tests')
    parser.add_argument('--ram-mib', type=int, default=64)
    parser.add_argument('--timeout', type=float, default=300)
    args = parser.parse_args()
    obj, kernel, cross = args.objdir.resolve(), args.kernel_objdir.resolve(), args.toolchain.resolve()
    work = obj/'guest-tests'
    work.mkdir(exist_ok=True)
    cc = str(cross/'bin/rvm-netbsd-gcc')
    env = dict(os.environ, RVM_REAL_CC=cc)
    gcc_include = subprocess.check_output([cc, '-print-file-name=include'], text=True).strip()
    flags = ['-O2', '-mfpu', '-D_NETBSD_SOURCE', '-nostdinc', '-isystem',
             str(obj/'destdir.rvm/usr/include'), '-isystem', gcc_include]
    programs = [
        ('native-libc-check', [ROOT/'sys/arch/rvm/tests/native_libc.c',
                               ROOT/'sys/arch/rvm/tests/native_syscall.S'],
         'RVM normal libc: startup, stdio, errno, heap, FP control, contexts, TLS and shared RAS atomics verified'),
        ('native-constructors', [STAND/'user'/name for name in
         ('constructor_check.c', 'constructor_other.c', 'constructor_preinit.S')],
         'preinit, constructor priorities, destructors, atexit and recursive exit verified'),
        ('native-tls', [STAND/'user/tls_check.c', STAND/'user/tls_other.c'],
         'emulated TLS initialization, alignment, constructors, signals, fork/COW and exec verified')]
    programs.append(('native-context', [ROOT/'sys/arch/rvm/tests/native_context.c'],
        'RVM normal libc: makecontext/swapcontext arguments, return links, signal masks and FPU state verified'))
    file_utilities = ('ls', 'test', 'mkdir', 'cp', 'mv', 'rm')
    if all((obj/'bin'/name/name).is_file() for name in file_utilities):
        programs.append(('native-utilities', [ROOT/'sys/arch/rvm/tests/native_utilities.c'],
            'RVM native utilities: FFS mkdir, test, copy, recursive symlinks, rename, listing and removal verified'))
    else:
        file_utilities = ()
    math_library = obj/'lib/libm/libm.a'
    if math_library.exists():
        programs.append(('native-math', [ROOT/'sys/arch/rvm/tests/native_math.c',
                         STAND/'user/math_check.c', STAND/'user/fpu_regs.S'],
                         'RVM normal libm: binary32 FMA, elementary functions and complex argument/result ABI verified'))
    installs, commands = [], []
    for name, sources, marker in programs:
        objects = []
        for i, source in enumerate(sources):
            target = work/f'{name}-{i}.o'
            # Boundary/exception checks must call the library rather than
            # accept GCC's builtin folding of out-of-range math results.
            extra = ['-fno-builtin', '-frounding-math'] if name == 'native-math' else []
            subprocess.run([sys.executable, str(STAND/'user_cc.py')] + flags + extra +
                           ['-c', str(source), '-o', str(target)], check=True, env=env)
            objects.append(str(target))
        program = work/name
        libraries = [str(obj/'lib/libc/libc.a')]
        if name == 'native-math':
            libraries.insert(0, str(math_library))
        subprocess.run([sys.executable, str(STAND/'user_link.py'), '--cc', cc,
                        '--ld', str(cross/'bin/rvm-netbsd-ld'),
                        '--nm', str(cross/'bin/rvm-netbsd-nm'),
                        '--ar', str(cross/'bin/rvm-netbsd-ar'),
                        '--end', str(obj/'lib/csu/crtend.o'), '-o', str(program),
                        str(obj/'lib/csu/crt0.o')] + objects +
                       [str(obj/'lib/csu/crtbegin.o')] + libraries, check=True)
        installs += ['--install', str(program), '/bin/'+name]
        commands += ['--command', 'run /bin/'+name, marker+'\nstatus=0']
    for name, arguments, expected in (
            ('echo', ' native-utility', 'native-utility\nstatus=0'),
            ('cat', ' /etc/motd', 'Welcome to NetBSD/RVM on a real FFS root filesystem.\nstatus=0'),
            ('pwd', '', '/\nstatus=0'), ('sync', '', 'status=0')):
        artifact = obj/'bin'/name/name
        if artifact.exists():
            installs += ['--install', str(artifact), '/bin/native-'+name]
            commands += ['--command', 'run /bin/native-'+name+arguments, expected]
    for name in file_utilities:
        installs += ['--install', str(obj/'bin'/name/name), '/bin/native-'+name]
    shell = obj/'bin/sh/sh'
    if shell.is_file():
        installs += ['--install', str(shell), '/bin/sh',
                     '--install', str(Path(__file__).with_name('native_shell.sh')), '/etc/native-shell.sh']
        commands += ['--command', 'run /bin/sh /etc/native-shell.sh',
            'RVM native shell: arithmetic, expansions, functions, pipelines, heredocs, traps and status verified\nstatus=0']
    makefs = Path(subprocess.check_output([str(obj/'nbmake-rvm'), '-C',
        str(ROOT/'lib/libc'), '-V', '${TOOL_MAKEFS}'], text=True).strip())
    if not makefs.is_file():
        raise SystemExit('Missing host makefs; rebuild with build-userland.sh')
    for name in ('netbsd.fhr', 'bootloader.bin'):
        shutil.copyfile(kernel/name, work/name)
    export = ['--export-tree', str(args.export_tree)] if args.export_tree else []
    if args.reuse_tree:
        tree = args.reuse_tree.resolve()
        spec = Path(str(tree)+'.spec')
        if not spec.exists() or not (work/'vm.diskroot.config').exists():
            raise SystemExit('--reuse-tree requires a previously exported native fixture')
        for i in range(0, len(installs), 3):
            target = tree/installs[i+2].lstrip('/')
            if not target.exists():
                with spec.open('a') as metadata:
                    metadata.write('./'+str(target.relative_to(tree))+
                                   ' type=file mode=0755 uid=0 gid=0\n')
            shutil.copyfile(installs[i+1], target)
            target.chmod(0o755)
        image = work/'root-fixture.new'
        subprocess.run([str(makefs), '-t', 'ffs', '-B', 'be',
                        '-s', '16m', '-f', '256', '-F', str(spec), '-o',
                        'version=2,bsize=16384,fsize=2048', str(image), str(tree)], check=True)
        image.replace(work/'root-fixture.img')
    else:
        subprocess.run([sys.executable, str(STAND/'make_root.py'), '--objdir', str(work),
                        '--makefs', str(makefs),
                        '--toolchain', str(cross)] + installs + export, check=True)
    subprocess.run([sys.executable, str(Path(__file__).with_name('console.py')),
                    '--objdir', str(work), '--vmemu', args.vmemu,
                    '--ram-mib', str(args.ram_mib), '--timeout', str(args.timeout)] +
                   (['--shell'] if shell.is_file() else []) + commands, check=True)
    print('PASS: normal NetBSD libc/csu guest integration')


if __name__ == '__main__':
    main()
