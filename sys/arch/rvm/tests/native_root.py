#!/usr/bin/env python3
"""Boot a disposable copy of the native image and optionally exercise login."""
import argparse
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import tempfile
import time
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'stand'))
from fhr_pack import unpack as unpack_fhr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, default=Path('obj.rvm-native-root'))
    parser.add_argument('--vmemu', default='VMEmu')
    parser.add_argument('--kernel-objdir', type=Path,
                        help='boot a different kernel/loader against a temporary copy of this image')
    parser.add_argument('--early', action='store_true',
                        help='require the EARLY idle/HALT and timed-CV regression instead of a production-only boot')
    parser.add_argument('--ram-mib', type=int, default=64)
    parser.add_argument('--no-network', action='store_true',
                        help='omit TAP networking from the disposable test VM')
    parser.add_argument('--timeout', type=float,
                        help='total seconds (default: 300 with login, otherwise 90)')
    parser.add_argument('--idle-seconds', type=float, default=0,
                        help='measure host CPU use after reaching the login prompt')
    parser.add_argument('--max-idle-cpu', type=float,
                        help='maximum CPU seconds per wall second during the idle sample')
    parser.add_argument('--login', action='store_true',
                        help='authenticate root and run a shell command on the copied image')
    parser.add_argument('--login-count', type=int, default=2,
                        help='successful logins on the same copied disk (default: 2)')
    parser.add_argument('--history-keys', action='store_true',
                        help='check Up arrow recalls and executes the prior shell command')
    parser.add_argument('--source-root', type=Path,
                        help='use a separately packaged native-root directory')
    parser.add_argument('--guest-command',
                        help='run one additional command after the first login')
    parser.add_argument('--guest-marker',
                        help='require this output from --guest-command')
    parser.add_argument('--extended-utils', action='store_true',
                        help='exercise the extended native-root utility set after login')
    parser.add_argument('--system-utils', action='store_true',
                        help='exercise only the newly ported system-inspection utilities')
    parser.add_argument('--disk-utils', action='store_true',
                        help='exercise newly ported disk administration utilities')
    parser.add_argument('--swap-utils', action='store_true',
                        help='exercise the native ld0b swap partition')
    parser.add_argument('--pager-utils', action='store_true',
                        help='exercise newly ported pager and visual encoder')
    parser.add_argument('--encoding-utils', action='store_true',
                        help='exercise newly ported encoding and ident utilities')
    parser.add_argument('--editor-utils', action='store_true',
                        help='exercise the newly ported vi/ex editor')
    parser.add_argument('--compression-utils', action='store_true',
                        help='exercise the newly ported gzip and bzip2 utilities')
    parser.add_argument('--terminal-utils', action='store_true',
                        help='exercise the newly ported script PTY recorder')
    parser.add_argument('--math-utils', action='store_true',
                        help='exercise the newly ported bc calculator')
    parser.add_argument('--top-utils', action='store_true',
                        help='exercise the newly ported top process display')
    parser.add_argument('--archive-utils', action='store_true',
                        help='exercise pax tar and cpio command modes')
    parser.add_argument('--file-utils', action='store_true',
                        help='exercise the newly ported file type detector')
    parser.add_argument('--five-utils', action='store_true',
                        help='exercise unvis, getopt, lam, rs, and shuffle')
    parser.add_argument('--shuffle-utils', action='store_true',
                        help='exercise shuffle alone')
    parser.add_argument('--next-five-utils', action='store_true',
                        help='exercise timeout, mkfifo, pwait, printenv, and progress')
    parser.add_argument('--port25-utils', action='store_true',
                        help='exercise the next 25 packaged userland programs')
    parser.add_argument('--port25-extra', action='store_true',
                        help='exercise the remaining port25 program behaviors')
    args = parser.parse_args()
    if args.timeout is None:
        args.timeout = 300 if args.login else 90
    if args.login_count < 1:
        parser.error('--login-count must be positive')
    if args.history_keys and not args.login:
        parser.error('--history-keys requires --login')
    source = args.source_root.resolve() if args.source_root else args.objdir.resolve()/'native-root'
    with tempfile.TemporaryDirectory(prefix='rvm-native-root-') as directory:
        boot = Path(directory)
        for name in ('root-native.img', 'netbsd.fhr', 'bootloader.bin'):
            origin = args.kernel_objdir.resolve() if args.kernel_objdir and name != 'root-native.img' else source
            shutil.copyfile(origin/name, boot/name)
        kernel_image = boot/'netbsd.fhr'
        if kernel_image.read_bytes()[7] == 1:
            kernel_image.write_bytes(unpack_fhr(kernel_image.read_bytes()))
        config = re.sub(r'Size = \d+', f'Size = {args.ram_mib * 16}',
                        (source/'vm.config').read_text(), count=1)
        if args.no_network:
            config = re.sub(r'\n\[NETWORK\]\n.*?\n End\n', '\n', config,
                            flags=re.DOTALL)
        (boot/'vm.config').write_text(config)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        if args.login:
            injector = boot/'keyboard.so'
            subprocess.run(['cc', '-shared', '-fPIC', '-O2',
                            str(Path(__file__).with_name('keyboard_events.c')),
                            '-ldl', '-lSDL2', '-o', str(injector)], check=True)
            fifo = boot/'keys'
            os.mkfifo(fifo)
            env.update(LD_PRELOAD=str(injector), RVM_TEST_KEYFILE=str(fifo))
        process = subprocess.Popen(['stdbuf', '-oL', '-eL', shutil.which(args.vmemu), '-c', 'vm.config'],
            cwd=boot, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = bytearray()
        markers = ('NetBSD/RVM native root: writable FFS, native init and shell ready', 'login: ')
        if args.early:
            markers += ('RVM normal kernel: idle HALT and timer wakeup verified',)
        started = time.monotonic()
        deadline = started+args.timeout
        suffix = '-'+args.kernel_objdir.resolve().name if args.kernel_objdir else ''
        log = source/f'{"login" if args.login else "boot"}-{args.ram_mib}{suffix}.log'
        idle_start = idle_cpu = None
        idle_ratio = None
        steps = []
        if args.login:
            password = os.environ.get('RVM_TEST_PASSWORD', 'password').encode()+b'\n'
            for count in range(1, args.login_count+1):
                utility_checks = []
                if args.extended_utils and count == 1:
                    # Keep each line below the console tty's canonical input limit.
                    utility_checks = [
                        "cd /tmp && /usr/bin/printf 'alpha\\nbeta\\n' > port-utils && "
                        "/bin/chmod 600 port-utils && /bin/ln port-utils port-hard && "
                        "/bin/ln -s port-utils port-sym && /bin/kill -0 $$ && "
                        "/bin/test \"$(/usr/bin/basename /tmp/port-utils)\" = port-utils && "
                        "/bin/test \"$(/usr/bin/dirname /tmp/port-utils)\" = /tmp && "
                        "echo 'RVM extended utilities group 1 verified'\n",
                        "/bin/test \"$(/usr/bin/head -n 1 port-utils)\" = alpha && "
                        "/bin/test \"$(/usr/bin/tail -n 1 port-utils)\" = beta && "
                        "/bin/test \"$(/usr/bin/wc -l < port-utils)\" -eq 2 && "
                        "/usr/bin/touch stamp && /bin/test -f stamp && /bin/date >/dev/null && "
                        "/bin/hostname >/dev/null && /usr/bin/uname -a >/dev/null && "
                        "/usr/bin/env RVM_PORT=ok /bin/sh -c "
                        "'/bin/test \"$RVM_PORT\" = ok' && "
                        "echo 'RVM extended utilities group 2 verified'\n",
                        "/usr/bin/grep -q beta port-utils && "
                        "/bin/test \"$(/usr/bin/sed -n '2p' port-utils)\" = beta && "
                        "/bin/test \"$(/usr/bin/printf 'abc\\n' | /usr/bin/cut -c1-2)\" = ab && "
                        "/bin/test \"$(/usr/bin/printf 'abc\\n' | /usr/bin/tr a-z A-Z)\" = ABC && "
                        "echo 'RVM extended utilities group 3 verified'\n",
                        "/usr/bin/printf 'b\\na\\n' > sort-input && "
                        "/bin/test \"$(/usr/bin/sort sort-input | "
                        "/usr/bin/head -n 1)\" = a && "
                        "/bin/test \"$(/usr/bin/printf 'a\\na\\n' | /usr/bin/uniq | "
                        "/usr/bin/wc -l)\" -eq 1 && "
                        "/bin/test \"$(/usr/bin/printf 'ok\\n' | /usr/bin/xargs "
                        "/usr/bin/printf '%s')\" = ok && "
                        "echo 'RVM extended utilities group 4 verified'\n",
                        "/bin/mkdir port-tree && /bin/cp port-utils port-tree/file && "
                        "/usr/bin/find port-tree -type f -name file | /usr/bin/grep -q port-tree/file && "
                        "/usr/bin/cmp port-utils port-tree/file && /usr/bin/cksum port-utils >/dev/null && "
                        "/usr/bin/du -s port-tree >/dev/null && /bin/df / >/dev/null && "
                        "echo 'RVM extended utilities group 5 verified'\n",
                        "/bin/test \"$(/bin/expr 2 + 3)\" -eq 5 && "
                        "/usr/bin/printf 'tee-data\\n' | /usr/bin/tee tee-file >/dev/null && "
                        "/bin/test \"$(/usr/bin/stat -f '%z' tee-file)\" -eq 9 && "
                        "/bin/test \"$(/usr/bin/jot 3 1 | /usr/bin/tail -n 1)\" -eq 3 && "
                        "/bin/test \"$(/usr/bin/seq 1 3 | /usr/bin/tail -n 1)\" -eq 3 && "
                        "echo 'RVM extended utilities group 6 verified'\n",
                        "/usr/bin/printf 'a\\nb\\n' > set-a && "
                        "/usr/bin/printf 'b\\nc\\n' > set-b && "
                        "/bin/test \"$(/usr/bin/comm -12 set-a set-b)\" = b && "
                        "/bin/test \"$(/usr/bin/printf 'abcd\\n' | /usr/bin/fold -w 2 | "
                        "/usr/bin/wc -l)\" -eq 2 && "
                        "/bin/test \"$(/usr/bin/join set-a set-a | /usr/bin/head -n 1)\" = 'a a' && "
                        "/bin/test \"$(/usr/bin/paste -d: set-a set-a | "
                        "/usr/bin/head -n 1)\" = a:a && "
                        "echo 'RVM extended utilities group 7 verified'\n",
                        "temp=$(/usr/bin/mktemp /tmp/rvm.XXXXXX) && /bin/test -f \"$temp\" && "
                        "/bin/test \"$(/usr/bin/realpath \"$temp\")\" = \"$temp\" && "
                        "/usr/bin/hexdump -n 1 \"$temp\" >/dev/null && "
                        "/usr/bin/printf 'a b\\nb c\\n' > graph && "
                        "/bin/test \"$(/usr/bin/tsort graph | /usr/bin/head -n 1)\" = a && "
                        "echo 'RVM extended utilities group 8 verified'\n",
                        "/bin/test \"$(/usr/bin/awk 'END { print NR }' set-a)\" -eq 2 && "
                        "/bin/test \"$(/usr/bin/printf abc | /usr/bin/rev)\" = cba && "
                        "/usr/bin/printf 'a b\\n' | /usr/bin/column -t | /usr/bin/grep -q a && "
                        "/usr/bin/split -l 1 set-a piece && /bin/test -f pieceaa && "
                        "/bin/test -f pieceab && /usr/bin/nl -ba set-a | /usr/bin/tail -n 1 | "
                        "/usr/bin/grep -q b && /usr/bin/pr -t set-a | /usr/bin/cmp - set-a && "
                        "/usr/bin/cal 11 2026 | /usr/bin/grep -q 2026 && "
                        "echo 'RVM extended utilities group 9 verified'\n",
                        "/usr/bin/printf 'old\\n' > patch-target && "
                        "/usr/bin/printf '%s\\n' '--- patch-target' '+++ patch-target' "
                        "'@@ -1 +1 @@' '-old' '+new' > change.diff && "
                        "/usr/bin/patch < change.diff >/dev/null && "
                        "/bin/test \"$(/bin/cat patch-target)\" = new && "
                        "echo 'RVM extended utilities group 10 verified'\n",
                        "/usr/bin/diff set-a set-a && /usr/bin/diff3 set-a set-a set-a && "
                        "/bin/test \"$(/usr/bin/printf foo | /usr/bin/m4 -Dfoo=bar)\" = bar && "
                        "/usr/bin/printf 'a\\nedited\\n.\\nw\\nq\\n' | /bin/ed edit-file >/dev/null && "
                        "/bin/test \"$(/bin/cat edit-file)\" = edited && "
                        "/usr/bin/time /bin/test -f edit-file 2>/dev/null && /usr/bin/tty >/dev/null && "
                        "echo 'RVM extended utilities group 11 verified'\n",
                        "/usr/bin/printf 'one two three four\\n' | /usr/bin/fmt -w 7 > wrapped && "
                        "/bin/test \"$(/usr/bin/wc -l < wrapped)\" -gt 1 && "
                        "/usr/bin/compress -c port-utils > port.Z && "
                        "/usr/bin/compress -cd port.Z | /usr/bin/cmp - port-utils && "
                        "echo 'RVM extended utilities group 12 verified'\n",
                        "/usr/bin/printf 'all:\\n\\t@echo made > make-result\\n' > Makefile && "
                        "/usr/bin/make -r -f Makefile >/dev/null && "
                        "/bin/test \"$(/bin/cat make-result)\" = made && "
                        "/bin/mkdir archive-in archive-out && /bin/cp port-utils archive-in/file && "
                        "(cd archive-in && /bin/pax -w -f ../archive.pax file) && "
                        "(cd archive-out && /bin/pax -r -f ../archive.pax) && "
                        "/usr/bin/cmp port-utils archive-out/file && "
                        "echo 'RVM extended utilities group 13 verified'\n",
                        "/bin/dd if=port-utils of=dd-copy bs=1 2>/dev/null && "
                        "/usr/bin/cmp port-utils dd-copy && /bin/mkdir empty-dir && "
                        "/bin/rmdir empty-dir && /bin/domainname >/dev/null && "
                        "/bin/test \"$(/usr/bin/getconf LONG_BIT)\" -eq 64 && "
                        "/usr/bin/nice -n 1 /bin/test -f dd-copy && "
                        "/usr/bin/nohup /bin/sh -c 'echo nohup-ok' > nohup-out 2>/dev/null && "
                        "/bin/test \"$(/bin/cat nohup-out)\" = nohup-ok && "
                        "echo 'RVM extended utilities group 14 verified'\n",
                        "/usr/bin/printf 'a\\tb\\n' > tabs && /usr/bin/expand tabs > spaces && "
                        "/usr/bin/unexpand -a spaces > tabs-back && "
                        "/usr/bin/printf 'a\\bA\\n' | /usr/bin/col -b | /usr/bin/grep -q A && "
                        "/usr/bin/chflags nodump dd-copy && /usr/bin/chflags none dd-copy && "
                        "echo 'RVM extended utilities group 15 verified'\n",
                        "/sbin/chown root:wheel dd-copy && /bin/chgrp wheel dd-copy && "
                        "/usr/bin/mesg y && /usr/bin/renice 1 -p $$ >/dev/null && "
                        "/usr/bin/whereis sh | /usr/bin/grep -q /bin/sh && "
                        "/usr/bin/printf 'alpha\\nbeta\\n' > words && "
                        "/bin/test \"$(/usr/bin/look beta words)\" = beta && "
                        "/usr/bin/printf 'abcdef\\n' | /usr/bin/colrm 2 5 | /usr/bin/grep -q af && "
                        "echo 'RVM extended utilities group 16 verified'\n",
                        "/usr/bin/banner RVM | /usr/bin/grep -q R && "
                        "/usr/bin/who >/dev/null && /usr/bin/users >/dev/null && "
                        "echo 'RVM extended native utilities verified'\n",
                    ]
                if args.system_utils and count == 1:
                    utility_checks += [
                        "/bin/ps -ax | /usr/bin/grep -q init && "
                        "/bin/test \"$(/sbin/sysctl -n kern.ostype)\" = NetBSD && "
                        "/sbin/dmesg | /usr/bin/grep -q NetBSD && "
                        "echo 'RVM system utilities group 1 verified'\n",
                        "/usr/bin/uptime >/dev/null && /usr/bin/pgrep init >/dev/null && "
                        "/usr/bin/vmstat -s >/dev/null && "
                        "/usr/bin/netstat -rn >/dev/null && /sbin/ifconfig -a >/dev/null && "
                        "/sbin/route -n show >/dev/null && "
                        "/bin/test -c /dev/mem && /bin/test -c /dev/kmem && "
                        "/bin/dd if=/dev/mem of=/dev/null bs=1 skip=2097152 count=1 2>/dev/null && "
                        "/bin/dd if=/dev/kmem of=/dev/null bs=1 skip=2097152 count=1 2>/dev/null && "
                        "/usr/bin/fstat && "
                        "echo 'RVM system utilities verified'\n",
                    ]
                if args.disk_utils and count == 1:
                    utility_checks += [
                        "/sbin/swapctl -l | /usr/bin/grep -q ld0b && "
                        "/sbin/disklabel /dev/rld0c | /usr/bin/grep -q 'total sectors:' && "
                        "echo 'RVM disk utilities verified'\n",
                        "/sbin/newfs -N /dev/rld0a >/dev/null && "
                        "/sbin/tunefs -N /dev/rld0a >/dev/null && "
                        "echo 'RVM filesystem utilities verified'\n",
                    ]
                if args.swap_utils and count == 1:
                    utility_checks += [
                        "/bin/test -b /dev/ld0b && /bin/test -c /dev/rld0b && "
                        "/sbin/disklabel /dev/rld0c | /usr/bin/grep -q 'swap' && "
                        "/sbin/swapctl -l | /usr/bin/grep -q ld0b && "
                        "/sbin/swapctl -s | /usr/bin/grep -q 'total:' && "
                        "echo 'RVM swap device verified'\n",
                    ]
                if args.pager_utils and count == 1:
                    utility_checks += [
                        "/usr/bin/less -F /etc/motd | /usr/bin/grep -q Welcome && "
                        "/bin/test \"$(/usr/bin/printf abc | /usr/bin/vis)\" = abc && "
                        "echo 'RVM pager utilities verified'\n",
                    ]
                if args.encoding_utils and count == 1:
                    utility_checks += [
                        "cd /tmp && /bin/test \"$(/usr/bin/printf rvm | /usr/bin/base64 | "
                        "/usr/bin/base64 -d)\" = rvm && "
                        "/usr/bin/printf data | /usr/bin/uuencode decoded > encoded && "
                        "/usr/bin/uudecode encoded && /bin/test \"$(/bin/cat decoded)\" = data && "
                        "echo 'RVM encoders verified'\n",
                        "/usr/bin/printf '@(#)rvm-tag\\n' > tag && "
                        "/usr/bin/what tag | /usr/bin/grep -q rvm-tag && "
                        "echo 'RVM identifier utility verified'\n",
                    ]
                if args.editor_utils and count == 1:
                    utility_checks += [
                        "cd /tmp && /usr/bin/printf 'a\\nhello from vi\\n.\\nwq\\n' | "
                        "/usr/bin/ex -s vi-test && "
                        "/bin/test \"$(/bin/cat vi-test)\" = 'hello from vi' && "
                        "echo 'RVM editor verified'\n",
                    ]
                if args.compression_utils and count == 1:
                    utility_checks += [
                        "/usr/bin/gzip -c /etc/motd | /usr/bin/gunzip -c | "
                        "/usr/bin/cmp - /etc/motd && "
                        "/usr/bin/bzip2 -c /etc/motd | /usr/bin/bunzip2 -c | "
                        "/usr/bin/cmp - /etc/motd && "
                        "echo 'RVM compression utilities verified'\n",
                    ]
                if args.terminal_utils and count == 1:
                    utility_checks += [
                        "cd /tmp && /usr/bin/script -q -c '/bin/echo RVM-script-test' "
                        "typescript && /usr/bin/grep -q RVM-script-test typescript && "
                        "echo 'RVM terminal utility verified'\n",
                    ]
                if args.math_utils and count == 1:
                    utility_checks += [
                        "/bin/test \"$(/usr/bin/printf '2^10\\n' | /usr/bin/bc)\" = 1024 && "
                        "/bin/test \"$(/usr/bin/printf 'scale=4; 1/8\\n' | "
                        "/usr/bin/bc)\" = .1250 && "
                        "echo 'RVM math utility verified'\n",
                    ]
                if args.top_utils and count == 1:
                    utility_checks += [
                        "/usr/bin/top -b -d 1 > /tmp/top-output && "
                        "/usr/bin/grep -q 'load averages:' /tmp/top-output && "
                        "echo 'RVM top utility verified'\n",
                    ]
                if args.archive_utils and count == 1:
                    utility_checks += [
                        "cd /tmp && /bin/mkdir archive-src archive-tar archive-cpio && "
                        "/usr/bin/printf 'archive-data\\n' > archive-src/file && "
                        "/bin/tar -cf archive.tar -C archive-src file && "
                        "(cd archive-tar && /bin/tar -xf ../archive.tar) && "
                        "/usr/bin/cmp archive-src/file archive-tar/file && "
                        "echo 'RVM tar mode verified'\n",
                        "cd /tmp/archive-src && /usr/bin/printf 'file\\n' | "
                        "/bin/cpio -o > ../archive.cpio && "
                        "(cd ../archive-cpio && /bin/cpio -i < ../archive.cpio) && "
                        "/usr/bin/cmp file ../archive-cpio/file && "
                        "echo 'RVM cpio mode verified'\n",
                    ]
                if args.file_utils and count == 1:
                    utility_checks += [
                        "/usr/bin/file /etc/motd | /usr/bin/grep -q text && "
                        "/usr/bin/file /usr/bin/file | "
                        "/usr/bin/grep -q 'RVM FHR executable' && "
                        "echo 'RVM file utility verified'\n",
                    ]
                if args.five_utils and count == 1:
                    utility_checks += [
                        "cd /tmp && /usr/bin/printf 'a b\\n' > five-vis && "
                        "/usr/bin/vis -w five-vis | /usr/bin/unvis | "
                        "/usr/bin/cmp - five-vis && echo 'RVM unvis verified'\n",
                        "/bin/test \"$(/usr/bin/getopt 'a:b' -a value -b tail)\" = "
                        "' -a value -b -- tail' && echo 'RVM getopt verified'\n",
                        "cd /tmp && /usr/bin/printf 'left\\n' > five-left && "
                        "/usr/bin/printf 'right\\n' > five-right && "
                        "/bin/test \"$(/usr/bin/lam -s : five-left -s : five-right)\" = "
                        "':left:right' && echo 'RVM lam verified'\n",
                        "/bin/test \"$(/usr/bin/printf 'a b\\nc d\\n' | /usr/bin/rs -h)\" = "
                        "'2 2' && echo 'RVM rs verified'\n",
                    ]
                if (args.five_utils or args.shuffle_utils) and count == 1:
                    utility_checks += [
                        "cd /tmp && /usr/bin/printf 'alpha\\nbeta\\ngamma\\n' > five-list && "
                        "/usr/bin/shuffle -f five-list > five-shuffled && "
                        "/usr/bin/sort five-shuffled | "
                        "/usr/bin/cmp - five-list && echo 'RVM shuffle verified'\n",
                    ]
                if args.next_five_utils and count == 1:
                    utility_checks += [
                        "/usr/bin/timeout 1 /bin/sleep 5 >/dev/null 2>&1; "
                        "code=$?; /bin/test \"$code\" -eq 124 && "
                        "echo 'RVM timeout verified'\n",
                        "cd /tmp && /usr/bin/mkfifo five-fifo && /bin/test -p five-fifo && "
                        "( /usr/bin/printf fifo > five-fifo & "
                        "/bin/test \"$(/bin/cat five-fifo)\" = fifo && wait ) && "
                        "echo 'RVM mkfifo verified'\n",
                        "/bin/sleep 2 & child=$!; /usr/bin/pwait -v -t 10 \"$child\" | "
                        "/usr/bin/grep -q 'exited with status 0' && wait \"$child\" && "
                        "echo 'RVM pwait verified'\n",
                        "/bin/test \"$(/usr/bin/env RVM_FIVE=ok "
                        "/usr/bin/printenv RVM_FIVE)\" = ok && "
                        "echo 'RVM printenv verified'\n",
                        "/usr/bin/progress -e -f /etc/motd /bin/cat > "
                        "/tmp/five-progress 2>/dev/null && "
                        "/usr/bin/cmp /etc/motd /tmp/five-progress && "
                        "echo 'RVM progress verified'\n",
                    ]
                if args.port25_utils and count == 1:
                    names = ('uuidgen fpr asa csplit checknr flock deroff locale fincore getent '
                             'pathchk unifdef ctags colcrt logname soelim apply sdiff shlock '
                             'gencat pwhash bzip2recover cap_mkdb db sortinfo').split()
                    for index in range(0, len(names), 5):
                        group = names[index:index+5]
                        utility_checks.append(' && '.join('/bin/test -x /usr/bin/'+name
                                                          for name in group) +
                                              f" && echo 'RVM port25 files {index//5+1} verified'\n")
                    utility_checks += [
                        "/usr/bin/uuidgen | /usr/bin/grep -q -- '-' && "
                        "/usr/bin/getent passwd root | /usr/bin/grep -q '^root:' && "
                        "/usr/bin/locale -a | /usr/bin/grep -q C && "
                        "/usr/bin/pathchk /tmp/port25 && "
                        "echo 'RVM port25 identities verified'\n",
                        "/usr/bin/flock /tmp/port25-lock /bin/echo locked | "
                        "/usr/bin/grep -q locked && "
                        "/usr/bin/fincore -s /etc/motd > /tmp/port25-fincore && "
                        "/usr/bin/grep -q 'in-core pages' /tmp/port25-fincore && "
                        "echo 'RVM port25 locks and cache verified'\n",
                        "cd /tmp && /usr/bin/printf 'a\\nb\\nc\\n' > port25-lines && "
                        "/usr/bin/csplit -s port25-lines /b/ && "
                        "/bin/test \"$(/bin/cat xx00)\" = a && "
                        "/usr/bin/printf 'b\\nc\\n' | /usr/bin/cmp - xx01 && "
                        "echo 'RVM port25 split verified'\n",
                        "cd /tmp && /usr/bin/printf '#ifdef ON\\nyes\\n#else\\nno\\n#endif\\n' "
                        "> port25-if && /usr/bin/unifdef -DON port25-if > port25-out; "
                        "code=$?; /bin/test \"$code\" -le 1 && "
                        "/bin/test \"$(/bin/cat port25-out)\" = yes && "
                        "echo 'RVM port25 preprocessor verified'\n",
                        "cd /tmp && /usr/bin/db -w hash port25.db key value && "
                        "/bin/test \"$(/usr/bin/db -V hash port25.db key)\" = value && "
                        "echo 'RVM port25 database verified'\n",
                        "/usr/bin/printf ' hello\\n' | /usr/bin/asa | "
                        "/usr/bin/grep -q hello && "
                        "/usr/bin/printf ' hello\\n' | /usr/bin/fpr | "
                        "/usr/bin/grep -q hello && "
                        "/usr/bin/printf 'hello\\n' | /usr/bin/colcrt | "
                        "/usr/bin/grep -q hello && "
                        "echo 'RVM port25 filters verified'\n",
                        "cd /tmp && /usr/bin/printf 'int port25fn(void) { return 1; }\\n' "
                        "> port25.c && /usr/bin/ctags port25.c && "
                        "/usr/bin/grep -q port25fn tags && "
                        "/usr/bin/printf 'include-ok\\n' > port25-inc && "
                        "/usr/bin/printf '.so /tmp/port25-inc\\n' | /usr/bin/soelim | "
                        "/usr/bin/grep -q include-ok && "
                        "echo 'RVM port25 source tools verified'\n",
                        "cd /tmp && /usr/bin/apply 'echo %1' apply-ok | "
                        "/usr/bin/grep -q apply-ok && "
                        "/usr/bin/sdiff port25-lines port25-lines | "
                        "/usr/bin/grep -q a && "
                        "/usr/bin/shlock -f port25-shlock -p $$ && "
                        "/bin/test -f port25-shlock && "
                        "echo 'RVM port25 helpers verified'\n",
                    ]
                if args.port25_extra and count == 1:
                    utility_checks += [
                        "/usr/bin/printf '.DS\\n.DE\\n' | /usr/bin/checknr > "
                        "/tmp/port25-checknr && /bin/test ! -s /tmp/port25-checknr && "
                        "/usr/bin/printf '.TH TEST 1\\nbody\\n' | /usr/bin/deroff | "
                        "/usr/bin/grep -q body && "
                        "/usr/bin/logname | /usr/bin/grep -q root && "
                        "echo 'RVM port25 text checks verified'\n",
                        "cd /tmp && /usr/bin/printf '$set 1\\n1 hello\\n' > "
                        "port25.msg && /usr/bin/gencat port25.cat port25.msg && "
                        "/bin/test -s port25.cat && "
                        "/usr/bin/printf 'demo|test:co#80:\\n' > port25.cap && "
                        "/usr/bin/cap_mkdb port25.cap && /bin/test -s port25.cap.db && "
                        "echo 'RVM port25 catalogs verified'\n",
                        "/usr/bin/pwhash -m secret | /usr/bin/grep -q '^\\$1\\$' && "
                        "/usr/bin/printf 'head\\n* Menu:\\n\\nB\\n* b::\\n' | "
                        "/usr/bin/sortinfo | /usr/bin/grep -q '^B$' && "
                        "echo 'RVM port25 hash and info verified'\n",
                        "cd /tmp && /usr/bin/bzip2 -c /etc/motd > port25.bz2 && "
                        "/usr/bin/bzip2recover port25.bz2 > /dev/null 2>&1 && "
                        "/usr/bin/bzip2 -dc rec00001port25.bz2 | "
                        "/usr/bin/cmp - /etc/motd && "
                        "echo 'RVM port25 recovery verified'\n",
                    ]
                command = f"id; printf 'RVM root login %s verified\\n' {count}; exit\n"
                if args.guest_command and count == 1:
                    utility_checks += [args.guest_command + '\n']
                steps += [('login: ', b'root\n'), ('Password:', password)]
                steps += [('# ', check.encode()) for check in utility_checks]
                if args.history_keys and count == 1:
                    steps += [('# ', b'echo RVM history key verified\n'),
                              ('RVM history key verified\n', b''),
                              ('# ', b'\x80\n'),
                              ('RVM history key verified\n', b'')]
                steps += [('# ', command.encode()),
                          (f'RVM root login {count} verified', b'')]
        stage = offset = 0
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
                        if args.idle_seconds > 0 and idle_ratio is None:
                            if idle_start is None:
                                idle_cpu = cpu_seconds()
                                idle_start = time.monotonic()
                            elapsed = time.monotonic()-idle_start
                            if elapsed < args.idle_seconds:
                                continue
                            idle_ratio = (cpu_seconds()-idle_cpu)/elapsed
                        if args.login and stage < len(steps):
                            expected, keys = steps[stage]
                            found = transcript.find(expected, offset)
                            if found >= 0:
                                offset = found+len(expected)
                                print(f'Login stage {stage+1}/{len(steps)}: {expected!r}, '
                                      f'wall={time.monotonic()-started:.1f}s, '
                                      f'CPU={cpu_seconds():.1f}s', flush=True)
                                if keys:
                                    with fifo.open('wb', buffering=0) as stream:
                                        stream.write(keys)
                                stage += 1
                        if not args.login or stage == len(steps):
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
        if args.login:
            assert stage == len(steps), f'Login stopped at stage {stage}/{len(steps)}:\n'+transcript[-10000:]
            if args.guest_marker:
                assert f'\n{args.guest_marker}\n' in transcript, transcript[-10000:]
            if args.history_keys:
                assert transcript.count('RVM history key verified\n') >= 2, transcript[-10000:]
            assert transcript.count('uid=0(root)') >= args.login_count, transcript[-5000:]
            if args.extended_utils:
                for marker in (
                        'RVM extended utilities group 1 verified',
                        'RVM extended utilities group 2 verified',
                        'RVM extended utilities group 3 verified',
                        'RVM extended utilities group 4 verified',
                        'RVM extended utilities group 5 verified',
                        'RVM extended utilities group 6 verified',
                        'RVM extended utilities group 7 verified',
                        'RVM extended utilities group 8 verified',
                        'RVM extended utilities group 9 verified',
                        'RVM extended utilities group 10 verified',
                        'RVM extended utilities group 11 verified',
                        'RVM extended utilities group 12 verified',
                        'RVM extended utilities group 13 verified',
                        'RVM extended utilities group 14 verified',
                        'RVM extended utilities group 15 verified',
                        'RVM extended utilities group 16 verified',
                        'RVM extended native utilities verified'):
                    assert f'\n{marker}\n' in transcript, transcript[-10000:]
            if args.system_utils:
                for marker in ('RVM system utilities group 1 verified',
                               'RVM system utilities verified'):
                    assert f'\n{marker}\n' in transcript, transcript[-10000:]
            if args.disk_utils:
                assert '\nRVM disk utilities verified\n' in transcript, transcript[-10000:]
                assert '\nRVM filesystem utilities verified\n' in transcript, transcript[-10000:]
            if args.swap_utils:
                assert '\nRVM swap device verified\n' in transcript, transcript[-10000:]
            if args.pager_utils:
                assert '\nRVM pager utilities verified\n' in transcript, transcript[-10000:]
            if args.encoding_utils:
                for marker in ('RVM encoders verified', 'RVM identifier utility verified'):
                    assert f'\n{marker}\n' in transcript, transcript[-10000:]
            if args.editor_utils:
                assert '\nRVM editor verified\n' in transcript, transcript[-10000:]
            if args.compression_utils:
                assert '\nRVM compression utilities verified\n' in transcript, transcript[-10000:]
            if args.terminal_utils:
                assert '\nRVM terminal utility verified\n' in transcript, transcript[-10000:]
            if args.math_utils:
                assert '\nRVM math utility verified\n' in transcript, transcript[-10000:]
            if args.top_utils:
                assert '\nRVM top utility verified\n' in transcript, transcript[-10000:]
            if args.archive_utils:
                for marker in ('RVM tar mode verified', 'RVM cpio mode verified'):
                    assert f'\n{marker}\n' in transcript, transcript[-10000:]
            if args.file_utils:
                assert '\nRVM file utility verified\n' in transcript, transcript[-10000:]
            if args.five_utils:
                for marker in ('RVM unvis verified', 'RVM getopt verified',
                               'RVM lam verified', 'RVM rs verified'):
                    assert f'\n{marker}\n' in transcript, transcript[-10000:]
            if args.five_utils or args.shuffle_utils:
                assert '\nRVM shuffle verified\n' in transcript, transcript[-10000:]
            if args.next_five_utils:
                for marker in ('RVM timeout verified', 'RVM mkfifo verified',
                               'RVM pwait verified', 'RVM printenv verified',
                               'RVM progress verified'):
                    assert f'\n{marker}\n' in transcript, transcript[-10000:]
            if args.port25_utils:
                for marker in [f'RVM port25 files {n} verified' for n in range(1, 6)] + [
                        'RVM port25 identities verified',
                        'RVM port25 locks and cache verified',
                        'RVM port25 split verified',
                        'RVM port25 preprocessor verified',
                        'RVM port25 database verified',
                        'RVM port25 filters verified',
                        'RVM port25 source tools verified',
                        'RVM port25 helpers verified']:
                    assert f'\n{marker}\n' in transcript, transcript[-10000:]
            if args.port25_extra:
                for marker in ('RVM port25 text checks verified',
                               'RVM port25 catalogs verified',
                               'RVM port25 hash and info verified',
                               'RVM port25 recovery verified'):
                    assert f'\n{marker}\n' in transcript, transcript[-10000:]
        if args.idle_seconds > 0:
            assert idle_ratio is not None, 'Idle sample did not complete: '+transcript[-3000:]
            print(f'Idle host CPU: {idle_ratio*100:.1f}% of one core over {elapsed:.1f}s', flush=True)
            if args.max_idle_cpu is not None:
                assert idle_ratio <= args.max_idle_cpu, f'Idle CPU ratio {idle_ratio:.3f} exceeds {args.max_idle_cpu:.3f}'
    kernel = 'EARLY kernel idle/HALT checks' if args.early else 'production kernel'
    print(f'PASS: {kernel} and minimal native init/rc/getty root ({args.ram_mib} MiB)')
    if args.login:
        print(f'PASS: {args.login_count} authenticated root logins, shell commands and logout on the copied production image')


if __name__ == '__main__':
    main()
