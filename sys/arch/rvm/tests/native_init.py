#!/usr/bin/env python3
"""Boot normal static init/sh/getty on a disposable FFS integration image."""
import argparse
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[4]
STAND = ROOT/'sys/arch/rvm/stand'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, default=ROOT/'obj.rvm-native-userland')
    parser.add_argument('--fixture-tree', type=Path, required=True,
                        help='a previously exported native libc guest fixture')
    parser.add_argument('--kernel-objdir', type=Path, default=ROOT/'obj.rvm-diskroot')
    parser.add_argument('--toolchain', type=Path, default=ROOT/'../rvm_gcc/rvm_cross_netbsd')
    parser.add_argument('--vmemu', default='VMEmu')
    parser.add_argument('--login', action='store_true',
                        help='also test native login with a disposable test password database')
    parser.add_argument('--change-password', action='store_true',
                        help='change the guest password, rebuild its databases and log in again')
    parser.add_argument('--password-cipher', choices=('blowfish,4', 'argon2id'), default='blowfish,4',
                        help='password-change cipher; argon2id exercises the normal image configuration')
    parser.add_argument('--fsck', action='store_true',
                        help='force read-only filesystem checks through raw disk I/O before rc remounts root')
    parser.add_argument('--tmpfs', action='store_true',
                        help='mount tmpfs and check mappings, truncation faults and its size limit')
    parser.add_argument('--pty', action='store_true',
                        help='mount ptyfs and check terminal allocation, line disciplines and forkpty')
    parser.add_argument('--ram-mib', type=int, default=64)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--idle-seconds', type=float, default=0,
                        help='measure idle CPU use at the prompt before sending login input')
    parser.add_argument('--max-idle-cpu', type=float,
                        help='maximum host CPU seconds per wall second during the idle sample')
    args = parser.parse_args()
    if args.change_password and not args.login:
        parser.error('--change-password requires --login')
    obj, kernel, cross = args.objdir.resolve(), args.kernel_objdir.resolve(), args.toolchain.resolve()
    fixture = args.fixture_tree.resolve()
    work = obj/('pty-tests' if args.pty else 'tmpfs-tests' if args.tmpfs else 'fsck-tests' if args.fsck else 'init-tests')
    work.mkdir(exist_ok=True)
    cc = str(cross/'bin/rvm-netbsd-gcc')
    env = dict(os.environ, RVM_REAL_CC=cc)
    include = subprocess.check_output([cc, '-print-file-name=include'], text=True).strip()
    source = work/'mount.c'
    source.write_text('''#include <sys/mount.h>
#include <stdio.h>
#include <pwd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef RVM_PASSWORD_PREFIX
#define RVM_PASSWORD_PREFIX "$2"
#endif
int main(int argc, char **argv) {
    if (argc > 1) {
        const char *user = getenv("USER"), *home = getenv("HOME");
        if (getuid() != 0 || getgid() != 0 || !isatty(0) ||
            !user || strcmp(user, "root") || !home || strcmp(home, "/root"))
            return 1;
        if (strcmp(argv[1], "changed") == 0) {
            struct passwd *pw = getpwnam("root");
            struct stat status;
            if (!pw || strncmp(pw->pw_passwd, RVM_PASSWORD_PREFIX, sizeof(RVM_PASSWORD_PREFIX)-1) ||
                stat("/etc/spwd.db", &status) != 0 || (status.st_mode & 0777) != 0600)
                return 1;
            puts("RVM native passwd: database rebuild and new-password login verified");
        } else {
            puts("RVM native login: password, identity, environment and controlling tty verified");
        }
        return 0;
    }
    char *device = NULL;
    if (mount("ffs", "/", MNT_UPDATE, &device, sizeof(device)) != 0) {
        perror("native init mount"); return 1;
    }
    puts("RVM native init: writable FFS root verified"); return 0;
}
''')
    target = source.with_suffix('.o')
    cipher_flag = ['-DRVM_PASSWORD_PREFIX="$argon2id$"'] if args.password_cipher == 'argon2id' else []
    subprocess.run([str(STAND/'user_cc.py'), '-O2', '-mfpu', '-D_NETBSD_SOURCE'] + cipher_flag + [
                    '-nostdinc', '-isystem', str(obj/'destdir.rvm/usr/include'),
                    '-isystem', include, '-c', str(source), '-o', str(target)], check=True, env=env)
    mount = work/'mount'
    subprocess.run([sys.executable, str(STAND/'user_prog.py'), '--toolchain', str(cross),
                    '--sysroot', str(obj/'destdir.rvm'), '-o', str(mount), str(target)], check=True)
    for enabled, name, libraries in ((args.tmpfs, 'tmpfs', []), (args.pty, 'pty', ['-lutil'])):
        if not enabled:
            continue
        target = work/(name+'.o')
        subprocess.run([str(STAND/'user_cc.py'), '-O2', '-mfpu', '-D_NETBSD_SOURCE',
                        '-nostdinc', '-isystem', str(obj/'destdir.rvm/usr/include'),
                        '-isystem', include, '-c', str(Path(__file__).with_name('native_'+name+'.c')),
                        '-o', str(target)], check=True, env=env)
        subprocess.run([sys.executable, str(STAND/'user_prog.py'), '--toolchain', str(cross),
                        '--sysroot', str(obj/'destdir.rvm'), '-o', str(work/name), str(target)] + libraries, check=True)
    makefs = Path(subprocess.check_output([str(obj/'nbmake-rvm'), '-C', str(ROOT/'lib/libc'),
        '-V', '${TOOL_MAKEFS}'], text=True).strip())
    with tempfile.TemporaryDirectory(prefix='rvm-native-init-') as temporary:
        boot = Path(temporary)
        tree = boot/'tree'
        shutil.copytree(fixture, tree)
        for original, guest in ((obj/'sbin/init/init', 'sbin/init'),
                                (obj/'libexec/getty/getty', 'usr/libexec/getty'),
                                (obj/'bin/sh/sh', 'bin/sh'), (mount, 'bin/native-init-mount')):
            path = tree/guest
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, path)
            path.chmod(0o755)
        for name in ('var/run', 'var/log'):
            (tree/name).mkdir(parents=True, exist_ok=True)
        administration = (('sbin/mount/mount', 'sbin/mount'),
                          ('sbin/mount_ffs/mount_ffs', 'sbin/mount_ffs'),
                          ('bin/stty/stty', 'bin/stty'), ('usr.bin/id/id', 'usr/bin/id'))
        native_admin = all((obj/source).is_file() for source, _ in administration)
        if native_admin:
            for source, destination in administration:
                guest = tree/destination
                guest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(obj/source, guest)
                guest.chmod(0o755)
        if args.login:
            login = tree/'usr/bin/login'
            shutil.copyfile(obj/'usr.bin/login/login', login)
            login.chmod(0o4555)
            # Known MD5-crypt vector for a disposable guest account. This is
            # not a host account/database; nbpwd_mkdb writes only inside tree.
            password = boot/'password.in'
            password.write_text('root:$1$saltstri$qQY4WxjABChYG1ccLpfkz/:0:0::0:0:'
                                'RVM test root:/root:/bin/sh\n')
            subprocess.run([str(obj/'hosttools/bin/nbpwd_mkdb'), '-B', '-p',
                            '-d', str(tree), str(password)], check=True)
        if args.change_password:
            for source, destination in (('usr.bin/passwd/passwd', 'usr/bin/passwd'),
                                        ('usr.sbin/pwd_mkdb/pwd_mkdb', 'usr/sbin/pwd_mkdb')):
                path = tree/destination
                path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(obj/source, path)
                path.chmod(0o4555 if path.name == 'passwd' else 0o755)
            (tree/'etc/passwd.conf').write_text('default:\n\tlocalcipher = '+args.password_cipher+'\n')
        if args.fsck:
            for name in ('fsck', 'fsck_ffs'):
                shutil.copyfile(obj/'sbin'/name/name, tree/'sbin'/name)
                (tree/'sbin'/name).chmod(0o755)
            (tree/'etc/fstab').write_text('/dev/ld0a / ffs rw 1 1\n')
        if args.tmpfs:
            shutil.copyfile(obj/'sbin/mount_tmpfs/mount_tmpfs', tree/'sbin/mount_tmpfs')
            (tree/'sbin/mount_tmpfs').chmod(0o755)
            shutil.copyfile(work/'tmpfs', tree/'bin/native-tmpfs')
            (tree/'bin/native-tmpfs').chmod(0o755)
        if args.pty:
            (tree/'dev/pts').mkdir(exist_ok=True)
            with (tree/'etc/group').open('a') as groups:
                groups.write('tty:*:4:\n')
            shutil.copyfile(obj/'sbin/mount_ptyfs/mount_ptyfs', tree/'sbin/mount_ptyfs')
            (tree/'sbin/mount_ptyfs').chmod(0o755)
            shutil.copyfile(work/'pty', tree/'bin/native-pty')
            (tree/'bin/native-pty').chmod(0o755)
        root_mount = '/bin/native-init-mount\n'
        if native_admin:
            root_mount = '''/sbin/mount_ffs -o update,rw /dev/ld0a /
echo 'RVM native init: writable FFS root verified'
/sbin/mount
[ "$(/usr/bin/id -u)" = 0 ]
/bin/stty -a > /dev/null
echo 'RVM native init: normal mount, id and stty verified'
'''
        filesystem_check = ('''/sbin/fsck_ffs -fn /dev/rld0a
/sbin/fsck -fn -t ffs /dev/rld0a
echo 'RVM native fsck: forced read-only UFS2 passes through raw disk verified'
''' if args.fsck else '')
        tmpfs_check = ('''/sbin/mount_tmpfs -s 8m -m 1777 tmpfs /tmp
/bin/native-tmpfs
''' if args.tmpfs else '')
        pty_check = ('''/sbin/mount_ptyfs ptyfs /dev/pts
/bin/native-pty
''' if args.pty else '')
        (tree/'etc/rc').write_text('''#!/bin/sh
set -e
[ "$PPID" = 1 ]
'''+filesystem_check+root_mount+tmpfs_check+pty_check+'''
/bin/sh /etc/native-shell.sh
echo 'RVM native init: PID 1 ran /etc/rc successfully'
''')
        (tree/'etc/ttys').write_text('console "/usr/libexec/getty std.9600" dumb on secure\n')
        (tree/'etc/gettytab').write_text(
            'default:ce:ck:np:im=RVM native getty ready\\r\\n:lm=login\\72 :\n'
            'std.9600:sp#9600:tc=default:\n')
        # Preserve device specifications from make_root, while discovering
        # ordinary files/directories added for this boot.
        devices = [line for line in Path(str(fixture)+'.spec').read_text().splitlines()
                   if ' type=char ' in line]
        if native_admin:
            devices.append('./dev/ld0a type=block mode=0600 uid=0 gid=0 device=netbsd,303,0')
        if args.fsck:
            devices.append('./dev/rld0a type=char mode=0600 uid=0 gid=0 device=netbsd,303,0')
        if args.pty:
            devices += ['./dev/ptm type=char mode=0666 uid=0 gid=0 device=netbsd,165,1',
                        './dev/ptmx type=char mode=0666 uid=0 gid=0 device=netbsd,165,0']
        metadata = ['. type=dir mode=0755 uid=0 gid=0']
        for path in sorted(tree.rglob('*')):
            kind = 'dir' if path.is_dir() else 'file'
            mode = '1777' if path == tree/'tmp' else f'{path.stat().st_mode & 0o7777:04o}'
            metadata.append(f'./{path.relative_to(tree)} type={kind} mode={mode} uid=0 gid=0')
        spec = boot/'root.spec'
        spec.write_text('\n'.join(metadata+devices)+'\n')
        subprocess.run([str(makefs), '-t', 'ffs', '-B', 'be', '-s', '32m', '-f', '256',
                        '-F', str(spec), '-o', 'version=2,bsize=16384,fsize=2048',
                        str(boot/'root-fixture.img'), str(tree)], check=True)
        for name in ('netbsd.fhr', 'bootloader.bin'):
            shutil.copyfile(kernel/name, boot/name)
        import re
        config = (obj/'guest-tests/vm.diskroot.config').read_text()
        (boot/'vm.config').write_text(re.sub(r'Size = \d+', f'Size = {args.ram_mib * 16}', config, count=1))
        env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        fifo = boot/'keys'
        if args.login:
            injector = boot/'keyboard.so'
            subprocess.run(['cc', '-shared', '-fPIC', '-O2',
                            str(Path(__file__).with_name('keyboard_events.c')),
                            '-ldl', '-lSDL2', '-o', str(injector)], check=True)
            os.mkfifo(fifo)
            env.update(LD_PRELOAD=str(injector), RVM_TEST_KEYFILE=str(fifo))
        process = subprocess.Popen(['stdbuf', '-oL', '-eL', shutil.which(args.vmemu), '-c', 'vm.config'],
            cwd=boot, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = bytearray()
        markers = ('RVM native init: writable FFS root verified',
                   'RVM native init: PID 1 ran /etc/rc successfully', 'RVM native getty ready', 'login: ')
        if native_admin:
            markers += ('RVM native init: normal mount, id and stty verified',)
        if args.fsck:
            markers += ('RVM native fsck: forced read-only UFS2 passes through raw disk verified',)
        if args.tmpfs:
            markers += ('RVM native tmpfs: sparse files, shared/private mmap, fork, truncation SIGBUS and unlinked I/O verified',
                        'RVM native tmpfs: mount size limit and reclaimed capacity verified')
        if args.pty:
            markers += ('RVM native pty: allocation, permissions, raw/canonical I/O, poll/select, forkpty and controlling shell verified',)
        deadline = time.monotonic()+args.timeout
        stage, offset = 0, 0
        idle_start = idle_cpu = idle_ratio = None
        def cpu_seconds():
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
                        (work/'boot.log').write_bytes(output)
                    transcript = output.decode(errors='replace').replace('\r', '')
                    ready = all(marker in transcript for marker in markers)
                    if ready and args.idle_seconds > 0 and idle_ratio is None:
                        if idle_start is None:
                            idle_cpu = cpu_seconds()
                            idle_start = time.monotonic()
                        elapsed = time.monotonic()-idle_start
                        if elapsed < args.idle_seconds:
                            continue
                        idle_ratio = (cpu_seconds()-idle_cpu)/elapsed
                        print(f'Idle host CPU: {idle_ratio*100:.1f}% of one core over {elapsed:.1f}s', flush=True)
                        if args.max_idle_cpu is not None:
                            assert idle_ratio <= args.max_idle_cpu, f'Idle CPU ratio {idle_ratio:.3f} exceeds {args.max_idle_cpu:.3f}'
                    if args.login and ready:
                        steps = [('login: ', b'root\n'), ('Password:', b'wrong-password\n'),
                                 ('login: ', b'root\n'), ('Password:', b'password\n'),
                                 ('# ', b'/bin/native-init-mount login\n')]
                        final_marker = 'RVM native login: password, identity, environment and controlling tty verified'
                        if args.change_password:
                            steps += [('# ', b'/usr/bin/passwd root\n'),
                                      ('New password:', b'Rvm-Changed42\n'),
                                      ('Retype new password:', b'Rvm-Changed42\n'),
                                      ('# ', b'echo RVM-password-status:$?; exit\n'),
                                      ('login: ', b'root\n'), ('Password:', b'password\n'),
                                      ('login: ', b'root\n'), ('Password:', b'Rvm-Changed42\n'),
                                      ('# ', b'/bin/native-init-mount changed\n')]
                            final_marker = 'RVM native passwd: database rebuild and new-password login verified'
                        if stage < len(steps) and steps[stage][0] in transcript[offset:]:
                            offset = len(transcript)
                            with fifo.open('wb', buffering=0) as keys:
                                keys.write(steps[stage][1])
                            stage += 1
                        ready = stage == len(steps) and final_marker in transcript[offset:]
                    if ready or 'panic:' in transcript:
                        break
        finally:
            if process.poll() is None:
                process.kill()
            rest, _ = process.communicate()
            output.extend(rest)
        (work/'boot.log').write_bytes(output)
        transcript = output.decode(errors='replace').replace('\r', '')
        assert all(marker in transcript for marker in markers), transcript[-10000:]
        if args.idle_seconds > 0:
            assert idle_ratio is not None, 'Idle sample did not complete'
        if args.login:
            assert 'Login incorrect' in transcript, transcript[-10000:]
            assert 'RVM native login: password, identity, environment and controlling tty verified' in transcript, transcript[-10000:]
        if args.change_password:
            assert 'RVM-password-status:0' in transcript, transcript[-10000:]
            assert transcript.count('Login incorrect') >= 2, transcript[-10000:]
            assert 'RVM native passwd: database rebuild and new-password login verified' in transcript, transcript[-10000:]
        assert 'panic:' not in transcript and 'fatal kernel exception' not in transcript
    detail = ('password change and authenticated login' if args.change_password else
              'authenticated login' if args.login else 'getty login prompt')
    if args.fsck:
        detail = 'raw disk fsck and '+detail
    if args.tmpfs:
        detail = 'tmpfs mappings/accounting and '+detail
    if args.pty:
        detail = 'ptyfs terminal/session tests and '+detail
    print(f'PASS: native PID 1 init, rc/sh and {detail} ({args.ram_mib} MiB)')


if __name__ == '__main__':
    main()
