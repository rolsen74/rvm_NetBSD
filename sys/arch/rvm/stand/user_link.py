#!/usr/bin/env python3
"""Link static FHR userland with lazy archives, weak binding and constructors.

FHR loses weak symbol flags and has no init-array linker support. Resolve
archive members before linking, then assemble temporary copies only where a
weak definition is overridden or an undefined weak reference must become zero.
Keep each library's .fhr.s/.weak.json/.init.json files beside its archive.
"""
import argparse
from dataclasses import dataclass, field
import json
import os
import shutil
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from kernel_link import shadow
from static_init import registry


@dataclass
class Object:
    origin: Path
    archive: Path | None = None
    definitions: set = field(default_factory=set)
    undefined: set = field(default_factory=set)

    def metadata(self, suffix, default):
        path = self.origin.with_suffix(suffix)
        return json.loads(path.read_text()) if path.exists() else default

    @property
    def weak(self):
        # A source can declare aliases to functions implemented in another
        # translation unit. GAS omits such aliases from this object; they
        # must not suppress a later object's real weak definition.
        return set(self.metadata('.weak.json', [])) & self.definitions

    @property
    def weakrefs(self):
        path = self.origin.with_suffix('.fhr.s')
        if not path.exists():
            return {}
        return dict(re.findall(r'^\s*\.weakref\s+([\w.]+),\s*([\w.]+)',
                               path.read_text(), re.M))

    @property
    def required(self):
        # Undefined weak references do not cause archive extraction.
        optional = set(self.weakrefs.values())
        if optional:
            source = re.sub(r'^\s*\.weakref[^\n]*', '',
                            self.origin.with_suffix('.fhr.s').read_text(), flags=re.M)
            optional = {name for name in optional
                        if not re.search(r'(?<![\w.])'+re.escape(name)+r'(?![\w.])', source)}
        return self.undefined - optional


def symbols(args, path):
    output = subprocess.check_output([args.nm, '-P', '-A', '-g', str(path)], text=True)
    if path.suffix == '.a':
        members = subprocess.check_output([args.ar, 't', str(path)], text=True).splitlines()
        if len(set(members)) != len(members):
            raise SystemExit('Duplicate archive member names: '+str(path))
        objects = {name: Object(path.parent/name, path) for name in members}
    else:
        objects = {None: Object(path)}
    for line in output.splitlines():
        match = re.match(re.escape(str(path)) + r'(?:\[([^]]+)\])?:\s+(\S+)\s+(\S)', line)
        if not match:
            raise SystemExit('Unrecognized nm record: '+line)
        member, name, kind = match.groups()
        obj = objects[member]
        (obj.undefined if kind == 'U' else obj.definitions).add(name)
    return list(objects.values())


def link(args):
    roots, archives = [], []
    for name in args.objects:
        path = Path(name).resolve()
        objects = symbols(args, path)
        if path.suffix == '.a':
            archives.extend(objects)
        else:
            roots.extend(objects)
    selected = list(roots)
    available = list(archives)
    generated = set.union(set(), *(obj.definitions for obj in symbols(args, args.end.resolve()))) | {'__rvm_run_init', '__rvm_run_preinit',
                 '__rvm_run_constructors', '__rvm_run_fini', '__rvm_register_fini'}
    while True:
        definitions = set.union(generated, *(obj.definitions for obj in selected))
        required = set.union(set(), *(obj.required for obj in selected)) - definitions
        candidates = [obj for obj in available if obj.definitions & required]
        if not candidates:
            break
        # Prefer a strong provider to a weak fallback within the archives.
        obj = next((obj for obj in candidates
                    if (obj.definitions - obj.weak) & required), candidates[0])
        selected.append(obj)
        available.remove(obj)
    if required:
        raise SystemExit('Unresolved static user symbols:\n  '+'\n  '.join(sorted(required)))
    definitions = set.union(generated, *(obj.definitions for obj in selected))
    strong = set.union(set(), *(obj.definitions - obj.weak for obj in selected))
    seen = set(strong)
    with tempfile.TemporaryDirectory(prefix='rvm-user-link-') as temporary:
        work = Path(temporary)
        objects, callbacks = [], []
        for index, obj in enumerate(selected):
            weak = obj.weak
            hidden = weak & seen
            seen.update(weak)
            absent = {alias: name for alias, name in obj.weakrefs.items()
                      if name not in definitions}
            if hidden or absent:
                assembly = obj.origin.with_suffix('.fhr.s')
                if not assembly.exists():
                    raise SystemExit('Missing assembly for weak binding: '+str(assembly))
                source = assembly.read_text()
                if hidden:
                    source = shadow(source, hidden, f'__rvm_shadow_{index}_')
                if absent:
                    source = re.sub(r'^\s*\.weakref\s+([^,]+),[^\n]*\n',
                                    lambda m: '' if m[1].strip() in absent else m[0],
                                    source, flags=re.M)
                    for alias in absent:
                        # GAS cannot parse LEA with a numeric zero address.
                        # Materialize a null pointer in the destination register.
                        source = re.sub(r'(?m)^(\s*)lea\s+'+re.escape(alias)+
                                        r',\s*(r[0-9]+)\s*$',
                                        r'\1move.q #0,\2', source)
                        source = re.sub(r'(?m)^(\s*\.quad\s+)'+re.escape(alias)+r'\s*$',
                                        r'\g<1>0', source)
                        if re.search(r'(?<![\w.])'+re.escape(alias)+r'(?![\w.])', source):
                            raise SystemExit('Unsupported undefined weak use: '+alias)

                rewritten = work/f'{index}.s'
                rewritten.write_text(source)
                target = rewritten.with_suffix('.o')
                try:
                    subprocess.run([args.cc, '-c', str(rewritten), '-o', str(target)], check=True)
                except subprocess.CalledProcessError:
                    failure = args.output.with_suffix('.failed.s')
                    failure.write_text('/* '+str(obj.origin)+' */\n'+source)
                    print('Failed assembly saved to '+str(failure), file=sys.stderr)
                    raise
            elif obj.archive:
                target = work/f'{index}.o'
                target.write_bytes(subprocess.check_output(
                    [args.ar, 'p', str(obj.archive), obj.origin.name]))
            else:
                target = obj.origin
            objects.append(target)
            callbacks.extend(obj.metadata('.init.json', []))
        source = work/'initializers.c'
        source.write_text(registry(callbacks, native_exit=True))
        target = source.with_suffix('.o')
        subprocess.run([sys.executable, str(Path(__file__).with_name('user_cc.py')),
                        '-O2', '-ffreestanding', '-c', str(source), '-o', str(target)],
                       check=True, env=dict(os.environ, RVM_REAL_CC=args.cc))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        executable = work/'program.fhr'
        mapfile = work/'program.map'
        subprocess.run([args.ld, '-e', '_start', '-Map', str(mapfile),
                        '-o', str(executable)] + list(map(str, objects)) +
                       [str(target), str(args.end)], check=True)
        if not executable.exists() or executable.read_bytes()[:4] != b'FHR\0':
            raise SystemExit('The FHR linker did not produce an executable')
        mapping = mapfile.read_text()
        entry = re.search(r'^\s+0x([0-9a-f]+)\s+_start\s*$', mapping, re.M)
        if entry is None or int(entry[1], 16) != 0:
            raise SystemExit('FHR _start must be the first byte of code')
        staged = Path(str(args.output)+'.new')
        shutil.copyfile(executable, staged)
        staged.chmod(0o755)
        staged.replace(args.output)
        Path(str(args.output)+'.map').write_text(mapping)
        Path(str(args.output)+'.objects.json').write_text(json.dumps([
            {'object': str(obj.origin), 'archive': str(obj.archive) if obj.archive else None}
            for obj in selected], indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cc', 'ld', 'nm', 'ar'):
        parser.add_argument('--'+name, required=True)
    parser.add_argument('--end', type=Path, required=True)
    parser.add_argument('-o', '--output', type=Path, required=True)
    parser.add_argument('objects', nargs='+')
    link(parser.parse_args())


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
