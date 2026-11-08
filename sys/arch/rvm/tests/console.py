#!/usr/bin/env python3
"""Verify SDL keyboard -> GeneralDriver -> tty -> disk-loaded C monitor."""
import argparse
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, default=Path('obj.rvm-diskroot'))
    parser.add_argument('--vmemu', default='VMEmu')
    parser.add_argument('--ram-mib', type=int, default=64)
    parser.add_argument('--pixels', action='store_true',
                        help='compare rendered pixels before typing and after tty erase')
    parser.add_argument('--keypad', action='store_true',
                        help='type numbers, operators and Enter using actual SDL keypad keycodes')
    parser.add_argument('--shell', action='store_true',
                        help='exercise native interactive sh, job control and writable-root redirection')
    parser.add_argument('--command', nargs=2, action='append', default=[],
                        metavar=('COMMAND', 'EXPECTED_OUTPUT'))
    args = parser.parse_args()
    obj = args.objdir.resolve()
    vm = shutil.which(args.vmemu)
    assert vm is not None, 'VMEmu not found'
    vm = str(Path(vm).resolve())
    with tempfile.TemporaryDirectory(prefix='rvm-console-') as directory:
        work = Path(directory)
        injector = work/'keyboard.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2',
                        str(Path(__file__).with_name('keyboard_events.c')),
                        '-ldl', '-lSDL2', '-o', str(injector)], check=True)
        for source, target in [('netbsd.fhr','netbsd.fhr'), ('bootloader.bin','bootloader.bin'),
                               ('root-fixture.img','root-fixture.img')]:
            shutil.copyfile(obj/source, work/target)
        config = (obj/'vm.diskroot.config').read_text()
        import re
        config = re.sub(r'Size = \d+', f'Size = {args.ram_mib * 16}', config, count=1)
        (work/'vm.config').write_text(config)
        fifo = work/'keys'
        os.mkfifo(fifo)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   LD_PRELOAD=str(injector), RVM_TEST_KEYFILE=str(fifo),
                   RVM_TEST_CAPTURE=str(work/'capture.request'))
        if args.keypad:
            env['RVM_TEST_KEYPAD'] = '1'
        process = subprocess.Popen(['stdbuf','-oL','-eL',vm,'-c','vm.config'], cwd=work,
                                   env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = bytearray()
        commands = [
            (b'echo 0123456789/*-+.=,\n', '\n0123456789/*-+.=,\n'),
            (b'echo AbC XYz\n', '\nAbC XYz\n'),
            (b'echo keyboard-oX\bk\n', '\nkeyboard-ok\n'),
            (b'run /bin/rvm-echo from-exec\n', 'status=0\n'),
            (b'env\n', 'PATH=/bin:/usr/bin\n'),
            (b'set RVM_DEMO Ready\n', 'environment updated\n'),
            (b'env\n', 'RVM_DEMO=Ready\n'),
            (b'unset RVM_DEMO\n', 'environment updated\n'),
            (b'run sync\n', 'status=0\n'),
            (b'run /usr/bin/yes signal-check\n', '\nsignal-check\n'),
            (b'\x03', 'status=2\n'),
            (b'cat /etc/motd\n', 'Welcome to NetBSD/RVM on a real FFS root filesystem.\n'),
            (b'pid\n', 'monitor pid=1\n'),
            (b'run /missing-program\n', 'status=32512\n'),
            (b'pid\n', 'monitor pid=1\n'),
        ]
        commands += [(command.encode() + b'\n', expected + '\n')
                     for command, expected in args.command]
        commands = [(command, expected, 'rvm# ') for command, expected in commands]
        # yes has no prompt while running; Ctrl-C is sent after its first line.
        commands = [(command, expected, None if command.startswith(b'run /usr/bin/yes ') else prompt)
                    for command, expected, prompt in commands]
        if args.shell:
            commands += [
                (b'sh\n', '\n# ', '# '),
                (b'echo "interactive:$((6 * 7))"\n', '\ninteractive:42\n', '# '),
                (b'echo shell-disk > /tmp/interactive; cat /tmp/interactive | cat\n', '\nshell-disk\n', '# '),
                (b'/usr/bin/yes shell-job\n', '\nshell-job\n', None),
                (b'\x1a', 'Suspended', '# '),
                (b'jobs\n', 'Suspended', '# '),
                (b'fg\n', '\nshell-job\n', None),
                (b'\x03', '\n# ', '# '),
                (b'echo "signal:$?"\n', '\nsignal:130\n', '# '),
                (b'exit 0\n', 'status=0\n', 'rvm# '),
                (b'cat /tmp/interactive\n', '\nshell-disk\n', 'rvm# '),
                (b'pid\n', 'monitor pid=1\n', 'rvm# '),
            ]
        step = -1
        offset = 0
        pixel_stage = 0 if args.pixels else 6
        pixel_offset = 0
        def send_keys(data):
            with fifo.open('wb', buffering=0) as keys:
                keys.write(data)
        def capture(name):
            (work/'capture.request').write_text(str(work/(name+'.ppm'))+'\n')
        deadline = time.monotonic() + (90 if args.shell else 60)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                while time.monotonic() < deadline:
                    if selector.select(0.1):
                        data = os.read(process.stdout.fileno(), 65536)
                        if not data: break
                        output.extend(data)
                    transcript = output.decode(errors='replace').replace('\r', '')
                    if pixel_stage < 6:
                        if pixel_stage == 0 and 'rvm# ' in transcript:
                            capture('baseline'); pixel_stage = 1
                        elif pixel_stage == 1 and (work/'baseline.ppm').exists():
                            pixel_offset = len(output); send_keys(b'X'); pixel_stage = 2
                        elif pixel_stage == 2 and b'X' in output[pixel_offset:]:
                            capture('typed'); pixel_stage = 3
                        elif pixel_stage == 3 and (work/'typed.ppm').exists():
                            pixel_offset = len(output); send_keys(b'\b'); pixel_stage = 4
                        elif pixel_stage == 4 and b'\b \b' in output[pixel_offset:]:
                            capture('erased'); pixel_stage = 5
                        elif pixel_stage == 5 and (work/'erased.ppm').exists():
                            for name in ('baseline', 'typed', 'erased'):
                                shutil.copyfile(work/(name+'.ppm'), obj/('console-'+name+'.ppm'))
                            assert (work/'baseline.ppm').read_bytes() != (work/'typed.ppm').read_bytes(), 'Typed glyph did not render'
                            assert (work/'baseline.ppm').read_bytes() == (work/'erased.ppm').read_bytes(), 'Backspace did not restore the original pixels'
                            pixel_stage = 6
                        continue
                    ready = step == -1 and 'rvm# ' in transcript
                    if step >= 0:
                        response = transcript.find(commands[step][1], offset)
                        prompt = commands[step][2]
                        ready = response >= 0 and (prompt is None or prompt in transcript[response:])
                    if ready:
                        step += 1
                        if step == len(commands): break
                        offset = len(transcript)
                        send_keys(commands[step][0])
                    if b'panic:' in output or b'fatal kernel exception' in output: break
        finally:
            if process.poll() is None: process.kill()
            rest, _ = process.communicate()
            output.extend(rest)
        text = output.decode(errors='replace').replace('\r', '')
        (obj/'console-keyboard.log').write_text(text)
        assert pixel_stage == 6, f'Pixel test did not finish (stage {pixel_stage})'
        assert step == len(commands), f'Command sequence stopped at {step}: '+text[-6000:]
        for marker in ('\nAbC XYz\n', '\nkeyboard-ok\n', '\nfrom-exec\n', 'status=0',
                       'exec failed', 'status=32512'):
            assert marker in text, 'Missing '+repr(marker)+'\n'+text[-6000:]
        assert text.count('monitor pid=1') >= 2, text[-6000:]
        assert text.count('Welcome to NetBSD/RVM on a real FFS root filesystem.') >= 2
        assert 'panic:' not in text and 'fatal kernel exception' not in text
        assert 'console controlling tty setup failed' not in text, text[-6000:]
        assert 'environment setup failed' not in text, text[-6000:]
        if args.shell:
            assert 'root remount failed' not in text and 'root read-only remount failed' not in text, text[-6000:]
        print(f'PASS: real SDL keyboard, tty editing, monitor file I/O and child exec ({args.ram_mib} MiB)')


if __name__ == '__main__':
    main()
