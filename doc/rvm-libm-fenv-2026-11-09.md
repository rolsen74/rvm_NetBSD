# NetBSD/RVM floating-point environment and binary64 libm — 2026-11-09

The static user runtime now provides the C floating-point environment API and
a substantial binary64 subset of NetBSD libm. The default disk image exercises
both through real user processes. This builds on the
[FP process context support](rvm-fpu-2026-11-08.md).

## Environment

`machine/fenv.h` defines `fenv_t` as the FPCR/FPSR pair and `fexcept_t` as a
raw exception word. The exception constants match FPSR bits 0–4, and the four
rounding constants match FPCR. `FE_DFL_ENV` selects nearest-even and clears
status. All eleven standard `fe*` functions are implemented in
`lib/libm/arch/rvm/fenv.c`, with integer-ABI assembly control accessors.

Exception operations preserve comparison flags and every FP data register.
`feholdexcept` saves the complete environment and clears only exceptions.
`feupdateenv` reinstalls the saved environment and merges pending exceptions.
`fesetround` rejects unsupported modes. `fesetenv` and `feupdateenv` validate
both saved words before changing either hardware register.

The CPU has no exception trap enables: `fegetexcept` and `fedisableexcept`
return zero; `feenableexcept` returns -1 for a nonempty supported mask. This
does not synthesize SIGFPE for sticky hardware exceptions.

The gdtoa sources now build with `Honor_FLT_ROUNDS`, matching ordinary NetBSD
libc. Decimal formatting and parsing consult `fegetround`, including directed
rounding for `printf`, `strtod` and `strtof`. These interfaces do not promise
that every parsing exception is reflected in FPSR.

## Math runtime

`stand/user/mathlib.py` catalogs the imported, unchanged NetBSD sources.
They are included in the bootstrap `librvm.a`; programs do not need a
separate `-lm` with this builder. This is still a bootstrap library, not a
complete distribution build of libc/libm/csu.

The catalog covers double-precision trigonometric/inverse/hyperbolic functions,
exp/log/pow, roots, erf/erfc, rounding, decomposition/scaling, remainder/remquo,
nextafter, min/max/difference, NaN construction and fused multiply-add.
The normal POSIX wrappers provide errno behavior for domain/range errors.
RVM's binary64 `long double` uses the MI aliases where supplied; `rintl`
forwards to `rint`. Complete float/long-double and complex API coverage remains
future work.

The MD `__ieee754_sqrt` uses FSqrt while preserving its FP scratch register.
`lrint`, `llrint`, `lrintl` and `llrintl` use RFStore.q, honoring FPCR and
setting hardware Inexact/Invalid flags. Out-of-range native conversions return
the CPU's signed minimum sentinel. C casts continue to use compiler-rt.

NetBSD's MI `fma` supplies fused results without a hardware FMA instruction.
Its intermediate arithmetic can set Inexact even when the final fused result
is exact. The port does not claim complete IEEE exception conformance for all
imported libm routines. Single-precision compiler-rt arithmetic, comparisons
and conversions still have their prior software exception/rounding behavior.
In particular, exposing fenv does not make all binary32 arithmetic obey FPCR.

The installed GCC/GAS now emit/accept FPU arithmetic and register-bank
mnemonics. FP save/restore uses `fpusha`/`fpopa`; VMAsm spells their operands
`(r2)`, while GAS spells them `r2`. VMAsm, GAS and VMDis now recognize FPCR
and FPSR (register IDs 12 and 13), so control access uses `mfctrl`/`mtctrl`
mnemonics throughout the port as well. The compiler regression
checks mnemonic output instead of the previous raw opcode strings. Kernel C
and standalone bootstrap C explicitly select `-mno-fpu`.
FP register-mask stack operations use `fpushm`/`fpopm`; the old shortened
spellings are no longer accepted. This changes mnemonic spelling only.

## Comparison builds and checks

`--software-float` explicitly selects `-mno-fpu` for user code/libraries and
uses the MI software square root and integer-rounding implementations. The
comparison runtime still has fixed nearest-even arithmetic and no automatic
FPSR exceptions. It tests fenv controls, directed decimal conversions and
nearest-even math separately from the native arithmetic requirements. Native
instruction checks remain enabled in their separately compiled fixture, as
before. Both modes require an emulator with FP context instructions.

```sh
./build-diskroot.sh
python3 sys/arch/rvm/tests/hardfile.py --objdir obj.rvm-diskroot \
    --diskroot --expect-accel
python3 sys/arch/rvm/tests/fpu_compiler.py
python3 sys/arch/rvm/tests/fpu_control_tools.py
python3 sys/arch/rvm/tests/console.py --shell --pixels --keypad
```

The math check covers environment save/hold/update/default behavior,
selective exception clearing/replacement, comparison-bit preservation,
reserved-bit rejection, unavailable trap enables, all four rounding modes,
directed decimal midpoint conversions, signed zero, native integer conversion
boundaries, fused cancellation, avoidance of intermediate overflow, math
values and errno/exception propagation. It verifies that the FP bank remains
unchanged. It runs in the boot fixture and with the compiler test programs at
`-O0`, `-O2` and `-Os`.

For a separately built software image, add `--software-float` to `make_root.py`
and to the `hardfile.py --diskroot` command so the host requires the comparison
runtime's success marker.

Validation includes normal and DIAGNOSTIC disk-root boot/persistence at
64/640 MiB, the native compiler programs at all three optimization levels,
a 64 MiB software-runtime boot/persistence run, interactive shell/job control
at 64/640 MiB, keyboard/keypad/pixel checks at 64 MiB, FHR binding/archive
regressions and the installed VMAsm's thirteen regression tests. Normal artifacts
are in `obj.rvm-diskroot/`; diagnostic
artifacts are in `obj.rvm-diskroot-diag/`. Each tree retains boot transcripts
under `hardfile-tests/`, and the native compiler transcript is
`obj.rvm-diskroot/fpu-compiler-64.log`.

The subsequent [static initialization update](rvm-static-init-2026-11-09.md)
adds static C constructors and destructors. Remaining port work includes full
libc/csu build integration, TLS, pthreads, dynamic linking, production init/login,
complete libm coverage
and broader ABI/IEEE conformance.
