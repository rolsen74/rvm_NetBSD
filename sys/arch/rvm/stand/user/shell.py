"""Generate and build the tree's NetBSD sh in static SMALL configuration."""
import subprocess

SOURCES = '''alias arith_token arithmetic cd echo error eval exec expand
histedit input jobs mail main memalloc miscbltin mystring options parser redir
show trap output var syntax'''.split()


def build(builder, output):
    shell = builder.root/'bin/sh'
    work = builder.obj/'sh'
    work.mkdir(exist_ok=True)

    def generate(script, *args, stdout=None):
        subprocess.run(['sh', str(shell/script)] + list(map(str, args)),
                       cwd=work, stdout=stdout, check=True)

    generate('mktokens')
    generate('mkbuiltins', '-h', shell/'shell.h', shell/'builtins.def', work)
    generate('mknodes.sh', shell/'nodetypes', shell/'nodes.c.pat', work)
    with (work/'nodenames.h').open('w') as stream:
        generate('mknodenames.sh', work/'nodes.h', stdout=stream)
    generate('mkoptions.sh', shell/'option.list', 'optinit.h', work)
    sources = [(shell/'bltin' if name == 'echo' else shell)/(name+'.c') for name in SOURCES]
    sources += [builder.root/'bin/test/test.c', builder.root/'bin/kill/kill.c',
                builder.root/'usr.bin/printf/printf.c']
    generate('mkinit.sh', *sources)
    sources += [work/(name+'.c') for name in ('builtins', 'init', 'nodes')]
    builder.program(sources, output, ['-DSHELL', '-DSMALL', '-DUSE_LRAND48',
                    '-DREJECT_NULS', '-I', str(work), '-I', str(shell),
                    '-I', str(shell/'bltin'), '-I', str(builder.root/'lib/libedit')])
