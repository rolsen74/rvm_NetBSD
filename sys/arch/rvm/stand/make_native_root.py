#!/usr/bin/env python3
"""Package a minimal native static userland with init/rc/getty/login."""
import argparse
import ipaddress
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import re
from fhr_pack import pack as pack_fhr

ROOT = Path(__file__).resolve().parents[4]
PROGRAMS = {
    **{'bin/'+name: 'bin/'+name+'/'+name for name in
       ('sh', 'echo', 'cat', 'pwd', 'sync', 'ls', 'test', 'mkdir', 'cp', 'mv', 'rm', 'stty', 'sleep',
        'chmod', 'ln', 'kill', 'hostname', 'date', 'df', 'expr', 'ed', 'pax',
        'dd', 'rmdir', 'domainname', 'ps')},
    **{'sbin/'+name: 'sbin/'+name+'/'+name for name in
       ('init', 'mount', 'mount_ffs', 'umount', 'reboot', 'fsck', 'fsck_ffs', 'mount_tmpfs', 'mount_ptyfs',
        'chown', 'dmesg', 'sysctl', 'ifconfig', 'route', 'disklabel', 'swapctl',
        'newfs', 'tunefs', 'ping')},
    **{'sbin/'+name: 'sbin/'+name+'/'+name for name in
       ('mknod', 'rndctl', 'scan_ffs', 'mount_kernfs', 'mount_procfs',
        'mount_union', 'mount_msdos', 'fsck_msdos', 'mount_cd9660',
        'mount_ext2fs', 'fsck_ext2fs')},
    **{'sbin/'+name: 'sbin/'+name+'/'+name for name in
       ('newfs_ext2fs', 'fsirand', 'blkdiscard', 'mount_fdesc',
        'mount_overlay', 'mount_ntfs', 'mount_lfs', 'fsck_lfs',
        'dkctl', 'drvctl')},
    'usr/libexec/getty': 'libexec/getty/getty',
    'usr/bin/login': 'usr.bin/login/login',
    'usr/bin/passwd': 'usr.bin/passwd/passwd',
    'usr/sbin/pwd_mkdb': 'usr.sbin/pwd_mkdb/pwd_mkdb',
    **{'usr/sbin/'+name: 'usr.sbin/'+name+'/'+name for name in
       ('chroot', 'lastlogin', 'accton', 'quot', 'mtree', 'services_mkdb')},
    **{'usr/sbin/'+name: 'usr.sbin/'+name+'/'+name for name in
       ('dumpfs', 'zdump', 'unlink', 'diskpart', 'cpuctl',
        'quotaon', 'repquota')},
    'usr/bin/id': 'usr.bin/id/id',
    'usr/bin/yes': 'usr.bin/yes/yes',
    **{'usr/bin/'+name: 'usr.bin/'+name+'/'+name for name in
       ('basename', 'dirname', 'env', 'head', 'tail', 'wc', 'printf', 'uname', 'touch',
        'grep', 'sed', 'cut', 'tr', 'sort', 'uniq', 'xargs')},
    **{'usr/bin/'+name: 'usr.bin/'+name+'/'+name for name in
       ('find', 'du', 'cksum', 'cmp', 'tee', 'stat', 'jot', 'seq',
        'comm', 'fold', 'join', 'paste', 'mktemp', 'realpath', 'hexdump', 'tsort')},
    'usr/bin/awk': 'external/historical/nawk/bin/awk',
    'usr/bin/less': 'external/bsd/less/bin/less/less',
    'usr/bin/vi': 'external/bsd/nvi/usr.bin/nvi/vi',
    'usr/bin/gzip': 'usr.bin/gzip/gzip',
    'usr/bin/bzip2': 'usr.bin/bzip2/bzip2',
    'usr/bin/script': 'usr.bin/script/script',
    'usr/bin/bc': 'external/bsd/bc/bin/bc',
    'usr/bin/top': 'external/bsd/top/bin/top',
    'usr/bin/file': 'external/bsd/file/bin/file',
    'usr/bin/vis': 'usr.bin/vis/vis',
    **{'usr/bin/'+name: 'usr.bin/'+name+'/'+name for name in
       ('base64', 'uuencode', 'uudecode', 'what',
        'unvis', 'getopt', 'lam', 'rs', 'shuffle',
        'timeout', 'mkfifo', 'pwait', 'printenv', 'progress')},
    **{'usr/bin/'+name: 'usr.bin/'+name+'/'+name for name in
       ('column', 'rev', 'split', 'nl', 'pr', 'cal', 'patch',
        'diff', 'diff3', 'm4', 'time', 'tty', 'fmt', 'compress', 'make',
        'nice', 'nohup', 'getconf', 'expand', 'unexpand', 'col', 'chflags')},
    **{'usr/bin/'+name: 'usr.bin/'+name+'/'+name for name in
       ('logger', 'mesg', 'whereis', 'look', 'banner', 'colrm', 'tput',
        'renice', 'who', 'users', 'w', 'pkill', 'vmstat', 'netstat', 'fstat')},
    **{'usr/bin/'+name: 'usr.bin/'+name+'/'+name for name in
       ('uuidgen', 'fpr', 'asa', 'csplit', 'checknr', 'flock', 'deroff',
        'locale', 'fincore', 'getent', 'pathchk', 'unifdef', 'ctags',
        'colcrt', 'logname', 'soelim', 'apply', 'sdiff', 'shlock',
        'gencat', 'pwhash', 'bzip2recover', 'cap_mkdb', 'db', 'sortinfo')},
    **{'usr/bin/'+name: 'usr.bin/'+name+'/'+name for name in
       ('leave', 'fsplit', 'tabs', 'lastcomm', 'infocmp', 'tset', 'resize',
        'units', 'qsubst', 'from', 'whois', 'wall', 'getaddrinfo',
        'getnameinfo', 'innetgr', 'revoke', 'error', 'nbperf', 'tcopy',
        'vndcompress')},
    **{'usr/bin/'+name: 'usr.bin/'+name+'/'+name for name in
       ('atto', 'last', 'lock', 'fgen', 'mkcsmapper', 'mkesdb', 'mklocale',
        'ipcs', 'ipcrm', 'mkubootimage', 'mkdep', 'rpcinfo',
        'netgroup', 'ul', 'write', 'tic', 'calendar', 'indent', 'tftp',
        'finger')},
    **{'usr/bin/'+name: 'usr.bin/'+name+'/'+name for name in
       ('newsyslog', 'ktrace', 'rlogin')},
    **{'usr/bin/'+name: 'usr.bin/'+name+'/'+name for name in
       ('rsh', 'showmount', 'quota')},
}


def add_swap_partition(image, swap_mib):
    """Append swap and label the FFS image as ld0a, ld0b, and raw ld0c."""
    sector = 512
    root_bytes = image.stat().st_size
    swap_bytes = swap_mib * 1024 * 1024
    if root_bytes % sector or swap_bytes % 4096:
        raise SystemExit('Root and swap sizes must be sector/page aligned')
    root_sectors = root_bytes // sector
    swap_sectors = swap_bytes // sector
    total_sectors = root_sectors + swap_sectors
    if swap_sectors <= 0 or total_sectors > 0xffffffff:
        raise SystemExit('Swap size must be positive and total disk must fit a disklabel')

    # NetBSD's big-endian 4BSD disklabel occupies sector 1. makefs leaves
    # this sector in the FFS boot area, before the filesystem superblock.
    label = bytearray(sector)
    struct.pack_into('>IHH', label, 0, 0x82564557, 15, 0)  # magic, DKTYPE_LD
    label[8:8+len(b'RVM hardfile')] = b'RVM hardfile'
    label[24:24+len(b'native root')] = b'native root'
    struct.pack_into('>6I', label, 40, sector, 32, 64,
                     (total_sectors + 2047) // 2048, 2048, total_sectors)
    struct.pack_into('>HH', label, 72, 3600, 1)  # rpm, interleave
    struct.pack_into('>IHHII', label, 132, 0x82564557, 0, 3, 8192, 65536)
    struct.pack_into('>IIIBBH', label, 148, root_sectors, 0, 2048, 7, 8, 0)
    struct.pack_into('>IIIBBH', label, 164, swap_sectors, root_sectors, 0, 1, 0, 0)
    struct.pack_into('>IIIBBH', label, 180, total_sectors, 0, 0, 0, 0, 0)
    checksum = 0
    for (word,) in struct.iter_unpack('>H', label[:148 + 3 * 16]):
        checksum ^= word
    struct.pack_into('>H', label, 136, checksum)
    with image.open('r+b') as disk:
        disk.truncate(root_bytes + swap_bytes)
        disk.seek(sector)
        disk.write(label)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, required=True)
    parser.add_argument('--kernel-objdir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path,
                        help='write the rebuilt image here instead of objdir/native-root')
    parser.add_argument('--master-passwd', type=Path,
                        help='guest master.passwd input; defaults to a locked root account')
    parser.add_argument('--image-size', default='128m',
                        help='FFS root partition size (default: 128m)')
    parser.add_argument('--swap-mib', type=int, default=64,
                        help='swap partition size in MiB (default: 64)')
    parser.add_argument('--tap',
                        help='attach the named Linux TAP interface through VMEmu NETWORK')
    parser.add_argument('--guest-ipv4', type=ipaddress.IPv4Interface,
                        help='configure rvmnet0 at boot, for example 192.168.76.2/24')
    parser.add_argument('--guest-gateway', type=ipaddress.IPv4Address,
                        help='default gateway; defaults to the first address in the guest subnet')
    parser.add_argument('--guest-dns', type=ipaddress.IPv4Address,
                        help='DNS server for the guest (default: 1.1.1.1)')
    parser.add_argument('--extra-file', action='append', default=[], metavar='DEST=SOURCE',
                        help='copy an additional file into the image (for focused tests)')
    parser.add_argument('--pack-program', action='append', default=[], metavar='DEST',
                        help='store a built FHR executable in zlib-packed form')
    parser.add_argument('--pack-all-programs', action='store_true',
                        help='store every built FHR executable in zlib-packed form')
    parser.add_argument('--pack-kernel', action='store_true',
                        help='store the kernel FHR packed for run.sh staging')
    parser.add_argument('--pack-all-fhr', action='store_true',
                        help='pack all programs, FHR extra files, and the kernel')
    args = parser.parse_args()
    if args.tap and not re.fullmatch(r'[A-Za-z0-9_.-]{1,15}', args.tap):
        parser.error('--tap must be a Linux interface name of at most 15 characters')
    if args.guest_ipv4 and not args.tap:
        parser.error('--guest-ipv4 requires --tap')
    if (args.guest_gateway or args.guest_dns) and not args.guest_ipv4:
        parser.error('--guest-gateway and --guest-dns require --guest-ipv4')
    if args.guest_ipv4 and args.guest_ipv4.network.prefixlen > 30:
        parser.error('--guest-ipv4 requires a subnet with a usable gateway')
    if args.guest_gateway and (args.guest_gateway not in args.guest_ipv4.network or
                               args.guest_gateway == args.guest_ipv4.ip):
        parser.error('--guest-gateway must be another address in the guest subnet')
    if (args.guest_ipv4 and not args.guest_gateway and
            args.guest_ipv4.ip == args.guest_ipv4.network.network_address + 1):
        parser.error('--guest-ipv4 uses the default gateway address; set --guest-gateway')
    obj, kernel = args.objdir.resolve(), args.kernel_objdir.resolve()
    output = args.output_dir.resolve() if args.output_dir else obj/'native-root'
    packed_count = packed_original = packed_stored = 0
    unknown_packed = set(args.pack_program) - PROGRAMS.keys()
    if unknown_packed:
        raise SystemExit('Unknown program to pack: '+', '.join(sorted(unknown_packed)))
    output.mkdir(exist_ok=True)
    for source in PROGRAMS.values():
        if not (obj/source).is_file():
            raise SystemExit('Missing native program: '+str(obj/source))
    makefs = Path(subprocess.check_output([str(obj/'nbmake-rvm'), '-C', str(ROOT/'lib/libc'),
        '-V', '${TOOL_MAKEFS}'], text=True).strip())
    host_file = obj/'hosttools/bin/nbfile'
    if not host_file.is_file():
        subprocess.run([str(obj/'nbmake-rvm'), '-C', str(ROOT/'tools/file'), 'install'],
                       check=True)
    with tempfile.TemporaryDirectory(prefix='root-', dir=output) as temporary:
        work = Path(temporary)
        tree = work/'tree'
        for name in ('bin', 'sbin', 'usr/bin', 'usr/sbin', 'usr/libexec', 'usr/share/misc', 'usr/share/locale', 'dev/pts', 'etc',
                     'root', 'tmp', 'var/run', 'var/log', 'var/tmp/vi.recover'):
            (tree/name).mkdir(parents=True, exist_ok=True)
        for path in tree.rglob('*'):
            if path.is_dir():
                path.chmod(0o755)
        for destination, source in PROGRAMS.items():
            path = tree/destination
            shutil.copyfile(obj/source, path)
            path.chmod(0o755)
            if args.pack_all_fhr or args.pack_all_programs or destination in args.pack_program:
                original = path.read_bytes()
                packed = pack_fhr(original)
                path.write_bytes(packed)
                packed_count += 1
                packed_original += len(original)
                packed_stored += len(packed)
        for item in args.extra_file:
            destination, separator, source = item.partition('=')
            relative = Path(destination)
            if (not separator or relative.is_absolute() or '..' in relative.parts or
                    not destination or not Path(source).is_file()):
                raise SystemExit('Expected --extra-file DEST=SOURCE with a regular source file')
            path = tree/relative
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, path)
            path.chmod(0o755 if path.suffix != '.so' else 0o644)
            if args.pack_all_fhr:
                original = path.read_bytes()
                if original[:4] == b'FHR\0' and original[7] == 0:
                    packed = pack_fhr(original)
                    path.write_bytes(packed)
                    packed_count += 1
                    packed_original += len(original)
                    packed_stored += len(packed)
        for name, target in (('bin/[', 'test'), ('usr/bin/whoami', 'id'),
                             ('usr/bin/groups', 'id'), ('sbin/halt', 'reboot'),
                             ('sbin/poweroff', 'reboot'), ('sbin/mount_ufs', 'mount_ffs')):
            (tree/name).symlink_to(target)
        (tree/'bin/chgrp').symlink_to('../sbin/chown')
        (tree/'bin/tar').symlink_to('pax')
        (tree/'bin/cpio').symlink_to('pax')
        (tree/'usr/bin/tar').symlink_to('../../bin/tar')
        (tree/'usr/bin/cpio').symlink_to('../../bin/cpio')
        (tree/'sbin/swapon').symlink_to('swapctl')
        (tree/'usr/bin/uptime').symlink_to('w')
        (tree/'usr/bin/pgrep').symlink_to('pkill')
        (tree/'usr/bin/more').symlink_to('less')
        (tree/'usr/bin/page').symlink_to('less')
        (tree/'usr/bin/ex').symlink_to('vi')
        (tree/'usr/bin/view').symlink_to('vi')
        (tree/'usr/bin/gunzip').symlink_to('gzip')
        (tree/'usr/bin/gzcat').symlink_to('gzip')
        (tree/'usr/bin/zcat').symlink_to('gzip')
        (tree/'usr/bin/bunzip2').symlink_to('bzip2')
        (tree/'usr/bin/bzcat').symlink_to('bzip2')
        (tree/'usr/bin/reset').symlink_to('tset')
        (tree/'usr/bin/vnduncompress').symlink_to('vndcompress')
        rc = '''#!/bin/sh
set -e
/sbin/fsck -p /
/sbin/mount -uw /
/sbin/mount /tmp
/sbin/mount /dev/pts
/sbin/swapctl -A
echo 'NetBSD/RVM native root: writable FFS, native init and shell ready'
'''
        if args.guest_ipv4:
            rc += (f'/sbin/ifconfig rvmnet0 inet {args.guest_ipv4.ip} '
                   f'netmask {args.guest_ipv4.netmask} up\n')
            gateway = args.guest_gateway or args.guest_ipv4.network.network_address + 1
            rc += f'/sbin/route add default {gateway}\n'
            (tree/'etc/resolv.conf').write_text(
                f'nameserver {args.guest_dns or ipaddress.IPv4Address("1.1.1.1")}\n')
        (tree/'etc/rc').write_text(rc)
        (tree/'etc/rc').chmod(0o755)
        (tree/'etc/fstab').write_text('/dev/ld0a / ffs rw 1 1\n'
                                     '/dev/ld0b none swap sw 0 0\n'
                                     'tmpfs /tmp tmpfs rw,-m=1777,-s=16m 0 0\n'
                                     'ptyfs /dev/pts ptyfs rw 0 0\n')
        (tree/'etc/ttys').write_text('console "/usr/libexec/getty std.9600" dumb on secure\n')
        shutil.copyfile(ROOT/'etc/gettytab', tree/'etc/gettytab')
        shutil.copyfile(ROOT/'etc/passwd.conf', tree/'etc/passwd.conf')
        for name in ('hosts', 'networks', 'protocols', 'services', 'rpc',
                     'nsswitch.conf'):
            shutil.copyfile(ROOT/'etc'/name, tree/'etc'/name)
        (tree/'etc/group').write_text('wheel:*:0:root\ntty:*:4:\n')
        (tree/'etc/shells').write_text('/bin/sh\n')
        (tree/'etc/profile').write_text('PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH\n')
        (tree/'etc/motd').write_text('Welcome to the native NetBSD/RVM static userland.\n')
        magic_source = ROOT/'external/bsd/file/dist/magic'
        magic_parts = [b'0\tstring\tFHR\\0\tRVM FHR executable\n',
                       (magic_source/'Header').read_bytes(),
                       (magic_source/'Localstuff').read_bytes()]
        magic_parts.extend(path.read_bytes() for path in sorted((magic_source/'magdir').iterdir())
                           if path.is_file())
        (work/'magic').write_bytes(b'\n'.join(magic_parts))
        subprocess.run([str(host_file), '-C', '-m', str(work/'magic')],
                       cwd=work, check=True, stdout=subprocess.DEVNULL)
        shutil.copyfile(work/'magic.mgc', tree/'usr/share/misc/magic.mgc')
        shutil.copyfile(ROOT/'usr.bin/units/units.lib', tree/'usr/share/misc/units.lib')
        subprocess.run([str(obj/'hosttools/bin/nbtic'), '-ax', '-o',
                        str(tree/'usr/share/misc/terminfo.cdb'),
                        str(ROOT/'share/terminfo/terminfo')], check=True)
        (tree/'etc/login.conf').write_text('default:path=/bin /usr/bin /sbin /usr/sbin:umask=022:\n')
        (tree/'var/run/utmp').touch()
        (tree/'var/run/utmpx').touch()
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
            if path in (tree/'tmp', tree/'var/tmp', tree/'var/tmp/vi.recover'):
                mode = 0o1777
            elif path in (tree/'usr/bin/login', tree/'usr/bin/passwd'):
                mode = 0o4555
            metadata.append(f'{name} type={kind} mode={mode:04o} uid=0 gid=0')
        for name, kind, major, minor, mode in (
                ('console', 'char', 240, 0, '0600'), ('tty', 'char', 241, 0, '0666'),
                ('mem', 'char', 242, 0, '0600'), ('kmem', 'char', 242, 1, '0600'),
                ('null', 'char', 242, 2, '0666'), ('zero', 'char', 242, 12, '0666'),
                ('ptm', 'char', 165, 1, '0666'), ('ptmx', 'char', 165, 0, '0666'),
                ('ld0a', 'block', 303, 0, '0600'), ('rld0a', 'char', 303, 0, '0600'),
                ('ld0b', 'block', 303, 1, '0600'), ('rld0b', 'char', 303, 1, '0600'),
                ('rld0c', 'char', 303, 2, '0600')):
            metadata.append(f'./dev/{name} type={kind} mode={mode} uid=0 gid=0 device=netbsd,{major},{minor}')
        spec = work/'root.spec'
        spec.write_text('\n'.join(metadata)+'\n')
        image = output/'root-native.new'
        subprocess.run([str(makefs), '-t', 'ffs', '-B', 'be', '-s', args.image_size, '-f', '256',
                        '-F', str(spec), '-o', 'version=2,bsize=16384,fsize=2048,density=65536',
                        str(image), str(tree)], check=True)
        add_swap_partition(image, args.swap_mib)
        image.replace(output/'root-native.img')
    for name in ('netbsd.fhr', 'bootloader.bin'):
        shutil.copyfile(kernel/name, output/name)
    if args.pack_all_fhr or args.pack_kernel:
        kernel_image = output/'netbsd.fhr'
        original = kernel_image.read_bytes()
        packed = pack_fhr(original)
        kernel_image.write_bytes(packed)
        packed_count += 1
        packed_original += len(original)
        packed_stored += len(packed)
    config = (ROOT/'sys/arch/rvm/conf/vmemu.conf').read_text()
    config += ('\n[HARDFILE]\n Address = 0x0700000000003000\n'
               ' File = "root-native.img"\n ReadOnly = 0\n End\n')
    if args.tap:
        config += f'\n[NETWORK]\n Tap = "{args.tap}"\n End\n'
    (output/'vm.config').write_text(config)
    print('Native NetBSD/RVM image:', output/'root-native.img')
    if packed_count:
        print(f'Packed {packed_count} FHR executables: {packed_original} -> '
              f'{packed_stored} bytes ({packed_stored / packed_original:.1%})')
    print('Run VMEmu -c vm.config from', output)


if __name__ == '__main__':
    main()
