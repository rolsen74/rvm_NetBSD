#!/usr/bin/env python3
"""Compile normal kernel/libkern C with the current FHR assembler adapters.

FHR relocatable symbols do not preserve local/weak binding. Give file-local
definitions unique names and record weak definitions for kernel_link.py, which
selects them after seeing all objects. This supports static FHR binaries.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
from fhr_asm import normalize, raw_multiply_register


def private_prefix(target):
    # Different libraries/programs routinely contain equally named objects.
    # FHR promotes their local labels, so the full object identity is needed.
    identity = hashlib.sha256(str(target.resolve()).encode()).hexdigest()[:16]
    return '__rvm_local_' + re.sub(r'\W', '_', target.stem) + '_' + identity + '__'


def adapt(source, prefix):
    # Preserve userland copyright/revision strings in ordinary read-only
    # data. FHR has no ELF metadata section stack; these strings have no
    # referenced labels, so collect them at the end without changing code.
    metadata = []
    def collect(match):
        metadata.append(match[1])
        return ''
    source = re.sub(r'^\s*\.pushsection \.(?:copyright|ident),[^\n]*\n'
                    r'((?:[ \t]*\.asci[iz][^\n]*\n)+)[ \t]*\.popsection[^\n]*\n',
                    collect, source, flags=re.M)
    if metadata:
        source += '\n.section .rodata\n' + ''.join(metadata)
    # These sections carry link-time deprecation messages, not runtime data.
    # GAS/FHR has no .pushsection or ELF .gnu.warning handling.
    source = re.sub(r'^\s*\.pushsection \.gnu\.warning\.[^\n]+\n'
                    r'(?:\.ascii [^\n]+\n)+\.popsection\s*\n', '', source,
                    flags=re.M)
    public = set(re.findall(r'^\s*\.(?:globl|global|weak)\s+([\w.]+)',
                            source, re.M))
    # .comm is externally visible unless explicitly paired with .local.
    # __attribute__((common)) is still emitted with -fno-common.
    common = set(re.findall(r'^\s*\.comm\s+([\w.]+)', source, re.M))
    local = set(re.findall(r'^\s*\.local\s+([\w.]+)', source, re.M))
    public.update(common - local)
    weak = set(re.findall(r'^\s*\.weak\s+([\w.]+)', source, re.M))
    defined = set(re.findall(r'^\s*([\w.]+):', source, re.M))
    defined.update(re.findall(r'^\s*\.comm\s+([\w.]+)', source, re.M))
    defined.update(re.findall(r'^\s*([\w.]+)\s*=', source, re.M))
    defined.update(re.findall(r'^\s*\.set\s+([\w.]+)\s*,', source, re.M))
    private = {name: prefix + name for name in defined - public
               if not name.startswith('.L')}
    output = []
    for line in source.splitlines(keepends=True):
        if re.match(r'\s*\.hidden\s', line):
            continue  # Static kernels have no dynamic symbol visibility.
        line = re.sub(r'^(\s*)\.weak\s', r'\1.global ', line)
        if not line.lstrip().startswith(('.string', '.ascii', '.asciz')):
            line = re.sub(r'(?<![\w.])([A-Za-z_][\w.]*)(?![\w.])',
                          lambda m: private.get(m[0], m[0]), line)
        output.append(line)
    return ''.join(output), sorted(weak & defined)


def main():
    cc = os.environ.get('RVM_REAL_CC', 'rvm-netbsd-gcc')
    args = sys.argv[1:]
    sources = [x for x in args if x.endswith('.c') and not x.startswith('-')]
    if '-c' not in args or len(sources) != 1 or '-o' not in args:
        os.execvp(cc, [cc] + args)
    target = Path(args[args.index('-o') + 1])
    raw = target.with_suffix('.raw.s')
    # Avoid foo.s: BSD make may then select its .s.o suffix rule instead
    # of recompiling foo.c on the next invocation.
    assembly = target.with_suffix('.fhr.s')
    flags = list(args)
    flags[flags.index('-c')] = '-S'
    flags[flags.index('-o') + 1] = str(raw)
    subprocess.run([cc] + flags, check=True)
    prefix = private_prefix(target)
    source, weak = adapt(raw.read_text(), prefix)
    if os.environ.get('RVM_USER_INITIALIZERS') == '1':
        from static_init import extract
        source, entries = extract(source)
        target.with_suffix('.init.json').write_text(json.dumps(entries)+'\n')
    assembly.write_text(''.join(normalize(source, prefix + 'lc',
                               raw_multiply_register(cc))))
    subprocess.run([cc, '-c', str(assembly), '-o', str(target)], check=True)
    target.with_suffix('.weak.json').write_text(json.dumps(weak) + '\n')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
