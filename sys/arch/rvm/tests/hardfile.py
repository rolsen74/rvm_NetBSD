#!/usr/bin/env python3
"""Boot real MI disk I/O checks, or the FFS/FHR PID 1 integration fixture."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import tempfile
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def run_vm(vm, work, env, timeout, marker):
    process = subprocess.Popen(['stdbuf', '-oL', '-eL', vm, '-c', 'vm.config'],
        cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = bytearray()
    deadline = time.monotonic() + timeout
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            while time.monotonic() < deadline:
                if not selector.select(min(0.1, max(0, deadline-time.monotonic()))):
                    continue
                data = os.read(process.stdout.fileno(), 65536)
                if not data:
                    break
                output.extend(data)
                if marker.encode() in output or b'panic:' in output or b'fatal kernel exception' in output:
                    break
    finally:
        if process.poll() is None:
            process.kill()
        tail, _ = process.communicate()
        output.extend(tail)
    return output.decode(errors='replace')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, default=Path('obj.rvm-kernel'))
    parser.add_argument('--diskroot', action='store_true')
    parser.add_argument('--ram-mib', type=int, nargs='+', default=[64, 640])
    parser.add_argument('--timeout', type=float,
                        help='seconds per boot (default 30 for diskroot, 12 for raw I/O)')
    parser.add_argument('--vmemu', default='VMEmu')
    args = parser.parse_args()
    timeout = args.timeout if args.timeout is not None else (30 if args.diskroot else 12)
    obj = args.objdir.resolve()
    vm = shutil.which(args.vmemu)
    if vm is None:
        parser.error('VMEmu not found')
    logs = obj/'hardfile-tests'
    logs.mkdir(exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    for memory in args.ram_mib:
        for readonly in ([False] if args.diskroot else [False, True]):
            with tempfile.TemporaryDirectory(prefix='rvm-disk-boot-') as directory:
                work = Path(directory)
                for name in ('netbsd.fhr', 'bootloader.bin'):
                    shutil.copyfile(obj/name, work/name)
                disk = work/'disk.img'
                if args.diskroot:
                    shutil.copyfile(obj/'root-fixture.img', disk)
                    markers = (
                        'root on ld0a',
                        'PID 1 loaded from FHR on FFS; /dev/console opened',
                        'Welcome to NetBSD/RVM on a real FFS root filesystem.',
                        'disk init read /etc/motd successfully',
                        'malformed FHR executables rejected before replacing PID 1',
                        'GCC user executable, data relocations and execve verified',
                        'FHR shebang interpreter and argument forwarding verified',
                        'FHR BSS demand-zero and writable pages verified',
                        'FFS create/write/fsync/seek/read/unlink verified',
                        'disk fork/exec/wait integration verified; PID 1 sleeping',
                    )
                else:
                    with disk.open('wb') as stream:
                        stream.truncate(16 * 1024 * 1024)
                        stream.seek(128 * 512)
                        stream.write(bytes((i ^ 0x5a) & 255 for i in range(65536)))
                        stream.seek(-512, 2)
                        stream.write(b'RVMTEST1' + (1).to_bytes(8, 'big'))
                    ending = 'read-only policy' if readonly else 'restoration'
                    markers = ('root device:',
                        'RVM hardfile: MI disk read/write, 64 KiB DMA, flush, bounds and '
                        + ending + ' verified')
                before = digest(disk)
                config = (Path(__file__).resolve().parents[1]/'conf/vmemu.conf').read_text()
                config = re.sub(r'Size = \d+', f'Size = {memory * 16}', config, count=1)
                config += ('\n[HARDFILE]\n File = "disk.img"\n ReadOnly = '
                           + str(int(readonly)) + '\n End\n')
                (work/'vm.config').write_text(config)
                label = 'root' if args.diskroot else ('readonly' if readonly else 'readwrite')
                for boot in range(2 if args.diskroot else 1):
                    stop = markers[-1] if args.diskroot else 'root device:'
                    output = run_vm(vm, work, env, timeout, stop)
                    suffix = '-reboot' if boot else ''
                    (logs/f'{label}-{memory}{suffix}.log').write_text(output)
                    checks = markers
                    if boot:
                        checks += ('FFS persisted file verified after reboot',)
                    for marker in checks:
                        assert marker in output, 'Missing '+marker+'\n'+output[-8000:]
                    assert 'panic:' not in output and 'fatal kernel exception' not in output, output[-8000:]
                if not args.diskroot:
                    assert digest(disk) == before, 'Disk check did not restore the host image'
                print(f'PASS: {label} ({memory} MiB)', flush=True)


if __name__ == '__main__':
    main()
