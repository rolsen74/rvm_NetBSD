#!/usr/bin/env python3
"""Exercise actual VMEmu memory/cache/TLB and entropy code on the host."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=Path('../rvm_VMEmu'))
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--stats', action='store_true', help='also compile VMEmu memory-statistics paths')
args = parser.parse_args()
source = args.source.resolve()
tests = Path(__file__).resolve().parent
flags = ['cc', '-O2', '-g', '-D_LINUX_', '-D_SDL2_', '-DHAVE_GENERIC',
         '-I', str(source), '-I', str(source/'os_linux'), '-I', str(source/'gui_SDL2')]
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
if args.stats:
    flags += ['-DSHOWMEMACCESSED']
with tempfile.TemporaryDirectory(prefix='rvm-platform-tests-') as directory:
    for name, extra in [
        ('vmemu_mmu', []),
        ('vmemu_entropy', ['-I', str(source/'plugin_Generic'),
                           str(source/'plugin_Generic/Generic_Entropy.c'),
                           str(source/'plugin_Generic/Generic_Read_Mem.c'),
                           str(source/'plugin_Generic/Generic_Write_Mem.c'),
                           '-Wl,--wrap=getrandom']),
    ]:
        binary = Path(directory)/name
        subprocess.run(flags + [str(tests/(name+'.c'))] + extra + ['-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
