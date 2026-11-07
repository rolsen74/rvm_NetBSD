#!/usr/bin/env python3
"""Verify normal MI startup, integration checks, and the root-device prompt."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--objdir', type=Path, default=Path('obj.rvm-kernel'))
parser.add_argument('--image', type=Path)
parser.add_argument('--ram-mib', type=int, nargs='+', default=[64, 640])
parser.add_argument('--timeout', type=float, default=10)
parser.add_argument('--vmemu', default='VMEmu')
args = parser.parse_args()
obj = args.objdir.resolve()
image = (args.image or obj/'netbsd.fhr').resolve()
logs = obj/'boot-tests'
logs.mkdir(exist_ok=True)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
required = (
    'NetBSD main(): console initialized',
    'cpu0 at mainbus0: 64-bit RVM CPU',
    'RVM normal kernel: heap, kcopy, hardclock, softclock and CV wakeup verified',
    'RVM normal kernel: four-byte-aligned 64-bit atomic operations verified',
    'RVM normal kernel: 65536-item pool bitmap verified',
    'RVM normal kernel: per-LWP user stack pointer switching verified',
    'RVM normal kernel: user pmaps, demand-zero, copies, protection and COW verified',
    'RVM normal kernel: user-mode execution, user stack and syscall/RTE roundtrip verified',
    'RVM normal kernel: user kernel isolation, read-only text, NX and privilege faults verified',
    'RVM normal kernel: wired user I/O buffers and partial mapping rollback verified',
    'RVM normal kernel: integer contexts, ptrace registers and privileged-state rejection verified',
    'root device:',
)
for memory in args.ram_mib:
    if memory < 16:
        parser.error('normal-kernel tests need at least 16 MiB')
    with tempfile.TemporaryDirectory(prefix='rvm-normal-boot-') as directory:
        work = Path(directory)
        shutil.copyfile(image, work/'netbsd.fhr')
        shutil.copyfile(obj/'bootloader.bin', work/'bootloader.bin')
        config = (obj/'vm.config').read_text()
        (work/'vm.config').write_text(re.sub(r'Size = \d+',
            'Size = '+str(memory * 16), config, count=1))
        try:
            result = subprocess.run([args.vmemu, '-c', 'vm.config'], cwd=work,
                                    env=env, capture_output=True, timeout=args.timeout)
            output = result.stdout.decode(errors='replace')
            stderr = result.stderr.decode(errors='replace')
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'').decode(errors='replace')
            stderr = (error.stderr or b'').decode(errors='replace')
        output = re.sub(r'VMEmu display: SDL video driver [^\r\n]*\r?\n', '', output)
        (logs/('ram-'+str(memory)+'.log')).write_text(output+'\n'+stderr)
        for marker in required:
            assert marker in output, 'Missing '+marker+'\n'+output[-10000:]
        assert 'panic:' not in output and 'fatal kernel exception' not in output, output[-10000:]
        print(f'PASS: normal MI kernel, user VM/mode/I/O checks and root prompt ({memory} MiB)',
              flush=True)
