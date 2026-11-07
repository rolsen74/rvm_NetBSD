#!/usr/bin/env python3
"""Build a disposable big-endian UFS2 root containing the disk-init fixture."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


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
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[4]
    obj = args.objdir.resolve()
    obj.mkdir(parents=True, exist_ok=True)
    prefix = args.toolchain.resolve() / 'bin/rvm-netbsd-'
    with tempfile.TemporaryDirectory(prefix='root-fixture-', dir=obj) as directory:
        work = Path(directory)
        tree = work / 'root'
        for name in ('dev', 'etc', 'sbin'):
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
        subprocess.run(['python3', str(root/'sys/arch/rvm/stand/kernel_cc.py'),
                        '-O2', '-ffreestanding', '-fno-builtin', '-fno-pic',
                        '-fno-pie', '-fno-stack-protector',
                        '-fno-zero-initialized-in-bss', '-nostdinc',
                        '-I', str(root/'sys'), '-c',
                        str(root/'sys/arch/rvm/stand/disk_exec.c'),
                        '-o', str(work/'exec.o')], check=True,
                       env=dict(os.environ, RVM_REAL_CC=str(prefix)+'gcc'))
        child = tree/'sbin/exec-check'
        subprocess.run([str(prefix)+'ld', '-e', '_start', '-o', str(child),
                        str(work/'crt.o'), str(work/'exec.o')], check=True)
        child.chmod(0o755)
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
        (tree/'etc/motd').write_text('Welcome to NetBSD/RVM on a real FFS root filesystem.\n')
        spec = work/'root.spec'
        metadata = ['. type=dir mode=0755 uid=0 gid=0']
        for path in sorted(tree.rglob('*')):
            kind = 'dir' if path.is_dir() else 'file'
            mode = '0755' if path.is_dir() or path.parent.name == 'sbin' else '0644'
            metadata.append(f'./{path.relative_to(tree)} type={kind} mode={mode} uid=0 gid=0')
        metadata.append('./dev/console type=char mode=0600 uid=0 gid=0 device=netbsd,240,0')
        spec.write_text('\n'.join(metadata) + '\n')
        image = work/'root-fixture.img'
        subprocess.run([str(args.makefs.resolve()), '-t', 'ffs', '-B', 'be',
                        '-s', '16m', '-F', str(spec), '-o',
                        'version=2,bsize=16384,fsize=2048', str(image), str(tree)], check=True)
        image.replace(obj/'root-fixture.img')
    config = (root/'sys/arch/rvm/conf/vmemu.conf').read_text()
    config += ('\n[HARDFILE]\n Address = 0x0700000300000000\n'
               ' File = "root-fixture.img"\n ReadOnly = 0\n End\n')
    (obj/'vm.config').write_text(config)
    (obj/'vm.diskroot.config').write_text(config)
    print('Created disposable root fixture:', obj/'root-fixture.img')


if __name__ == '__main__':
    main()
