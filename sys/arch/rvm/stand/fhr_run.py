#!/usr/bin/env python3
"""Start VMEmu, expanding a packed boot FHR into a temporary image directory."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

from fhr_pack import unpack


def main():
    if len(sys.argv) < 2:
        raise SystemExit('usage: fhr_run.py IMAGE_DIRECTORY [VMEmu options]')
    image = Path(sys.argv[1]).resolve()
    kernel = image / 'netbsd.fhr'
    with kernel.open('rb') as stream:
        header = stream.read(16)
    if len(header) != 16 or header[:4] != b'FHR\0':
        raise SystemExit('invalid boot FHR header')
    command = [os.environ.get('VMEMU', 'VMEmu'), '-c', 'vm.config',
               *sys.argv[2:]]
    if header[7] == 0:
        os.chdir(image)
        os.execvp(command[0], command)
    if header[7] != 1:
        raise SystemExit('unsupported boot FHR packer')
    with tempfile.TemporaryDirectory(prefix='rvm-packed-boot-') as directory:
        staged = Path(directory)
        for path in image.iterdir():
            if path.name != kernel.name:
                (staged / path.name).symlink_to(path)
        (staged / kernel.name).write_bytes(unpack(kernel.read_bytes()))
        print(f'Expanded packed kernel: {kernel}', flush=True)
        return subprocess.call(command, cwd=staged)


if __name__ == '__main__':
    sys.exit(main())
