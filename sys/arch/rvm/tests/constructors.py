#!/usr/bin/env python3
"""Check constructor selection from static archives in a real FHR process."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT/'sys/arch/rvm/stand'))
from user.build import UserBuilder


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, default=ROOT/'obj.rvm-diskroot')
    parser.add_argument('--toolchain', type=Path, default=ROOT/'../rvm_gcc/rvm_cross_netbsd')
    parser.add_argument('--vmemu', default='VMEmu')
    args = parser.parse_args()
    obj = args.objdir.resolve()
    with tempfile.TemporaryDirectory(prefix='rvm-constructors-') as directory:
        work = Path(directory)
        builder = UserBuilder(args.toolchain, work/'user')
        for name in ('crt0.o', 'end.o', 'librvm.a', 'librvm.init.json'):
            shutil.copyfile(obj/'user'/name, builder.obj/name)
        used = work/'archive-used.c'
        used.write_text('''
#include <stdio.h>
static int initialized;
int archive_value(void) { return initialized; }
__attribute__((constructor(150))) static void initialize(void) { initialized = 41; }
__attribute__((destructor(150))) static void finalize(void) {
    puts("NetBSD/RVM: selected archive constructor/destructor verified");
}
''')
        unused = work/'archive-unused.c'
        unused.write_text('''
#include <unistd.h>
__attribute__((constructor)) static void must_not_run(void) { _exit(166); }
int unreferenced(void) { return 99; }
''')
        manifest = builder.obj/'librvm.init.json'
        catalog = json.loads(manifest.read_text())
        for source in (used, unused):
            target = builder.obj/(source.stem+'.o')
            builder.compile(source, target)
            subprocess.run([builder.prefix+'ar', 'rs', str(builder.obj/'librvm.a'), str(target)], check=True)
            catalog[target.name] = json.loads(target.with_suffix('.init.json').read_text())
        manifest.write_text(json.dumps(catalog)+'\n')
        main_source = work/'archive-main.c'
        main_source.write_text('''
#include <stdio.h>
extern int archive_value(void);
int main(void) {
    if (archive_value() != 41) return 166;
    puts("NetBSD/RVM: archive initialization before main verified");
    return 0;
}
''')
        program = work/'archive-check'
        builder.program([main_source], program)
        mapping = (builder.obj/(program.name+'.map')).read_text()
        assert 'must_not_run' not in mapping and 'unreferenced' not in mapping, mapping
        installs = ['--install', str(program), '/bin/archive-check']
        commands = ['--command', 'run /bin/archive-check',
                    'archive initialization before main verified\n'
                    'NetBSD/RVM: selected archive constructor/destructor verified\nstatus=0']
        for optimization in ('-O0', '-O2', '-Os'):
            name = 'constructors-'+optimization[1:]
            target = work/name
            builder.program([ROOT/'sys/arch/rvm/stand/user'/source for source in
                             ('constructor_check.c', 'constructor_other.c', 'constructor_preinit.S')],
                            target, [optimization])
            installs += ['--install', str(target), '/bin/'+name]
            commands += ['--command', 'run /bin/'+name,
                         'preinit, constructor priorities, destructors, atexit and recursive exit verified\nstatus=0']
        for name in ('netbsd.fhr', 'bootloader.bin'):
            shutil.copyfile(obj/name, work/name)
        subprocess.run([sys.executable, str(ROOT/'sys/arch/rvm/stand/make_root.py'),
                        '--objdir', str(work), '--makefs', str(obj/'hosttools/bin/nbmakefs'),
                        '--toolchain', str(args.toolchain.resolve())] + installs, check=True)
        try:
            subprocess.run([sys.executable, str(Path(__file__).with_name('console.py')),
                            '--objdir', str(work), '--vmemu', args.vmemu] + commands, check=True)
        finally:
            if (work/'console-keyboard.log').exists():
                shutil.copyfile(work/'console-keyboard.log', obj/'constructors-archive.log')
    print('PASS: static initialization at -O0/-O2/-Os, selected archive members and unused-member exclusion')


if __name__ == '__main__':
    main()
