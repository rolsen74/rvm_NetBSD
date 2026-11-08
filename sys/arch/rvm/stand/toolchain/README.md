# RVM GCC argument-frame correction

The maintained sibling sources and installed NetBSD-target compiler/linker in
this workspace were updated during the 2026-11-07 porting session. The patches
are retained here for reproducing the changes in an older checkout. Original
sources and installed executables are backed up under
`/tmp/rvm-netbsd-toolchain-backup-20261107/`.

`rvm-caller-homes.patch` applies to the maintained GCC backend in the sibling
`rvm_gcc/_data/work_gcc-14.3.0` tree. It addresses three related ABI defects:

- The prologue reserved locals and guessed incoming homes but omitted the
  accumulated outgoing-argument area. Calls with stacked arguments could
  overwrite local variables or saved state.
- The argument pointer referred to private local storage even for arguments
  passed on the caller's stack. Small scalar homes also had inconsistent
  padding.
- The incoming-varargs hook computed a pretend size but never saved unused
  argument registers, so `va_start` read uninitialized memory.

The corrected ABI reserves eight eight-byte register homes in each caller's
outgoing area, followed by stack arguments. All argument slots advance in
units of eight bytes. R2–R9 still carry the first eight argument words, and
JSR still pushes an eight-byte return address. Register arguments are spilled
to their caller-provided homes when needed; variadic functions spill their
remaining argument registers there. The argument-pointer offset includes
PUSHF's mask and saved registers, the return address, locals and outgoing area.
PUSHF/POPF register preservation and the four-byte stack alignment are unchanged.

The backend source is shared by RVM targets. Only the NetBSD-target installed
compiler was replaced in this session. Other targets rebuilt from the updated
backend must also rebuild their C/runtime objects and reserve the new caller
homes at assembly-to-C boundaries; their existing installed compilers were not
changed.

The compiler advertises `__RVM_CALLER_ARG_HOME__=64`. The port's assembly C-call
boundaries reserve that area when the macro is present. The macro also enables
the real MI printf parser and C stdarg wrappers. The older compiler retains its
legacy diagnostic adapters. Rebuild every C object and applicable runtime
library after changing the ABI; objects from the two ABIs must not be mixed.

To apply to the maintained sibling backend, then rebuild its compiler using
that project's existing build procedure:

```sh
cd ../rvm_gcc/_data/work_gcc-14.3.0
patch -p1 < ../../../rvm_NetBSD/sys/arch/rvm/stand/toolchain/rvm-caller-homes.patch
```

After rebuilding the compiler, clean-build the port and run the ABI and full
regression suites. `--suite abi` checks `-O0`, `-O2` and `-Os` at 16 and 64 MiB:

```sh
make -f sys/arch/rvm/Makefile.bootstrap clean
make -f sys/arch/rvm/Makefile.bootstrap
python3 sys/arch/rvm/tests/bootstrap.py --objdir obj.rvm-bootstrap \
    --cross rvm-netbsd- --vmemu VMEmu --suite abi
make -f sys/arch/rvm/Makefile.bootstrap TEST_SUITE=full test
```

The runtime tests exercise more than eight arguments, address-taken bool and
narrow scalar parameters, register/stack varargs, `va_copy`, calls nested inside
a variadic function, by-reference aggregate arguments and aggregate returns.
The MI formatter suite additionally checks mixed-width stacked printf arguments,
widths, precision, length queries and bounded-buffer truncation.

# Normal-kernel data emission and alignment

`rvm-data-alignment.patch` adds aligned/unaligned `.quad` emission and restores
`ASM_OUTPUT_ALIGN` as `.balign`. Previously even `const void *p = &object`
could fail with "initializer for integer/fixed-point value is too complicated",
and explicitly aligned C stacks/objects silently lost their requested alignment.
The normal kernel needs both fixes for MI pointer tables and page/cache alignment.
The caller-home ABI is unchanged by this patch, but clean-build all objects to
restore their alignments. The helper checks these hooks before building.

The maintained sibling backend and installed NetBSD `cc1`/`lto1` include these
fixes. Their immediate previous versions are backed up under
`/tmp/rvm-netbsd-normal-toolchain-backup-20261107/`. Other installed target
compilers, VMAsm and VMEmu were not changed in this normal-kernel session.

FHR object records currently lose local/weak binding. `kernel_cc.py` gives
private C definitions unique per-object names and records weak definitions;
`kernel_link.py` resolves strong-over-weak and first-weak precedence using
temporary object copies before linking. It includes libkern members explicitly.
This adapter supports static kernels, not shared objects/loadable modules.
The build also bypasses ELF objcopy/metadata post-processing that FHR cannot
round-trip. These workarounds should eventually move into the object tools.

# FHR linker diagnostics

`rvm-fhr-undefined-symbols.patch` applies to the maintained sibling
`rvm_gcc/_data/work_binutils-2.46.0` tree. Its FHR BFD target vector contained
null pointers for source-line/debug callbacks. GNU ld invokes the nearest-line
callback when reporting an undefined relocation, so a missing symbol caused
a host SIGSEGV while formatting the diagnostic. The patch uses BFD's standard
`_bfd_nosymbols_*` fallbacks for all six unsupported debug callbacks. These
report that debug information is unavailable without dereferencing null.

After rebuilding binutils, run:

```sh
python3 sys/arch/rvm/tests/linker.py --cross rvm-netbsd-
```

The tests require ordinary error status 1, the missing symbol in the diagnostic,
and no retained output executable for unresolved call and data relocations.

# Single-precision moves and software floating point

`rvm-softfloat-moves.patch` adds the missing SFmode move expansion to the
maintained GCC backend. SF values occupy the low 32 bits of a GPR and move
through the existing SImode instructions. Without this expansion, even a C
function returning its float parameter reaches GCC's multiword move fallback
and fails its `mode_size >= UNITS_PER_WORD` assertion on this 64-bit target.

The source overlay and installed NetBSD `cc1` include the tested fix. Their
previous versions are backed up in
`/tmp/rvm-softfloat-toolchain-backup-20261108/`. The matching compiler source
and build used for staging remain under `/tmp/rvm-netbsd-abi/`. `lto1` and
other installed target compilers were not replaced; FHR LTO is not supported
by this bootstrap build.

The user runtime supplies imported compiler-rt binary32/binary64 arithmetic
because the installed target libgcc lacks those functions. Its comparison
and leading/trailing-zero helpers return 32-bit C int, whereas this backend's
libcalls expect 64-bit word results. `stand/user/softfloat.c` adapts those
results without changing the C int ABI. The generated count-helper copies
also use the explicit 32-bit implementation to prevent recursion through
GCC's word-sized builtin lowering. Arithmetic uses nearest-even rounding;
these software helpers do not implement selectable rounding modes or hardware
floating exception state.

The disk-root stdio check exercises float/double arithmetic, comparisons,
integer/floating conversions, varargs formatting, hexadecimal output, parsing,
NaNs, infinities and the smallest binary64 subnormal in the emulator.

# Native binary64 FPU

`rvm-native-fpu.patch` adds `-mfpu` / `-mno-fpu` to the GCC backend. Apply it
after the single-precision move patch. The maintained sibling backend and
installed NetBSD driver and `cc1` include both patches. Their previous native
FPU versions are backed up in `/tmp/rvm-native-fpu-toolchain-backup-20261108/`.

Native add/subtract/multiply/divide, abs and sqrt preserve the integer-register
calling convention and save their scratch FP registers. The user builder now
selects `-mfpu`; the compiler default and kernel C build remain software float.
Single precision, comparisons and conversions still use the runtime helpers.
See [FPU integration](../../../../../doc/rvm-fpu-2026-11-08.md) for context
preservation, rounding, limitations and the three-optimization guest test.
