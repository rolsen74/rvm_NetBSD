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
