#!/usr/bin/env python3
"""Run GCC's native FPU output at -O0/-O2/-Os through the real user-mode port."""
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
    marker = 'GCC native FPU arithmetic, sqrt/abs, rounding, exceptions and FP-register preservation verified'
    with tempfile.TemporaryDirectory(prefix='rvm-native-fpu-') as directory:
        work = Path(directory)
        builder = UserBuilder(args.toolchain, work/'user')
        for name in ('crt0.o', 'end.o', 'librvm.a'):
            shutil.copyfile(obj/'user'/name, builder.obj/name)
        installs, commands = [], []
        for opt in ('-O0', '-O2', '-Os'):
            name = 'fpu-'+opt[1:]
            program = work/name
            builder.program([ROOT/'sys/arch/rvm/stand/user/fpu_native_check.c'], program,
                            [opt, '-mfpu', '-fno-math-errno', '-frounding-math'])
            assembly = (builder.obj/'program-0-fpu_native_check.raw.s').read_text()
            for opcode in ('0x01c8', '0x01c9', '0x01cb', '0x01ca', '0x01cc', '0x01cd'):
                assert opcode in assembly, f'{opt}: missing native {opcode}'
            assert '__adddf3' not in assembly and '__muldf3' not in assembly, opt
            installs += ['--install', str(program), '/bin/'+name]
            commands += ['--command', 'run /bin/'+name, marker]
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
                shutil.copyfile(work/'console-keyboard.log',
                                obj/f'fpu-compiler-{args.ram_mib}.log')
    print(f'PASS: GCC native FPU at -O0/-O2/-Os ({args.ram_mib} MiB)')


if __name__ == '__main__':
    main()
