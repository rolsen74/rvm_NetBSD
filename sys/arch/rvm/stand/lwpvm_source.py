#!/usr/bin/env python3
"""Select unchanged MI LWP/u-area glue with checked bootstrap allocation hooks."""
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
header = '''
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <uvm/uvm.h>
#include <machine/uarea.h>
#include <machine/pmap.h>
#include <machine/psl.h>
extern const char rvm_lwpvm_bad[];
static void rvm_no_user_uarea(vaddr_t base __unused) { panic(rvm_lwpvm_bad); }
#define uvm_uarea_free rvm_no_user_uarea
'''
names = ('uvm_lwp_getuarea', 'uvm_lwp_setuarea', 'uvm_lwp_fork', 'uvm_lwp_exit')
for name in names:
    header += f'#define {name} rvm_mi_{name}\n'
parts = []
for signature in ('vaddr_t\nuvm_lwp_getuarea(', 'void\nuvm_lwp_setuarea(',
                  'void\nuvm_lwp_fork(', 'void\nuvm_lwp_exit('):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    parts.append(source[start:end])
footer = '\n' + '\n'.join('#undef ' + name for name in names)
footer += '\n#include "' + str(Path(sys.argv[3]).resolve()) + '"\n'
Path(sys.argv[2]).write_text(source[:source.index('#include <sys/cdefs.h>')] + header + '\n'.join(parts) + footer)
