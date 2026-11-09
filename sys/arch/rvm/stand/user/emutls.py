"""Build GCC's emulated TLS implementation for the single-LWP runtime."""


def source_text(source):
    text = source.read_text()
    # Supply the ordinary C headers and the nonthreaded gthread setting in
    # place of GCC's generated target configuration headers. Keep the runtime
    # implementation, descriptor layout and license notice unchanged.
    for name in ('tconfig.h', 'tsystem.h', 'coretypes.h', 'tm.h', 'libgcc_tm.h', 'gthr.h'):
        directive = '#include "'+name+'"'
        assert text.count(directive) == 1
        text = text.replace(directive, '')
    return ('#include <stddef.h>\n#include <stdlib.h>\n#include <string.h>\n'
                         '#define __gthread_active_p() 0\n'+text+
                         '\n_Static_assert(sizeof(word) == 8 && sizeof(pointer) == 8, "RVM TLS word ABI");\n'
                         '_Static_assert(sizeof(struct __emutls_object) == 32 &&\n'
                         'offsetof(struct __emutls_object, loc) == 16 &&\n'
                         'offsetof(struct __emutls_object, templ) == 24, "RVM TLS descriptor ABI");\n')


def build(builder):
    source = builder.root/'external/gpl3/gcc/dist/libgcc/emutls.c'
    generated = builder.obj/'gcc_emutls.c'
    generated.write_text(source_text(source))
    target = builder.obj/'gcc_emutls.o'
    builder.compile(generated, target)
    return [target]


if __name__ == '__main__':
    import argparse
    from pathlib import Path
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(source_text(args.source))
