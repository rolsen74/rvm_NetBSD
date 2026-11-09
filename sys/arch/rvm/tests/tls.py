#!/usr/bin/env python3
"""Run GCC's emulated TLS output at -O0/-O2/-Os in real user processes."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT/'sys/arch/rvm/stand'))
from user.build import UserBuilder


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, default=ROOT/'obj.rvm-diskroot')
    parser.add_argument('--toolchain', type=Path, default=ROOT/'../rvm_gcc/rvm_cross_netbsd')
    parser.add_argument('--vmemu', default='VMEmu')
    parser.add_argument('--ram-mib', type=int, default=64)
    args = parser.parse_args()
    obj = args.objdir.resolve()
    with tempfile.TemporaryDirectory(prefix='rvm-tls-') as directory:
        work = Path(directory)
        builder = UserBuilder(args.toolchain, work/'user')
        for name in ('crt0.o', 'end.o', 'librvm.a', 'librvm.init.json'):
            shutil.copyfile(obj/'user'/name, builder.obj/name)
        installs, commands = [], []
        for optimization in ('-O0', '-O2', '-Os'):
            name = 'tls-'+optimization[1:]
            program = work/name
            builder.program([ROOT/'sys/arch/rvm/stand/user/tls_check.c',
                             ROOT/'sys/arch/rvm/stand/user/tls_other.c'], program, [optimization])
            assembly = (builder.obj/'program-0-tls_check.raw.s').read_text()
            assert '__emutls_get_address' in assembly and '__emutls_v.tls_number' in assembly
            installs += ['--install', str(program), '/bin/'+name]
            commands += ['--command', 'run /bin/'+name,
                         'emulated TLS initialization, alignment, constructors, signals, fork/COW and exec verified\nstatus=0']
        for name in ('netbsd.fhr', 'bootloader.bin'):
            shutil.copyfile(obj/name, work/name)
        subprocess.run([sys.executable, str(ROOT/'sys/arch/rvm/stand/make_root.py'),
                        '--objdir', str(work), '--makefs', str(obj/'hosttools/bin/nbmakefs'),
                        '--toolchain', str(args.toolchain.resolve())] + installs, check=True)
        try:
            subprocess.run([sys.executable, str(Path(__file__).with_name('console.py')),
                            '--objdir', str(work), '--vmemu', args.vmemu,
                            '--ram-mib', str(args.ram_mib)] + commands, check=True)
        finally:
            if (work/'console-keyboard.log').exists():
                shutil.copyfile(work/'console-keyboard.log', obj/f'tls-{args.ram_mib}.log')
    print(f'PASS: emulated TLS at -O0/-O2/-Os ({args.ram_mib} MiB)')


if __name__ == '__main__':
    main()
