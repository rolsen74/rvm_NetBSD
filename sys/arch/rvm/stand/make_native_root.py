#!/usr/bin/env python3
"""Package a minimal native static userland with init/rc/getty/login."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
PROGRAMS = {
    **{'bin/'+name: 'bin/'+name+'/'+name for name in
       ('sh', 'echo', 'cat', 'pwd', 'sync', 'ls', 'test', 'mkdir', 'cp', 'mv', 'rm', 'stty', 'sleep')},
    **{'sbin/'+name: 'sbin/'+name+'/'+name for name in
       ('init', 'mount', 'mount_ffs', 'umount', 'reboot', 'fsck', 'fsck_ffs', 'mount_tmpfs', 'mount_ptyfs')},
    'usr/libexec/getty': 'libexec/getty/getty',
    'usr/bin/login': 'usr.bin/login/login',
    'usr/bin/passwd': 'usr.bin/passwd/passwd',
    'usr/sbin/pwd_mkdb': 'usr.sbin/pwd_mkdb/pwd_mkdb',
    'usr/bin/id': 'usr.bin/id/id',
    'usr/bin/yes': 'usr.bin/yes/yes',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, required=True)
    parser.add_argument('--kernel-objdir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path,
                        help='write the rebuilt image here instead of objdir/native-root')
    parser.add_argument('--master-passwd', type=Path,
                        help='guest master.passwd input; defaults to a locked root account')
    args = parser.parse_args()
    obj, kernel = args.objdir.resolve(), args.kernel_objdir.resolve()
    output = args.output_dir.resolve() if args.output_dir else obj/'native-root'
    output.mkdir(exist_ok=True)
    for source in PROGRAMS.values():
        if not (obj/source).is_file():
            raise SystemExit('Missing native program: '+str(obj/source))
    makefs = Path(subprocess.check_output([str(obj/'nbmake-rvm'), '-C', str(ROOT/'lib/libc'),
        '-V', '${TOOL_MAKEFS}'], text=True).strip())
    with tempfile.TemporaryDirectory(prefix='root-', dir=output) as temporary:
        work = Path(temporary)
        tree = work/'tree'
        for name in ('bin', 'sbin', 'usr/bin', 'usr/sbin', 'usr/libexec', 'dev/pts', 'etc',
                     'root', 'tmp', 'var/run', 'var/log'):
            (tree/name).mkdir(parents=True, exist_ok=True)
        for path in tree.rglob('*'):
            if path.is_dir():
                path.chmod(0o755)
        for destination, source in PROGRAMS.items():
            path = tree/destination
            shutil.copyfile(obj/source, path)
            path.chmod(0o755)
        for name, target in (('bin/[', 'test'), ('usr/bin/whoami', 'id'),
                             ('usr/bin/groups', 'id'), ('sbin/halt', 'reboot'),
                             ('sbin/poweroff', 'reboot'), ('sbin/mount_ufs', 'mount_ffs')):
            (tree/name).symlink_to(target)
        (tree/'etc/rc').write_text('''#!/bin/sh
set -e
/sbin/fsck -p /
/sbin/mount -uw /
/sbin/mount /tmp
/sbin/mount /dev/pts
echo 'NetBSD/RVM native root: writable FFS, native init and shell ready'
''')
        (tree/'etc/rc').chmod(0o755)
        (tree/'etc/fstab').write_text('/dev/ld0a / ffs rw 1 1\n'
                                     'tmpfs /tmp tmpfs rw,-m=1777,-s=16m 0 0\n'
                                     'ptyfs /dev/pts ptyfs rw 0 0\n')
        (tree/'etc/ttys').write_text('console "/usr/libexec/getty std.9600" dumb on secure\n')
        shutil.copyfile(ROOT/'etc/gettytab', tree/'etc/gettytab')
        shutil.copyfile(ROOT/'etc/passwd.conf', tree/'etc/passwd.conf')
        (tree/'etc/group').write_text('wheel:*:0:root\ntty:*:4:\n')
        (tree/'etc/shells').write_text('/bin/sh\n')
        (tree/'etc/profile').write_text('PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH\n')
        (tree/'etc/motd').write_text('Welcome to the native NetBSD/RVM static userland.\n')
        (tree/'etc/login.conf').write_text('default:path=/bin /usr/bin /sbin /usr/sbin:umask=022:\n')
        password = work/'password.in'
        if args.master_passwd:
            shutil.copyfile(args.master_passwd.resolve(), password)
        else:
            password.write_text('root:*:0:0::0:0:System administrator:/root:/bin/sh\n')
        password.chmod(0o600)
        subprocess.run([str(obj/'hosttools/bin/nbpwd_mkdb'), '-B', '-p', '-d', str(tree), str(password)], check=True)
        for path in (tree/'etc').iterdir():
            if path.name not in ('master.passwd', 'spwd.db', 'rc'):
                path.chmod(0o644)
        metadata = ['. type=dir mode=0755 uid=0 gid=0']
        for path in sorted(tree.rglob('*')):
            name = './'+str(path.relative_to(tree))
            if path.is_symlink():
                metadata.append(name+' type=link uid=0 gid=0 link='+str(path.readlink()))
                continue
            kind = 'dir' if path.is_dir() else 'file'
            mode = path.stat().st_mode & 0o7777
            if path == tree/'tmp':
                mode = 0o1777
            elif path in (tree/'usr/bin/login', tree/'usr/bin/passwd'):
                mode = 0o4555
            metadata.append(f'{name} type={kind} mode={mode:04o} uid=0 gid=0')
        for name, kind, major, minor, mode in (
                ('console', 'char', 240, 0, '0600'), ('tty', 'char', 241, 0, '0666'),
                ('null', 'char', 242, 2, '0666'), ('zero', 'char', 242, 12, '0666'),
                ('ptm', 'char', 165, 1, '0666'), ('ptmx', 'char', 165, 0, '0666'),
                ('ld0a', 'block', 303, 0, '0600'), ('rld0a', 'char', 303, 0, '0600'),
                ('rld0c', 'char', 303, 2, '0600')):
            metadata.append(f'./dev/{name} type={kind} mode={mode} uid=0 gid=0 device=netbsd,{major},{minor}')
        spec = work/'root.spec'
        spec.write_text('\n'.join(metadata)+'\n')
        image = output/'root-native.new'
        subprocess.run([str(makefs), '-t', 'ffs', '-B', 'be', '-s', '32m', '-f', '256',
                        '-F', str(spec), '-o', 'version=2,bsize=16384,fsize=2048',
                        str(image), str(tree)], check=True)
        image.replace(output/'root-native.img')
    for name in ('netbsd.fhr', 'bootloader.bin'):
        shutil.copyfile(kernel/name, output/name)
    config = (ROOT/'sys/arch/rvm/conf/vmemu.conf').read_text()
    config += ('\n[HARDFILE]\n Address = 0x0700000000003000\n'
               ' File = "root-native.img"\n ReadOnly = 0\n End\n')
    (output/'vm.config').write_text(config)
    print('Native NetBSD/RVM image:', output/'root-native.img')
    print('Run VMEmu -c vm.config from', output)


if __name__ == '__main__':
    main()
