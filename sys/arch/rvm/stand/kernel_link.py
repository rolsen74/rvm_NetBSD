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


def split_bss(source):
    """Separate staging storage before assembling FHR objects with one data section."""
    code, storage = [], []
    active = False
    for line in source.splitlines(keepends=True):
        section = re.match(r'\s*\.(text|data|section)\b(.*)', line)
        if section:
            active = section[1] == 'section' and section[2].strip() == '.rvm.bss'
            if active:
                line = '\t.data\n'
        (storage if active else code).append(line)
    names = set(re.findall(r'^([\w.]+):', ''.join(storage), re.M))
    # Private names already have per-object prefixes. Export them between
    # these temporary objects without changing their visibility elsewhere.
    source = re.sub(r'^\s*\.local[^\n]*\n', '', ''.join(code), flags=re.M)
    data = re.sub(r'^\s*\.local[^\n]*\n', '', ''.join(storage), flags=re.M)
    data = '.data\n' + ''.join('.global '+name+'\n' for name in sorted(names)) + data
    return source, data if names else None


def section_script(link_sets, storage):
    """Merge runtime tables while retaining each NetBSD linker-set range."""
    lines = ['SECTIONS {', '. = 0;',
             '.text : { *(.text .text.*) }',
             '.data : { __rvm_data_base = .;',
             '*(EXCLUDE_FILE (' + ' '.join(json.dumps(str(p) + '*') for p in storage) +
             ') .data .data.*)' if storage else '*(.data .data.*)']
    for name in sorted(link_sets):
        lines += ['. = ALIGN(8);', f'__start_{name} = .;',
                  f'KEEP(*({name}))', f'__stop_{name} = .;']
    # FHR GAS retains quotes in the version note's input section name.
    lines += ['*(link_set_*)',
              '*(.note.netbsd.ident ?.note.netbsd.ident?)', '. += 8;', '}',
              '.rodata : { __rvm_rodata_base = .; *(.rodata .rodata.*) . += 8; }',
              '.bss (NOLOAD) : { __rvm_bss_base = .;']
    for path in storage:
        # A filename wildcard prevents implicit loading ahead of _start.
        lines.append(f'{json.dumps(str(path) + "*")}(.data)')
    lines += ['*(.rvm.bss .bss .bss.*) *(COMMON)', '. += 8;', '}', '}']
    return '\n'.join(lines) + '\n'


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
        link_sets = set()
        for path, origin in zip(objects, origins):
            metadata = origin.with_suffix('.weak.json')
            names = set(json.loads(metadata.read_text())) if metadata.exists() else set()
            symbols = subprocess.check_output([args.nm, '-P', '-g', str(path)], text=True)
            records = [line.split() for line in symbols.splitlines() if line.strip()]
            defined = {record[0] for record in records if record[1] != 'U'}
            link_sets.update(match[1] for record in records
                             if (match := re.fullmatch(r'__(?:start|stop)_(link_set_\w+)', record[0])))
            weak.append(names & defined)
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
        storage = []
        for index, (path, origin) in enumerate(zip(objects, origins)):
            assembly = (path.with_suffix('.s') if path.parent == work
                        and path.with_suffix('.s').exists() else origin.with_suffix('.fhr.s'))
            if not assembly.exists():
                continue
            code, zero = split_bss(assembly.read_text())
            if zero is None:
                continue
            for kind, source in (('code', code), ('bss', zero)):
                rewritten = work / f'{index}-{kind}.s'
                rewritten.write_text(source)
                target = rewritten.with_suffix('.o')
                subprocess.run([args.cc, '-c', str(rewritten), '-o', str(target)], check=True)
                if kind == 'code':
                    objects[index] = target
                else:
                    storage.append(target)
        objects.extend(storage)
        script = work / 'kernel.ld'
        script.write_text(section_script(link_sets, storage))
        # The current RVM linker cannot relax the larger INET kernel.
        subprocess.run([args.ld, '--no-relax', '-T', str(script), '-e', args.entry, '-Map', str(args.output) + '.map',
                        '-o', str(args.output)] + list(map(str, objects)), check=True)
        # FHR executables do not encode e_entry. The loader starts at the
        # first code byte, even when ld was passed a different -e symbol.
        mapping = Path(str(args.output) + '.map').read_text()
        entry = re.search(r'^\s+0x([0-9a-f]+)\s+' + re.escape(args.entry) +
                          r'\s*$', mapping, re.M)
        if entry is None or int(entry[1], 16) != 0:
            raise SystemExit('FHR entry must be first in .text; put kernel_locore first')


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
