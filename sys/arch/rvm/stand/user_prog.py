#!/usr/bin/env python3
"""Static FHR program link command for normal bsd.prog.mk builds."""
import argparse
from pathlib import Path
import sys
from types import SimpleNamespace

sys.dont_write_bytecode = True
from user_link import link


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--sysroot', type=Path, required=True)
    parser.add_argument('-o', '--output', type=Path, required=True)
    parser.add_argument('-static', action='store_true')
    parser.add_argument('-l', dest='libraries', action='append', default=[])
    parser.add_argument('-L', dest='search', type=Path, action='append', default=[])
    parser.add_argument('objects', nargs='+')
    # bsd.prog.mk can interleave object files, direct archive paths, and -l
    # flags in LDADD. argparse's default parser rejects such positional paths.
    args = parser.parse_intermixed_args()
    prefix = args.toolchain.resolve()/'bin/rvm-netbsd-'
    lib = args.sysroot.resolve()/'usr/lib'
    objects = [str(lib/'crt0.o')] + args.objects + [str(lib/'crtbegin.o')]
    libraries = args.libraries + ([] if 'c' in args.libraries else ['c'])
    for name in libraries:
        found = next((directory/('lib'+name+'.a') for directory in args.search+[lib]
                      if (directory/('lib'+name+'.a')).exists()), None)
        if found is None:
            raise SystemExit('Missing native static library: '+name)
        objects.append(str(found))
    link(SimpleNamespace(cc=str(prefix)+'gcc', ld=str(prefix)+'ld', nm=str(prefix)+'nm',
                         ar=str(prefix)+'ar', end=lib/'crtend.o',
                         output=args.output, objects=objects))


if __name__ == '__main__':
    main()
