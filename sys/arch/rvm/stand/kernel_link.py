#!/usr/bin/env python3
"""Link a static FHR kernel, resolving weak binding before invoking ld.

FHR REL0 records do not carry ELF weak/local binding. kernel_cc.py scopes
local definitions and records weak definitions. Select strong definitions in
preference to weak ones, then the first weak definition in object order. Only
shadowed weak definitions are renamed/removed in temporary object copies.
Library members are included explicitly; this kernel uses the full libkern.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def shadow(source, names, prefix):
    output = []
    for line in source.splitlines(keepends=True):
        if re.match(r'\s*\.(?:global|globl)\s+(' +
                    '|'.join(map(re.escape, sorted(names))) + r')\s*$', line):
            continue
        if any(re.match(r'\s*' + re.escape(name) + r'\s*=', line)
               for name in names):
            continue
        if any(re.match(r'\s*\.set\s+' + re.escape(name) + r'\s*,', line)
               for name in names):
            continue
        if re.match(r'\s*\.(?:type|size)\s', line):
            for name in names:
                line = re.sub(r'(?<![\w.])' + re.escape(name) + r'(?![\w.])',
                              prefix + name, line)
        else:
            for name in names:
                line = re.sub(r'^(\s*)' + re.escape(name) + r':',
                              r'\1' + prefix + name + ':', line)
        output.append(line)
    return ''.join(output)


def link(args):
    with tempfile.TemporaryDirectory(prefix='rvm-kernel-link-') as temporary:
        work = Path(temporary)
        objects = []
        origins = []
        for value in args.objects:
            path = Path(value).resolve()
            if path.suffix == '.a':
                members = subprocess.check_output([args.ar, 't', str(path)],
                                                  text=True).splitlines()
                if len(set(members)) != len(members):
                    raise SystemExit('Duplicate archive member names: ' + str(path))
                for member in members:
                    target = work / (str(len(objects)) + '.o')
                    target.write_bytes(subprocess.check_output(
                        [args.ar, 'p', str(path), member]))
                    objects.append(target)
                    origins.append(path.parent / member)
            else:
                objects.append(path)
                origins.append(path)
        weak = []
        definitions = []
        strong = set()
        for path, origin in zip(objects, origins):
            metadata = origin.with_suffix('.weak.json')
            names = set(json.loads(metadata.read_text())) if metadata.exists() else set()
            symbols = subprocess.check_output([args.nm, '-P', '-g',
                                               '--defined-only', str(path)], text=True)
            defined = {line.split()[0] for line in symbols.splitlines() if line.strip()}
            weak.append(names)
            definitions.append(defined)
            strong.update(defined - names)
        selected = set(strong)
        for index, (path, origin, names) in enumerate(zip(objects, origins, weak)):
            hidden = names & selected
            selected.update(names)
            if not hidden:
                continue
            assembly = origin.with_suffix('.fhr.s')
            if not assembly.exists():
                raise SystemExit('Missing assembly for weak resolution: ' + str(assembly))
            rewritten = work / (str(index) + '-resolved.s')
            rewritten.write_text(shadow(assembly.read_text(), hidden,
                                       'rvm_shadow_' + str(index) + '_'))
            target = rewritten.with_suffix('.o')
            subprocess.run([args.cc, '-c', str(rewritten), '-o', str(target)], check=True)
            objects[index] = target
        subprocess.run([args.ld, '-e', args.entry, '-Map', str(args.output) + '.map',
                        '-o', str(args.output)] + list(map(str, objects)), check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--ld', required=True)
    parser.add_argument('--nm', required=True)
    parser.add_argument('--ar', required=True)
    parser.add_argument('--entry', default='_start')
    parser.add_argument('-o', '--output', type=Path, required=True)
    parser.add_argument('objects', nargs='+')
    args = parser.parse_args()
    link(args)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        print('FHR kernel link failed with status ' + str(error.returncode), file=sys.stderr)
        sys.exit(error.returncode)
