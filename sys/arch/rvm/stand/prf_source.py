#!/usr/bin/env python3
"""Use NetBSD's formatter with the bootstrap console and fixed RVM stdarg ABI.

Normal kernels link subr_prf.c and initialize its logging/tty facilities.
The standalone profile retains the real format parser and snprintf bodies,
with a console character sink, no-op flush, and no logging/tty state.
"""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
license_end = source.index('#ifdef _KERNEL_OPT')
macros = source[source.index('#define\tto_digit(c)'):source.index('\nvoid\ndevice_printf(')]
header = '''
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/kprintf.h>
#include <machine/boot.h>
#include <machine/asyncirq.h>
#include <machine/bootprintf.h>
#ifdef __RVM_CALLER_ARG_HOME__
const char hexdigits[] = "0123456789abcdef";
const char HEXDIGITS[] = "0123456789ABCDEF";
static void rvm_prf_flush(void) { }
#define v_flush (&rvm_prf_flush)
#define putchar(c, flags, context) rvm_early_putchar(c)
'''
parts = []
for signature in ('int\nkprintf(', 'int\nvsnprintf(', 'int\nsnprintf('):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    parts.append(source[start:end])
footer = '\n#include "' + str(Path(sys.argv[3]).resolve()) + '"\n'
footer += '\n#else\nint rvm_prf_verify(void) { return 0; }\n#endif\n'
Path(sys.argv[2]).write_text(source[:license_end] + header + macros + '\n'.join(parts) + footer)
