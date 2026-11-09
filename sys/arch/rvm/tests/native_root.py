#!/usr/bin/env python3
"""Boot the minimal native image with the production kernel to a login prompt."""
import argparse
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, default=Path('obj.rvm-native-root'))
    parser.add_argument('--vmemu', default='VMEmu')
    parser.add_argument('--kernel-objdir', type=Path,
                        help='boot a different kernel/loader against a temporary copy of this image')
    parser.add_argument('--early', action='store_true',
                        help='require the EARLY idle/HALT and timed-CV regression instead of a production-only boot')
    parser.add_argument('--ram-mib', type=int, default=64)
    parser.add_argument('--timeout', type=float, default=90)
    parser.add_argument('--idle-seconds', type=float, default=0,
                        help='measure host CPU use after reaching the login prompt')
    parser.add_argument('--max-idle-cpu', type=float,
                        help='maximum CPU seconds per wall second during the idle sample')
    args = parser.parse_args()
    source = args.objdir.resolve()/'native-root'
    with tempfile.TemporaryDirectory(prefix='rvm-native-root-') as directory:
        boot = Path(directory)
        for name in ('root-native.img', 'netbsd.fhr', 'bootloader.bin'):
            origin = args.kernel_objdir.resolve() if args.kernel_objdir and name != 'root-native.img' else source
            shutil.copyfile(origin/name, boot/name)
        (boot/'vm.config').write_text(re.sub(r'Size = \d+', f'Size = {args.ram_mib * 16}',
                                            (source/'vm.config').read_text(), count=1))
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        process = subprocess.Popen(['stdbuf', '-oL', '-eL', shutil.which(args.vmemu), '-c', 'vm.config'],
            cwd=boot, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = bytearray()
        markers = ('NetBSD/RVM native root: writable FFS, native init and shell ready', 'login: ')
        if args.early:
            markers += ('RVM normal kernel: idle HALT and timer wakeup verified',)
        deadline = time.monotonic()+args.timeout
        log = source/f'boot-{args.ram_mib}.log'
        idle_start = idle_cpu = None
        idle_ratio = None
        def cpu_seconds():
            # /proc process accounting includes the emulator's CPU, GUI and timer threads.
            fields = Path(f'/proc/{process.pid}/stat').read_text().rsplit(')', 1)[1].split()
            return (int(fields[11])+int(fields[12]))/os.sysconf('SC_CLK_TCK')
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                while time.monotonic() < deadline:
                    if selector.select(0.1):
                        data = os.read(process.stdout.fileno(), 65536)
                        if not data:
                            break
                        output.extend(data)
                        log.write_bytes(output)
                    transcript = output.decode(errors='replace').replace('\r', '')
                    if all(marker in transcript for marker in markers):
                        if args.idle_seconds <= 0:
                            break
                        if idle_start is None:
                            idle_cpu = cpu_seconds()
                            idle_start = time.monotonic()
                        elapsed = time.monotonic()-idle_start
                        if elapsed >= args.idle_seconds:
                            idle_ratio = (cpu_seconds()-idle_cpu)/elapsed
                            break
                    if 'panic:' in transcript:
                        break
        finally:
            if process.poll() is None:
                process.kill()
            rest, _ = process.communicate()
            output.extend(rest)
        log.write_bytes(output)
        transcript = output.decode(errors='replace').replace('\r', '')
        assert all(marker in transcript for marker in markers), transcript[-10000:]
        assert 'panic:' not in transcript and 'fatal kernel exception' not in transcript
        if not args.early:
            assert 'Hello from a NetBSD/RVM user process' not in transcript
        if args.idle_seconds > 0:
            assert idle_ratio is not None, 'Idle sample did not complete: '+transcript[-3000:]
            print(f'Idle host CPU: {idle_ratio*100:.1f}% of one core over {elapsed:.1f}s', flush=True)
            if args.max_idle_cpu is not None:
                assert idle_ratio <= args.max_idle_cpu, f'Idle CPU ratio {idle_ratio:.3f} exceeds {args.max_idle_cpu:.3f}'
    kernel = 'EARLY kernel idle/HALT checks' if args.early else 'production kernel'
    print(f'PASS: {kernel} and minimal native init/rc/getty root ({args.ram_mib} MiB)')


if __name__ == '__main__':
    main()
