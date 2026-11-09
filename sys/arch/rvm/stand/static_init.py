"""Carry static initialization metadata across the FHR object adapter.

FHR has no linker-sorted ctor/dtor or ELF initialization-array sections.
Keep callbacks as ordinary exported code and let the user linker build the
ordered startup/exit calls from the objects actually selected for the image.
"""
import re


def extract(source):
    entries, output, exports = [], [], []
    current = None
    for line in source.splitlines(keepends=True):
        section = re.match(r'\s*\.(?:section|text|data|bss)\b', line)
        if section:
            table = re.fullmatch(
                r'\s*\.section\s+"?\.(ctors|dtors|preinit_array|init_array|fini_array)'
                r'(?:\.(\d+))?"?(?:,[^\n]*)?\s*', line)
            current = None
            if table:
                kind, priority = table.groups()
                priority = int(priority) if priority is not None else 65535
                if kind in ('ctors', 'dtors') and table[2] is not None:
                    priority = 65535 - priority
                if not 0 <= priority <= 65535:
                    raise ValueError('Invalid static initializer priority: '+line.strip())
                current = ('preinit' if kind == 'preinit_array' else
                           'fini' if kind in ('dtors', 'fini_array') else 'init', priority)
                output.append('\t.data\n')
                continue
        if current is not None:
            callback = re.fullmatch(r'\s*\.quad\s+([A-Za-z_.$][\w.$]*)\s*', line)
            if callback:
                name = callback[1]
                entries.append([*current, name])
                exports.append('\t.global '+name+'\n')
                continue
            if not line.strip() or re.match(r'\s*\.(?:balign|p2align|align|ident)\b', line):
                continue
            if re.match(r'\s*\.(?:local|comm|global|globl|weak|weakref|type|size|set)\b', line):
                output.append(line)
                continue
            raise ValueError('Unsupported static initializer data: '+line.strip())
        output.append(line)
    return ''.join(exports+output), entries


def registry(entries, native_exit=False):
    """Generate direct calls; callbacks may have been local C definitions."""
    declarations = []
    calls = {'preinit': [], 'init': [], 'fini': []}
    ordered = sorted(enumerate(entries), key=lambda item: (item[1][1], item[0]))
    for index, (phase, _priority, symbol) in ordered:
        if phase not in calls or not re.fullmatch(r'[A-Za-z_.$][\w.$]*', symbol):
            raise ValueError('Invalid static initializer metadata')
        name = 'callback_'+str(index)
        declarations.append(f'extern void {name}(void) __asm__("{symbol}");\n')
        calls[phase].append(name+'();')
    registrations = ''
    if native_exit:
        # Individual atexit entries let NetBSD's normal __cxa_finalize handle
        # exit() invoked by a destructor, continuing with the remaining ones.
        registrations = ('extern int atexit(void (*)(void));\n'
                         'int __rvm_register_fini(void) {\n'+
                         '\n'.join('if (atexit('+call[:-3]+')) return -1;'
                                   for call in calls['fini'])+
                         '\nreturn 0;\n}\n')
    preinit = '\n'.join(calls['preinit'])
    start = '\n'.join(calls['init'])
    # Advance before each call so exit() from a destructor cannot repeat it.
    finish = '\n'.join(f'if (next <= {i}) {{ next = {i+1}; {call} }}'
                       for i, call in enumerate(reversed(calls['fini'])))
    return (''.join(declarations)+registrations+
            'void __rvm_run_preinit(void) {\n'+preinit+'\n}\n'+
            'void __rvm_run_constructors(void) {\n'+start+'\n}\n'+
            'void __rvm_run_init(void) {\n'
            '__rvm_run_preinit(); __rvm_run_constructors();\n}\n'+
            'void __rvm_run_fini(void) {\nstatic unsigned next;\n'+finish+'\n}\n')
