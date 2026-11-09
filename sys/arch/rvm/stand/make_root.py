#!/usr/bin/env python3
"""Build a disposable big-endian UFS2 root containing the disk-init fixture."""
import argparse
import os
from pathlib import Path, PurePosixPath
import subprocess
import tempfile
import shutil
import sys
sys.dont_write_bytecode = True
from user.build import UserBuilder
from user.shell import build as build_shell


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x8005 if crc & 0x8000 else 0)) & 0xffff
    return crc


def bad_executables(init, directory):
    """Exercise header, integrity, relocation and allocation bounds in exec."""
    original = init.read_bytes()
    bad = bytearray(original)
    bad[12:14] = b'\xff\xff'
    (directory/'bad-header').write_bytes(bad)
    chunks = 16 + 12 * int.from_bytes(original[14:16], 'big')
    bad = bytearray(original)
    bad[chunks + 14] ^= 1
    (directory/'bad-crc').write_bytes(bad)
    bad = bytearray(original)
    bad[16:24] = (64 * 1024 * 1024 + 1).to_bytes(8, 'big')
    (directory/'bad-size').write_bytes(bad)
    size = int.from_bytes(original[16:24], 'big')
    length = int.from_bytes(original[chunks+4:chunks+12], 'big')
    # A valid CRC with an out-of-section ABS64 relocation location.
    data = original[chunks+14:chunks+14+size]
    payload = data + bytes([1, 0, 0, 0xff]) + b'\xff' * 8 + b'\0'
    chunk = (b'COD\0' + (len(payload)+2).to_bytes(8, 'big') +
             crc16(payload).to_bytes(2, 'big') + payload + b'END\0')
    (directory/'bad-reloc').write_bytes(original[:chunks] + chunk +
                                       original[chunks+12+length+4:])
    for path in directory.glob('bad-*'):
        path.chmod(0o755)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, required=True)
    parser.add_argument('--makefs', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--software-float', action='store_true',
                        help='compile C arithmetic using software helpers (FPU context instructions remain enabled)')
    parser.add_argument('--install', nargs=2, action='append', default=[],
                        metavar=('HOST_FILE', 'GUEST_PATH'),
                        help='add a program to the disposable image')
    parser.add_argument('--export-tree', type=Path,
                        help='save the generated tree and adjacent .spec for local image repacking')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[4]
    obj = args.objdir.resolve()
    obj.mkdir(parents=True, exist_ok=True)
    prefix = args.toolchain.resolve() / 'bin/rvm-netbsd-'
    with tempfile.TemporaryDirectory(prefix='root-fixture-', dir=obj) as directory:
        work = Path(directory)
        tree = work / 'root'
        for name in ('dev', 'etc', 'sbin', 'bin', 'usr/bin', 'tmp', 'root'):
            (tree / name).mkdir(parents=True)
        assembly = work / 'init.s'
        subprocess.run([str(prefix)+'gcc', '-nostdinc', '-I', str(root/'sys'),
                        '-E', '-P', '-x', 'assembler-with-cpp',
                        str(root/'sys/arch/rvm/stand/disk_init.S'),
                        '-o', str(assembly)], check=True)
        subprocess.run([str(prefix)+'as', str(assembly), '-o', str(work/'init.o')], check=True)
        init = tree/'sbin/init'
        subprocess.run([str(prefix)+'ld', '-e', '_start', '-o', str(init),
                        str(work/'init.o')], check=True)
        init.chmod(0o755)
        bad_executables(init, tree/'sbin')
        assembly = work/'exec.s'
        subprocess.run([str(prefix)+'gcc', '-nostdinc', '-I', str(root/'sys'),
                        '-E', '-P', '-x', 'assembler-with-cpp',
                        str(root/'sys/arch/rvm/stand/disk_exec.S'),
                        '-o', str(assembly)], check=True)
        subprocess.run([str(prefix)+'as', str(assembly), '-o', str(work/'crt.o')], check=True)
        includes = work/'include'
        includes.mkdir()
        (includes/'machine').symlink_to(root/'sys/arch/rvm/include', target_is_directory=True)
        objects = []
        for source in ('disk_exec', 'tty_check'):
            target = work/(source+'.o')
            subprocess.run(['python3', str(root/'sys/arch/rvm/stand/kernel_cc.py'),
                            '-O2', '-ffreestanding', '-fno-builtin', '-fno-pic',
                            '-fno-pie', '-fno-stack-protector', '-D__NetBSD__',
                            '-D_NETBSD_SOURCE', '-fno-zero-initialized-in-bss', '-nostdinc',
                            '-I', str(root/'lib/libpthread'),
                            '-I', str(includes), '-I', str(root/'sys'), '-c',
                            str(root/'sys/arch/rvm/stand'/(source+'.c')),
                            '-o', str(target)], check=True,
                           env=dict(os.environ, RVM_REAL_CC=str(prefix)+'gcc'))
            objects.append(str(target))
        child = tree/'sbin/exec-check'
        subprocess.run([str(prefix)+'ld', '-e', '_start', '-o', str(child),
                        str(work/'crt.o')] + objects, check=True)
        child.chmod(0o755)
        users = UserBuilder(args.toolchain, work/'user', native_fpu=not args.software_float)
        users.runtime()
        users.program([root/'sys/arch/rvm/stand/user/runtime_check.c',
                       root/'sys/arch/rvm/stand/user/heap_check.c',
                       root/'sys/arch/rvm/stand/user/environment_check.c',
                       root/'sys/arch/rvm/stand/user/platform_check.c',
                       root/'sys/arch/rvm/stand/user/stdio_check.c',
                       root/'sys/arch/rvm/stand/user/context_check.c',
                       root/'sys/arch/rvm/stand/user/jump_check.S',
                       root/'sys/arch/rvm/stand/user/native_check.c',
                       root/'sys/arch/rvm/stand/user/fpu_check.c',
                       root/'sys/arch/rvm/stand/user/math_check.c',
                       root/'sys/arch/rvm/stand/user/cache_probe.S'], tree/'sbin/runtime-check')
        users.program([root/'sys/arch/rvm/stand/user/monitor.c'], tree/'sbin/rvm-monitor')
        users.program([root/'sys/arch/rvm/stand/user/tls_check.c',
                       root/'sys/arch/rvm/stand/user/tls_other.c'], tree/'sbin/tls-check')
        users.program([root/'sys/arch/rvm/stand/user/constructor_check.c',
                       root/'sys/arch/rvm/stand/user/constructor_other.c',
                       root/'sys/arch/rvm/stand/user/constructor_preinit.S'], tree/'sbin/constructor-check')
        users.program([root/'sys/arch/rvm/stand/user/fpu_exec.c'], tree/'sbin/fpu-exec-check')
        users.program([root/'sys/arch/rvm/stand/user/fpu_native_check.c',
                       root/'sys/arch/rvm/stand/user/math_check.c'], tree/'sbin/fpu-native-check',
                      ['-mfpu', '-fno-math-errno'])
        users.program([root/'sys/arch/rvm/stand/user/echo.c'], tree/'bin/rvm-echo')
        users.program([root/'bin/sync/sync.c'], tree/'bin/sync')
        users.program([root/'usr.bin/yes/yes.c'], tree/'usr/bin/yes')
        for name in ('echo', 'cat', 'pwd', 'rm', 'mkdir', 'rmdir', 'sleep'):
            users.program([root/'bin'/name/(name+'.c')], tree/'bin'/name)
        users.program([root/'bin/ls'/(name+'.c') for name in ('cmp', 'ls', 'main', 'print', 'util')],
                      tree/'bin/ls', ['-DSMALL', '-I', str(root/'lib/libutil')])
        build_shell(users, tree/'bin/sh')
        exported = obj/'user'
        exported.mkdir(exist_ok=True)
        for artifact in ('crt0.o', 'end.o', 'librvm.a', 'librvm.init.json'):
            shutil.copyfile(work/'user'/artifact, exported/artifact)
        script = tree/'sbin/exec-script'
        script.write_text('#!/sbin/exec-check\n# Native FHR interpreter/argument integration fixture.\n')
        script.chmod(0o755)
        assembly = work/'bss.s'
        subprocess.run([str(prefix)+'gcc', '-nostdinc', '-I', str(root/'sys'),
                        '-E', '-P', '-x', 'assembler-with-cpp',
                        str(root/'sys/arch/rvm/stand/disk_bss.S'),
                        '-o', str(assembly)], check=True)
        subprocess.run([str(prefix)+'as', str(assembly), '-o', str(work/'bss.o')], check=True)
        bss = tree/'sbin/bss-check'
        subprocess.run([str(prefix)+'ld', '-e', '_start', '-o', str(bss),
                        str(work/'bss.o')], check=True)
        text = bss.read_bytes()
        if int.from_bytes(text[14:16], 'big') != 1:
            raise RuntimeError('BSS fixture requires exactly one input code section')
        header = bytearray(text[:16])
        header[14:16] = (2).to_bytes(2, 'big')
        info = (131072).to_bytes(8, 'big') + (16).to_bytes(2, 'big') + (3).to_bytes(2, 'big')
        bss.write_bytes(header + text[16:28] + info + text[28:] + b'BSS\0END\0')
        bss.chmod(0o755)
        for source, destination in args.install:
            guest = PurePosixPath(destination)
            if not guest.is_absolute() or '..' in guest.parts or str(guest) == '/':
                parser.error('--install requires an absolute guest file path without ..')
            target = tree.joinpath(*guest.parts[1:])
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(Path(source).resolve(), target)
            target.chmod(0o755)
        (tree/'etc/motd').write_text('Welcome to NetBSD/RVM on a real FFS root filesystem.\n')
        (tree/'etc/passwd').write_text('root:*:0:0:System administrator:/root:/bin/sh\n')
        (tree/'etc/group').write_text('wheel:*:0:root\n')
        (tree/'etc/shells').write_text('/bin/sh\n')
        shutil.copyfile(root/'sys/arch/rvm/stand/user/sh-check.sh', tree/'etc/sh-check')
        spec = work/'root.spec'
        metadata = ['. type=dir mode=0755 uid=0 gid=0']
        for path in sorted(tree.rglob('*')):
            kind = 'dir' if path.is_dir() else 'file'
            mode = '0755' if path.is_dir() or path.stat().st_mode & 0o111 else '0644'
            if path == tree/'tmp':
                mode = '1777'
            metadata.append(f'./{path.relative_to(tree)} type={kind} mode={mode} uid=0 gid=0')
        metadata.append('./dev/console type=char mode=0600 uid=0 gid=0 device=netbsd,240,0')
        metadata.append('./dev/tty type=char mode=0666 uid=0 gid=0 device=netbsd,241,0')
        metadata.append('./dev/null type=char mode=0666 uid=0 gid=0 device=netbsd,242,2')
        metadata.append('./dev/zero type=char mode=0666 uid=0 gid=0 device=netbsd,242,12')
        spec.write_text('\n'.join(metadata) + '\n')
        if args.export_tree is not None:
            exported_tree = args.export_tree.resolve()
            shutil.copytree(tree, exported_tree)
            shutil.copyfile(spec, str(exported_tree)+'.spec')
        image = work/'root-fixture.img'
        subprocess.run([str(args.makefs.resolve()), '-t', 'ffs', '-B', 'be',
                        '-s', '16m', '-f', '256', '-F', str(spec), '-o',
                        'version=2,bsize=16384,fsize=2048', str(image), str(tree)], check=True)
        image.replace(obj/'root-fixture.img')
    config = (root/'sys/arch/rvm/conf/vmemu.conf').read_text()
    config += ('\n[HARDFILE]\n Address = 0x0700000000003000\n'
               ' File = "root-fixture.img"\n ReadOnly = 0\n End\n')
    (obj/'vm.config').write_text(config)
    (obj/'vm.diskroot.config').write_text(config)
    print('Created disposable root fixture:', obj/'root-fixture.img')


if __name__ == '__main__':
    main()
