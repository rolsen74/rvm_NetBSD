#!/usr/bin/env python3
"""Check FHR relocatable linking, the first stage of native shared objects."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args):
    return subprocess.run([str(arg) for arg in args], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, required=True)
    args = parser.parse_args()
    tools = args.toolchain.resolve() / 'bin'
    cc = tools / 'rvm-netbsd-gcc'
    ld = tools / 'rvm-netbsd-ld'
    nm = tools / 'rvm-netbsd-nm'
    objdump = tools / 'rvm-netbsd-objdump'

    with tempfile.TemporaryDirectory(prefix='rvm-fhr-partial-') as directory:
        work = Path(directory)
        (work / 'first.c').write_text(
            'int shared_value = 5;\n'
            'char zero[8192];\n'
            'int first(int x) { return x + shared_value + zero[8191]; }\n')
        (work / 'second.c').write_text(
            'extern int shared_value;\n'
            'int second(void) { return shared_value + 2; }\n')
        (work / 'import.c').write_text(
            'extern int imported(int);\n'
            'int third(int x) { return imported(x); }\n')
        for name in ('first', 'second', 'import'):
            run(cc, '-O2', '-c', work / (name + '.c'),
                '-o', work / (name + '.o'))

        combined = work / 'combined.o'
        run(ld, '-r', work / 'first.o', work / 'second.o',
            work / 'import.o', '-o', combined)
        symbols = run(nm, combined)
        for name in ('first', 'second', 'third', 'shared_value', 'zero'):
            assert any(line.endswith(' ' + name) and ' U ' not in line
                       for line in symbols.splitlines()), symbols
        assert any(line.endswith(' U imported') for line in symbols.splitlines()), symbols
        relocations = run(objdump, '-r', combined)
        assert 'RVM_FHR_ABS64' in relocations and 'imported' in relocations
        assert '.data' in relocations, relocations

        shared = work / 'libexample.so'
        run(sys.executable, Path(__file__).resolve().parents[1] / 'stand/fhr_so.py',
            '--ld', ld, '-o', shared, work / 'first.o', work / 'second.o',
            work / 'import.o')
        image = shared.read_bytes()
        assert image[:8] == b'FHR\0\x01\x01\x02\0' and image[14:16] != b'\0\0'
        assert 'zero' in run(nm, shared) and 'imported' in run(objdump, '-r', shared)
        sections = run(objdump, '-h', shared)
        assert '.bss' in sections and '00002000' in sections, sections

        # A self-contained partial link must still be accepted by the
        # executable linker; unresolved library imports must be rejected.
        local = work / 'local.o'
        run(ld, '-r', work / 'first.o', work / 'second.o', '-o', local)
        run(ld, '-e', 'first', local, '-o', work / 'local.fhr')
        assert (work / 'local.fhr').stat().st_size > 16
        bad = subprocess.run([str(ld), '-e', 'first', str(combined),
                              '-o', str(work / 'unresolved.fhr')],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert bad.returncode != 0
        assert not (work / 'unresolved.fhr').exists()

    print('PASS: FHR partial links and shared images retain exports and relocations')


if __name__ == '__main__':
    main()
