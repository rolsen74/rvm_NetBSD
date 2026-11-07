#!/usr/bin/env python3
"""Verify that FHR linker errors are diagnostics rather than host crashes."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cross', default='rvm-netbsd-')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='rvm-link-errors-') as directory:
    root = Path(directory)
    for name, source in (
        ('call', '.text\n.global _start\n_start:\n jsr rvm_missing_call\n rts\n'),
        ('data', '.text\n.global _start\n_start:\n rts\n.data\n.quad rvm_missing_data\n'),
    ):
        src, obj, exe = (root / (name + ext) for ext in ('.s', '.o', '.fhr'))
        src.write_text(source)
        subprocess.run([args.cross + 'gcc', '-nostdinc', '-c', str(src), '-o', str(obj)], check=True)
        result = subprocess.run([args.cross + 'ld', '-e', '_start', '-o', str(exe), str(obj)],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        output = result.stdout + result.stderr
        assert result.returncode == 1, f'linker returned {result.returncode}: {output}'
        assert 'rvm_missing_' + name in output and 'undefined' in output.lower(), output
        assert not exe.exists(), 'failed link retained an output executable'
        print('PASS: FHR unresolved ' + name + ' relocation reports a diagnostic without crashing', flush=True)
