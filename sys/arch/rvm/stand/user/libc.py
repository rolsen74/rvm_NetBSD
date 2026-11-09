"""NetBSD libc source catalog for the static RVM runtime."""
STDIO = '''clrerr fclose fdopen feof ferror fflush fgetc fgetln fgetpos fgets
fileno findfp flags flockfile fopen fprintf fpurge fputc fputs fread freopen
fseek fseeko fsetpos ftell ftello funopen fvwrite fwalk fwide fwrite getc getchar
getdelim getline makebuf printf putc putchar puts refill remove rewind rget
setbuf setbuffer setvbuf snprintf_ss stdio ungetc vasprintf vfprintf vprintf
vsnprintf vsnprintf_ss vsprintf wbuf wsetup fmemopen open_memstream
dprintf vdprintf scanf fscanf sscanf vfscanf vscanf vsscanf fgetwc fgetwln
fgetws fputwc fputws getwc getwchar putwc putwchar ungetwc fwprintf swprintf
vfwprintf vswprintf vwprintf wprintf fwscanf swscanf vfwscanf vswscanf vwscanf
wscanf open_wmemstream fgetstr fparseln perror getw putw gettemp mkstemp
mkstemps mkostemp mkostemps mkdtemp tmpfile'''.split()
LOCALE = '''global_locale localeconv runetable multibyte_amd1 multibyte_c90
__mb_cur_max setlocale generic_lc_all dummy_lc_collate fix_grouping rune
_wctype _wctrans iswctype_mb wcscoll wcstoimax wcstoumax wcstod wcstof wcstold'''.split()
CITRUS = '''citrus_ctype citrus_none citrus_lc_ctype citrus_lc_monetary
citrus_lc_numeric citrus_lc_time citrus_lc_messages citrus_mmap citrus_db
citrus_db_hash citrus_hash citrus_bcs citrus_lookup citrus_memstream
citrus_module citrus_bcs_strtol'''.split()
GEN = ['ctype_', 'tolower_', 'toupper_', 'isatty', 'sysconf', 'sysctl', 'sysctlgetmibinfo', 'isctype', 'fpclassifyd_ieee754', 'signbitd_ieee754', 'fpclassifyf_ieee754', 'signbitf_ieee754']
GEN += ['isnand_ieee754', 'isnanf_ieee754', 'isinfd_ieee754', 'isinff_ieee754']
GEN += ['isfinited_ieee754', 'isfinitef_ieee754']
GEN += '''getcwd err errc errx verr verrc verrx warn warnc warnx
vwarn vwarnc vwarnx sigsetops signal siglist times raise opendir readdir
closedir initdir telldir setmode signalname signalnumber signalnext siginterrupt
setproctitle pw_scan signame alphasort fts pwcache getbsize humanize_number vis'''.split()
WIDE_STRINGS = '''wmemcpy wmemmove wmemset wmemcmp wcscpy wcscspn wcsncpy
wcspbrk wcsncmp wmemchr'''.split()


def build(builder):
    members = []
    for directory, names in [('stdio', STDIO), ('locale', LOCALE),
                             ('citrus', CITRUS), ('gen', GEN), ('termios', ['tcgetattr', 'tcgetpgrp', 'tcsetpgrp']),
                             ('compat-43', ['killpg']), ('stdlib', ['qsort', 'strtol', 'atoi', 'bsearch', 'merge']),
                             ('string', ['strdup', 'strndup', 'strerror', 'strerror_r', 'strsignal', '__strsignal', 'strcoll', 'strtok', 'strtok_r', 'wcscmp', 'strmode', 'wcslen', 'wcschr', 'mempcpy']),
                             ('time', ['localtime', 'asctime']),
                             ('string', WIDE_STRINGS),
                             ('gdtoa', 'dtoa ldtoa hdtoa gdtoa gdtoa_locks dmisc gmisc misc smisc sum ulp strtod strtof strtodg strtord gethex hexnan hd_init'.split())]:
        for name in names:
            source = builder.root/'lib/libc'/directory/(name+'.c')
            target = builder.obj/(directory+'_'+name+'.o')
            extra = ['-D_LIBC', '-DWITH_RUNE']
            if directory == 'gdtoa':
                extra += ['-DHonor_FLT_ROUNDS']
            if directory == 'time':
                extra += ['-I', str(builder.root/'lib/libc/time')]
                if name == 'localtime':
                    # reentrant.h defines once_t even in the nonthreaded build;
                    # tzcode provides its own bool once_t in this configuration.
                    generated = builder.obj/'localtime_compat.c'
                    original = source.read_text()
                    needle = '#include "private.h"'
                    assert original.count(needle) == 1
                    original = original.replace(needle, needle+'\n#undef once_t')
                    # Keep tzcode's mutex path inside its threaded build.
                    original = original.replace(
                        '#if !defined TM_GMTOFF || !USE_TIMEX_T',
                        '#if THREAD_SAFE && (!defined TM_GMTOFF || !USE_TIMEX_T)', 1)
                    generated.write_text(original)
                    source = generated
            builder.compile(source, target, extra)
            members.append(target)
    for name in ('strtoi', 'strtoimax', 'strtoumax', 'getopt', 'strtoul', 'strtoll', 'strtoull'):
        target = builder.obj/('stdlib_'+name+'.o')
        builder.compile(builder.root/'common/lib/libc/stdlib'/(name+'.c'), target, ['-D_LIBC'])
        members.append(target)
    for name in '''adddf3 subdf3 muldf3 divdf3 comparedf2 fixdfsi fixdfdi fixunsdfsi
                   fixunsdfdi floatsidf floatdidf floatunsidf floatundidf
                   extendsfdf2 truncdfsf2 addsf3 subsf3 mulsf3 divsf3 comparesf2
                   fixsfsi fixsfdi fixunssfsi fixunssfdi floatsisf floatdisf
                   floatunsisf floatundisf clzdi2 clzsi2 ctzdi2 ctzsi2'''.split():
        target = builder.obj/('compiler_rt_'+name+'.o')
        source = builder.root/'sys/external/bsd/compiler_rt/dist/lib/builtins'/(name+'.c')
        extra = ['-D__ELF__']
        if name in ('comparedf2', 'comparesf2'):
            mode = 'df' if name == 'comparedf2' else 'sf'
            extra = ['-U__ELF__']  # The adapter supplies GCC's __cmp aliases.
            extra += [f'-D__{op}{mode}2=__rvm_{op}{mode}2'
                      for op in ('eq', 'ne', 'le', 'lt', 'ge', 'gt', 'unord', 'cmp')]
        if name in ('clzdi2', 'ctzdi2'):
            op = name[:3]
            # GCC lowers a 32-bit builtin to the word helper on this target.
            # Avoid recursion using compiler-rt's explicit 32-bit implementation.
            generated = builder.obj/(name+'_compat.c')
            generated.write_text(f'extern int __{op}si2(unsigned);\n'
                                 f'#define __builtin_{op} __{op}si2\n'
                                 f'#include "{source}"\n')
            source = generated
            extra += [f'-D__{name}=__rvm_{name}']
        builder.compile(source, target, extra)
        members.append(target)
    target = builder.obj/'softfloat.o'
    builder.compile(builder.stand/'user/softfloat.c', target)
    members.append(target)
    target = builder.obj/'util_stat_flags.o'
    builder.compile(builder.root/'lib/libutil/stat_flags.c', target,
                    ['-I', str(builder.root/'lib/libutil')])
    members.append(target)
    generated = builder.obj/'errlist.c'
    with generated.open('w') as output:
        import subprocess
        subprocess.run(['awk', '-f', str(builder.root/'lib/libc/gen/errlist.awk'),
                        str(builder.root/'sys/sys/errno.h')], stdout=output, check=True)
    target = builder.obj/'gen_errlist.o'
    builder.compile(generated, target, ['-D_LIBC'])
    members.append(target)
    return members
