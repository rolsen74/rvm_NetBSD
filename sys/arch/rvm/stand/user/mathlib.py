"""NetBSD binary64 libm catalog for the static bootstrap runtime."""

# Use the ordinary POSIX wrappers, including errno handling, and the MI
# fdlibm implementations.  Extended-precision and complex math are separate
# porting work; do not compile their machine-specific representation code.
SOURCES = '''
e_acos e_acosh e_asin e_atan2 e_atanh e_cosh e_exp e_fmod e_hypot
e_log e_log2 e_log10 e_pow e_rem_pio2 e_remainder e_scalb e_sinh
k_cos k_rem_pio2 k_sin k_standard k_tan
s_asinh s_atan s_cbrt s_ceil s_copysign s_cos s_erf s_exp2 s_expm1
s_fabs s_fabsf s_finite s_floor s_frexp s_ilogb s_lib_version
s_log1p s_logb s_matherr s_modf s_nearbyint s_nextafter s_rint s_rintf
s_round s_scalbn s_signgam s_significand s_sin s_sincos s_tan s_tanh
s_trunc s_fmax s_fmin s_fdim s_lround s_llround s_fma s_remquo
w_acos w_acosh w_asin w_atan2 w_atanh w_cosh w_exp w_fmod w_hypot
w_log w_log2 w_log10 w_pow w_remainder w_scalb w_sinh w_sqrt
'''.split()


def build(builder):
    members = []
    flags = ['-I', str(builder.root/'lib/libm/src'), '-D_POSIX_MODE',
             '-D_MULTI_LIBM', '-frounding-math', '-fno-strict-aliasing']
    sources = [builder.root/'lib/libm/src'/(name+'.c') for name in SOURCES]
    sources += [builder.root/'lib/libm/gen'/(name+'.c')
                for name in ('nan', 'nanf', 'nanl')]
    sources += [builder.root/'lib/libm/arch/rvm'/name
                for name in ('fenv.c', 's_rintl.c', 'fenvreg.S')]
    sources += [builder.root/'lib/libm/arch/rvm/e_sqrt.S' if builder.native_fpu
                else builder.root/'lib/libm/src/e_sqrt.c']
    sources += [builder.root/('lib/libm/arch/rvm' if builder.native_fpu
                             else 'lib/libm/src')/(name+('.S' if builder.native_fpu else '.c'))
                for name in ('s_lrint', 's_llrint', 's_lrintl', 's_llrintl')]
    for source in sources:
        target = builder.obj/('libm_'+source.stem+'.o')
        if source.suffix == '.S':
            builder.assemble(source, target)
        else:
            builder.compile(source, target, flags)
        members.append(target)
    return members
