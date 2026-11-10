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

The kernel linker groups output into `.text`, `.data`, `.rodata`, and `.bss`,
in that order. The loader allocates each section on separate 64 KiB pages,
so read-only data separates initialized writable data from writable BSS.
NetBSD linker sets share `.data`, with explicit `__start_link_set_*` and
`__stop_link_set_*` boundaries; the version note is folded into `.data` too.
Zero-initialized storage uses FHR BSS0 and is cleared by the loader, without
storing its zero bytes in the executable. Explicit `.data` attributes remain
initialized data.

Two toolchain limitations require an intermediate step. The BSS1 object reader
replaces the recorded allocation size with eight bytes, and multiple writable
input sections can produce incorrect relocations. `kernel_cc.py` marks zero
storage as `.rvm.bss` in its saved assembly. After weak-symbol resolution,
`kernel_link.py` assembles code/data and zero storage into separate temporary
objects, each with ordinary `.data` and its original alignment. A generated
linker script excludes the zero-storage objects from `.data` and collects
them into the final NOLOAD `.bss`. Bootstrap and userland keep their existing
zero-filled `.data` adapter.

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

The installed GCC/GAS now use FPU mnemonics, including the corrected FPushm
and FMove opcode assignments. The port's register-bank assembly uses these
mnemonics too. VMAsm and GAS accept `mfctrl FPCR,r0`, `mfctrl FPSR,r1`,
`mtctrl r0,FPCR` and `mtctrl r1,FPSR`; VMDis prints those names. The existing
16-bit control IDs remain 12 and 13. GAS now parses both operands of the
control instructions, including the existing integer control registers.
`rvm-control-registers.patch` records the incremental GAS fix against the
FPU-enabled binutils overlay. The maintained sibling sources and installed
tools include it. The previous tools/sources are backed up under
`/tmp/rvm-fpctrl-update/backup/`.

FP register-mask stack operations are spelled `fpushm` and `fpopm`; the old
`fpush`/`fpop` spellings have been removed. GCC output, the MD math routines
and VMDis use the mask mnemonics. Their encodings remain 0x01c2/0x01c0;
the address-based `fpusha`/`fpopa` operations are unchanged.
The prior compiler/tools are backed up under `/tmp/rvm-fpm-rename/backup/`.

```sh
python3 sys/arch/rvm/tests/fpu_control_tools.py
```

This checks all control-register encodings, FP register masks, malformed
operands, rejection of the removed FP stack spellings and a
GAS/VMAsm/VMDis round trip. See the
[libm/fenv update](../../../../../doc/rvm-libm-fenv-2026-11-09.md) for the
public environment API and native math runtime.

# NetBSD type definitions

`rvm-netbsd-types.patch` selects GCC's NetBSD integer-type definitions for the
RVM NetBSD target, including the least-width types used by `uchar.h`. It adds
NetBSD/unix predefined macros and a signed `wint_t`, while retaining the RVM
unsigned `wchar_t` ABI. The maintained sibling compiler sources and installed
`cc1` include this update. The previous compiler and configuration are backed
up under `/tmp/rvm-stdint-update/backup/`.

# VMEmu MMU store preflight

`rvm-mmu-store-preflight.patch` applies to the sibling VMEmu repository,
using `patch -p1`. Cross-page writes check every destination mapping before
modifying RAM or data-cache lines. NetBSD's four-byte-aligned 64-bit atomics
need this behavior for protection faults and restartable COW stores.
Physical device/bus errors are outside this guarantee. The patch also updates
the FPU test target for `VMRun.c` and the `fpushm`/`fpopm` opcode names, and
adds cached/uncached MMU regression checks. Run `make -j4 test-fpu` there.

# GCC code generation optimizations

`rvm-gcc-optimizations.patch` applies to the GCC source tree with `patch -p1`,
after the native FPU and unsigned VAdd/VSub immediate updates. The maintained
sibling GCC backend and installed NetBSD `cc1` include it. The previous backend
and compiler are backed up in `/tmp/rvm-gcc-opt/`.

The update reuses the Z flag for adjacent integer moves, logical operations
and zero extensions before EQ/NE branches. Checks happen during final output,
after scheduling; labels, calls, asm and unknown instructions stop reuse.
Signed comparisons keep their explicit tests because extension and arithmetic
flags need not describe the tested subword's sign or overflow.

Known-zero register copies become clears of the appropriate width, and an
adjacent constant store/register assignment shares the register's immediate.
The latter uses the existing destination and checks address dependencies.
Named signed/unsigned divmod expanders select the existing combined division
instructions. Register outputs may overlap inputs because the emulator reads
both inputs before writing either result.

A leaf with no locals, outgoing area, saved registers or frame-pointer use
omits PUSHF/POPF; argument-pointer elimination accounts for the missing frame.
An explicit `-fno-omit-frame-pointer` now retains the frame as requested.
The backend now describes ordinary narrow loads as preserving the other
register bits, rather than incorrectly promising zero extension. This removes
the EXTU.L that reload previously inserted for ordinary 32-bit loads and
comparisons. Genuine 32-to-64-bit unsigned loads retain EXTU.L: MOVE.L preserves
the destination's upper half.

```sh
python3 sys/arch/rvm/tests/gcc_optimizations.py
```

This checks emitted instructions and executes signed/unsigned boundary cases,
dirty upper register bits, quotient/remainder pairs, reused constants, zero
arguments, local frames and stack arguments in VMEmu. It runs at `-O0`, `-O2`,
`-Os`, and `-O2 -fno-omit-frame-pointer`. Use `--compiler-dir` to test a candidate
`cc1` without installing it, or `--baseline` to check the original compiler's
behavior without requiring the new output patterns.

# Bit operations and subword clears

`rvm-gcc-bit-operations.patch` applies after the optimization patch above.
`rvm-gas-bit-operations.patch` applies to the binutils source tree. Both use
`patch -p1`. The maintained sibling sources and installed tools include them;
the previous versions are backed up under `/tmp/rvm-bits/`.

GAS accepts BClr/BSet/BChg/BTst, their register forms, and their compact
immediate forms, with unsuffixed and `.q` spellings. Immediate bit numbers
are in 0..63. For example, `& 0xfffffff7` becomes `vbclr #3,src,dst`:
the instruction takes a bit number rather than a mask.

GCC selects constant and variable bit clear/set/change instructions and fuses
single-bit EQ/NE branches into bit tests. Register operations can implement
narrow C values, but the general memory forms are restricted to actual 64-bit
accesses. Big-endian GCC extract positions are converted to hardware bit
numbers, which count from the least significant bit.

Masks that clear a complete low byte, word or long use the corresponding
RCLR. For example, `& 0xffff0000U` uses RCLR.W. A target instruction attribute
blocks whole-result Z reuse for these subword clears and for bit operations:
the former describe only the cleared subword, and the latter describe the
old bit. A later branch on the whole result therefore keeps its test.

```sh
python3 sys/arch/rvm/tests/bit_tools.py
python3 sys/arch/rvm/tests/bit_ops.py
```

The first test checks exact encodings, malformed operands and a
GAS/VMAsm/VMDis round trip. The second executes all bit positions, old-bit
flags, result branches, memory-width guards and subword clears at `-O0`,
`-O2` and `-Os`. The top bit may use a shorter sign test when appropriate.

# Save only written integer registers

`rvm-save-written-registers.patch` applies after the bit-operation updates.
The maintained sibling backend and installed NetBSD `cc1` and `lto1` include
it. Previous sources and compiler binaries are saved under
`/tmp/rvm-save-mask/backup-installed/`.

PUSHF masks now include written or explicitly clobbered registers rather than
every live input. For example, `atomic_add_64` drops read-only R3 from its mask,
and `atomic_cas_64` drops R3/R4. Modified argument registers, allocated working
registers, subword clobbers and the frame pointer remain protected. Calls retain
the previous void-return treatment of the result/scratch pair.

The backend inspects the function's basic blocks, since prologue expansion uses
a separate instruction sequence. It accounts for auto-increment notes and
allocated pseudos during elimination, then freezes the mask after reload so
stack-argument offsets and the emitted frame agree. Functions that only read
argument registers may now qualify for the existing frameless-leaf path.

```sh
python3 sys/arch/rvm/tests/gcc_optimizations.py
```

The suite checks the add/CAS masks and verifies complete register preservation
after byte/long asm clobbers. Its isolated argument-ABI fixture checks stacked
and narrow arguments, stdarg/va_copy, nested calls, by-value aggregates, return
structures and dynamic stack allocation at `-O0`, `-O2`, `-Os`, and with an
explicit frame pointer. It needs no prebuilt bootstrap-kernel C objects.

# Omit empty register-save frames

`rvm-omit-empty-save-frame.patch` applies after the written-register update.
The maintained sibling backend and installed NetBSD `cc1` and `lto1` include
it. Previous sources and compiler binaries are saved under
`/tmp/rvm-omit-pushf/backup-installed/`.

Functions with no integer registers to preserve can now omit PUSHF/POPF even
when they need fixed stack space. A required or used frame pointer, or dynamic
stack allocation, retains the hardware frame. Calls still reserve their
64-byte argument-home area.

When PUSHF/POPF is omitted, a nonzero stack allocation gets a matching ADD
before RTS. Frames using POPF need no extra ADD because POPF restores SP.
Zero-sized allocations emit neither SUB nor ADD. Argument-pointer elimination
accounts for the omitted save frame when accessing stacked arguments.

`gcc_optimizations.py` checks all three cleanup paths and executes functions
with fixed locals and calls. A helper overwrites all eight argument-home slots;
another fixture reads a stacked argument after calling it. The existing
register-preservation, stdarg, aggregate and dynamic-allocation checks run in
all four compiler configurations above.

# CPU flag builtins

`rvm-status-flags.patch` applies after the empty-save-frame update. The
maintained sibling backend and installed NetBSD `cc1` and `lto1` include it.
Previous sources and binaries are in `/tmp/rvm-sr-flags/backup-installed/`.
The compiler defines `__RVM_FLAG_BUILTINS__=1`.

The no-argument builtins `__builtin_rvm_clrc/setc`, `clrv/setv`, `clrz/setz`,
`clrn/setn`, and `clrx/setx` emit the corresponding CPU instructions. Each
spelling uses the `__builtin_rvm_` prefix. They return `unsigned long` containing
SR immediately after the instruction when the result is used. A single RTL
instruction includes the operation and snapshot, so reload and scheduling
cannot insert a flag-changing instruction between them. Ignoring the result
emits only the set/clear instruction.

`__builtin_rvm_clri()` and `__builtin_rvm_seti()` similarly return CR after
updating its interrupt-enable bit. `__builtin_rvm_get_sr()` and
`__builtin_rvm_get_cr()` explicitly read those registers. Interrupt operations
and control-register reads require supervisor mode in this CPU; consequently,
capturing an arithmetic flag builtin's SR result also requires supervisor mode.
Use the builtin's returned snapshot when testing its result, since intervening
C expressions may change the live arithmetic flags.

`__builtin_rvm_clrs()` returns void and clears supervisor mode. The caller must
have prepared the user-mode execution context before this transition. There is
no SETS builtin: opcode 0x0079 is reserved. All these builtins are volatile
compiler barriers, and flag writes invalidate GCC's condition-code state.

The kernel's interrupt-enable path now uses the SETI builtin. Its disable
helpers retain the old CR result and use CLRI, preserving unrelated CR bits
while removing the temporary register and mask sequence.

`gcc_optimizations.py` compiles and executes `gcc_flags.c` at all four existing
optimization configurations. It checks all five arithmetic flag pairs,
interrupt enable/disable snapshots and unrelated CR bits, exact adjacency of
flag writes and their snapshots, and the shorter form for unused results.
CLRS is checked in generated code without changing the test VM's mode.

# Full-width byte and word extensions

`rvm-full-width-extensions.patch` applies after the status-flag update. The
maintained sibling sources and installed `cc1`/`lto1` include it. Backups are
in `/tmp/rvm-extensions/backup-installed/`.

EXTU/EXTS and their register forms write the complete 64-bit destination,
regardless of source width. The backend now describes direct byte-to-long,
byte-to-quad and word-to-quad conversions, for signed and unsigned register
and memory operands. GCC no longer needs intermediate widening instructions
for these conversions. Quadword masks of 0xff, 0xffff and 0xffffffff also use
the corresponding register EXTU instead of a larger mask sequence.

This does not assume that ordinary narrow moves clear upper bits. Mixed
signed/unsigned conversions still retain both extensions when required; for
example, converting signed 16-bit -1 to unsigned 32-bit and then unsigned
64-bit must produce 0x00000000ffffffff rather than 0xffffffffffffffff.

The GCC suite checks single-extension output and executes dirty-upper-bit,
byte/word sign-boundary, memory-load and mixed-conversion cases in all four
compiler configurations. The bit-operation suite also passes at its three
optimization levels.

# Omit homes for fixed register-only calls

`rvm-register-call-area.patch` applies after the full-width extension update.
The maintained sibling backend and installed `cc1`/`lto1` include it. Backups
are in `/tmp/rvm-regcall/backup-installed/`.

Fixed, nonvariadic signatures with at most eight scalar arguments, each at
most eight bytes, no longer reserve a 64-byte caller argument-home area.
This applies to direct calls and function pointers with complete prototypes.
GCC places address-taken parameters in the callee's own local frame. The same
rule applies to old-style definitions with known parameters; unprototyped calls
retain the conservative caller area.

Variadic calls, functions with stack arguments, aggregate signatures, complex
values, wider scalars, and unknown library calls keep the established layout.
The existing pointer-based `va_list` and ninth-argument offset therefore remain
valid. The compiler defines `__RVM_REGISTER_CALL_ABI__=2` to identify the new
rule; `__RVM_CALLER_ARG_HOME__=64` remains the conservative area size.

The kernel's assembly-to-C stubs call fixed register-only signatures, so their
`RVM_C_CALL_HOME` is zero with this compiler. Older compilers keep the previous
64-byte setup. Variadic assembly wrappers and the nine-argument raw syscall
wrapper retain their existing convention.

Recompile C objects and libraries together: an old callee may still store a
parameter in the caller's homes. Existing statically linked guest programs
remain usable with the rebuilt kernel because the syscall ABI is unchanged.
The clean native image for this update is `obj.rvm-native-regcall/`; the default
launcher's persistent disk is preserved.

The GCC suite checks zero-area fixed calls, required variadic homes, addressed
and narrow parameters, typed callbacks, old-style definitions, unknown-prototype
calls, stacked arguments, stdarg/va_copy, aggregates and dynamic stack frames
at all four compiler configurations. The former area-writing test helper now
has an explicit variadic signature.

Validation also passed the clean native rebuild, EARLY boot checks at 64 and
640 MiB, normal libc/csu and libm integration (including FP/complex ABI,
contexts, TLS, constructors, utilities and interactive shell), and two logins
each on the fresh 640 MiB image and the existing disk copy at 64 MiB. The rebuilt
`genfs_gop_write` has no SUB: its two callees take fixed register-only arguments.
The production kernel is 21,558 bytes smaller. The default runtime's kernel and
loader are updated with `.before-register-calls` backups; its disk checksum
is unchanged.

# Allocate the result register

`rvm-allocate-return-register.patch` applies after the register-call-area
update. The maintained sibling backend and installed `cc1`/`lto1` include it;
backups are in `/tmp/rvm-return-reg/backup-installed/`.

R0/R1 are now available to GCC's register allocator while retaining their
existing call-clobbered/result-register role. GCC can coalesce result pseudos
into R0, including results merged across multiple return paths. This removes temporary
constant assignments followed by copies to R0, redundant intermediate clears,
and some callee-saved working registers. The argument registers remain fixed.
This changes allocation, not the calling convention.

Both halves must be available together for 128-bit return reloads. Explicitly
reserving either register with `-ffixed-r0` or `-ffixed-r1` reserves the pair.
The existing save-mask policy protects written R0/R1 in void functions, and
values live across calls still receive preserved registers or spill slots.

The GCC tests reproduce the two-key comparator and verify direct R0 assignments
on its -1/0/1 paths. Runtime cases cover 64-bit unsigned comparison boundaries,
signed return paths, a live value across a nested call, full-width R0
preservation in an assembly-to-void-C call, and a void callee with stack
arguments. They also check 128-bit call/results and full-width R0/R1
preservation across a void callee making a wide-result call. These run in all
four compiler configurations. The formerly failing 128-bit compiler-rt helpers
compile at all three optimization levels, including when either result
register is explicitly reserved.

The complete native objects and kernels rebuilt successfully. EARLY boot
checks passed at 64/640 MiB; normal libc/csu/libm integration passed contexts,
TLS, FP/complex ABI, constructors, utilities and interactive shell checks; two
authenticated logins passed on a copy of the existing disk at 640 MiB.
The matching `compare_futex_key` and `compare_futex` functions now save only
R10/R62, assign return constants directly to R0 and clear only R0 on the zero
path. The production kernel is 13,015 bytes smaller. Runtime kernels and
loaders are updated with `.before-return-registers` backups, and both persistent
disk checksums are unchanged.

# Same-section PC-relative addresses

`rvm-same-section-pcrel.patch` applies to binutils after the existing FHR/GAS
updates. The maintained sibling sources and installed assembler, linker,
nm, ar and ranlib include it. Their previous versions and source backups are
in `/tmp/rvm-pcrel/backup-installed/`.

GAS records precise instruction starts for absolute 64-bit LEA, JSR, JMP and
conditional-branch operands in code. Reserved `__rvm_pcrel_` mapping symbols
carry this information through the existing FHR object format; BFD restores
their local binding and excludes them from public/archive symbols. They add
no section bytes and do not appear in executable relocation streams.

After symbol resolution and final section placement, the linker converts a
marked operand only if its target belongs to the same output code section.
It replaces absolute mode 0x37 with 64-bit PC-relative mode 0x33, computes the
displacement from the opcode address, and omits that loader relocation. Code
in different input objects or archive members can therefore use PC-relative
calls and LEAs once merged into the same output section.

Instruction widths and section layouts stay unchanged. Cross-section operands,
stored pointers (including pointers embedded in .text), absolute symbols and
older objects without mapping metadata retain their previous behavior.
`--no-relax` disables the final conversion and reproduces the previous
instruction bytes and relocation set. No CPU or loader format change is needed.

```sh
python3 sys/arch/rvm/tests/pcrel_link.py
```

The regression checks backward and forward references, globals, archives,
separate output code sections, data/rodata references, local mapping-symbol
binding, and explicit `--no-relax`. A literal pointer preceded by bytes that
look like a LEA instruction verifies that byte-pattern guessing is not used.
The linked fixture executes direct calls, an indirect call through a stored
pointer, and data accesses in VMEmu. GCC and bit-operation suites also pass
with the new tools.

The rebuilt production kernel keeps identical section sizes and permissions,
with relocations reduced from 61,872 to 15,787 and its file reduced from
2,748,784 to 2,566,137 bytes. All 46,085 removed entries are same-code-section
instruction operands; a byte comparison verifies that no other code bytes
changed. The EARLY kernel passes at 64 and 640 MiB.

The rebuilt native libc/csu/libm, constructor, TLS, context, utility and
interactive-shell tests pass, as do two authenticated logins on a copy of the
existing disk at 640 MiB. Default and native-regcall runtime kernels/loaders
are updated with `.before-same-section-pcrel` backups. Both guest disks retain
their original checksums.

Compact signed operands (2026-11-10)
----------------------------------

Apply `rvm-compact-operands.patch` after `rvm-same-section-pcrel.patch` to
binutils 2.46.0 and rebuild GAS. GCC already emits unsuffixed constants and
symbol references, so its assembler now selects the compact forms without
changing the C ABI or GCC register allocation:

* Unsuffixed positive immediates use the smallest unsigned 8/16/32/64-bit
  encoding; negative immediates use the smallest signed encoding. Signed
  extension preserves the value independently of the instruction's `.b`,
  `.w`, `.l` or `.q` operation width. Explicit immediate suffixes retain their
  existing behavior. Full-width bit patterns such as `0xffffffffffffffff`
  can use signed8 `-1`, preserving all 64 bits after extension.
  `cmp.q #256,r13` already used unsigned16 mode 0x39.
* Numeric register and PC displacements use all signed widths from 8 through
  64 bits in byte increments. `lea (-65536,r14),r0` uses signed24 mode 0x16.
* Symbolic calls, jumps, branches and LEAs within the same input code section
  use GAS fragment relaxation, including forward references, backward
  references and alignment. Displacements are relative to the opcode start.
  A backward BNE by 29 bytes is `00 87 2c e3`, four bytes rather than eleven.
* Cross-object and cross-section references retain ABS64 assembler fixups.
  The existing final linker still converts same-output-code-section fixups
  into PC64; it does not shrink already assembled input sections. Linker
  `--no-relax` disables that final conversion, while assembler-local compact
  references are already resolved and remain compact.

```sh
python3 sys/arch/rvm/tests/compact_operands.py
python3 sys/arch/rvm/tests/pcrel_link.py
python3 sys/arch/rvm/tests/gcc_optimizations.py
```

The operand regression checks unsigned and signed immediate boundaries,
signed displacement boundaries at every supported width, forward/backward
branches around the 8/16-bit limits, alignment, and execution in VMEmu.

The full native rebuild reduces the production kernel from 2,566,137 to
2,284,429 bytes (281,708 bytes saved), with loader relocations unchanged at
15,787. Its code section drops from 2,091,820 to 1,810,107 bytes; data, rodata
and BSS sizes and section permissions remain unchanged. GCC execution tests
pass at O0/O2/Os and O2 with a frame pointer, as do the bit-operation suite,
216 operand boundary checks, and PC-relative/pointer regression. EARLY boots
at 64 and 640 MiB; rebuilt libc/csu/libm, utilities, context/TLS/constructors,
interactive shell and two authenticated native logins pass. The default
`./run.sh` and native-regcall kernels/loaders are updated, with
`.before-compact-operands` backups and unchanged guest disk checksums.

Final-link displacement shrinking (2026-11-10)
--------------------------------------------

Apply `rvm-final-link-relaxation.patch` after `rvm-compact-operands.patch`.
Rebuild GAS, BFD, ld and the BFD-based binutils. No C ABI change is involved.
The default final linker now shortens tracked ABS64 instruction operands
when their resolved target shares the same output code section, including
references across input objects and archive members. `--no-relax` preserves
the previous absolute instruction bytes and loader relocation set.

GAS emits local, versioned coverage markers in addition to its existing
instruction mapping symbols. Only fully covered instruction-only input
sections are resized. Code containing alignment directives, inline data,
explicit numeric/indexed PC operands, or untracked symbolic PC operands opts
out. Objects containing symbolic constant differences also opt out, since
those differences may already be resolved without relocations. Old objects
retain the existing PC64 final conversion. Addressed bytes inside an operand
prevent shrinking that operand. A code pointer addend outside its input
section (including a negative addend before its first instruction) prevents
resizing that section, preserving its literal offset. Coverage end markers reject incomplete or
combined coverage; byte patterns alone never establish instruction boundaries.

BFD snapshots original input bytes, symbols and relocation addends before
relaxing. It rebuilds each input section from that snapshot on every layout
pass, maps local PC targets and all symbol/relocation offsets, and remaps
section-relative pointer addends as well as named symbols. An operand that
outgrows its selected range is promoted and retains that minimum width for
the rest of the link, preventing padding-induced layout oscillation. The
final pass validates each compact displacement before emitting it. ld's
normal layout retains input/output section alignment and produces the final
map from the updated definitions.

```sh
python3 sys/arch/rvm/tests/link_relax.py
```

The VM regression checks backward PC16 and forward PC16/PC24 LEAs, direct
and indirect calls, local branches, aliases, stored pointers, numeric pointer
addends, archive members, inline-data and numeric-PC opt-outs, an addressed
operand, and a linker-script alignment boundary. It also compares explicit
`--no-relax` output against the previous linker when `--baseline-ld` is given;
`--baseline-as` adds a legacy-object compatibility check.

The rebuilt production kernel shrinks from 2,284,429 to 2,192,814 bytes:
91,615 bytes saved, with loader relocations unchanged at 15,787. Its code
section drops from 1,810,107 to 1,718,469 bytes; data/rodata/BSS sizes and
permissions remain unchanged. Both rvm_copyuser paths now load rvm_user_pte
and kcopy with PC16, and memset with PC24. The full native libraries and
programs were rebuilt. GCC O0/O2/Os/frame-pointer tests, bit operations,
operand boundaries, PC-relative/pointer tests, EARLY 64/640 MiB boots, native
libc/csu/libm/TLS/context/constructors/utilities, interactive shell and two
authenticated logins pass. A final negative-addend guard was separately
regression-tested and re-linked both kernels byte-identically to these
validated images. The default ./run.sh and native-regcall runtime kernels
and loaders are updated, with .before-final-link-relaxation backups and
unchanged guest disk checksums.

Section-base addressing (2026-11-10)
------------------------------------

Apply `rvm-section-bases.patch` after `rvm-final-link-relaxation.patch` and
rebuild GAS, BFD and ld. GCC already reserves R50 for `.rodata`, R51 for
`.data` and R52 for `.bss`; its allocation and calling convention are unchanged.

Startup opts in by exporting `__rvm_section_bases_v1` at its entry. The updated
crt0 loads relocated `__rvm_rodata_base`, `__rvm_data_base` and `__rvm_bss_base`
addresses before calling C. The standalone/static user script and generated
kernel script define those symbols inside their output sections. Each data
section reserves eight trailing bytes so even an otherwise empty section has
a valid loader relocation target. Link static user programs through
`stand/user_link.py` (or its normal BSD-make adapter), which supplies the script.
The existing userland zero-storage adapter still collects zero-initialized
objects in `.data`; R52 addresses storage actually placed in output `.bss`.

GAS marks actual ABS64 memory operands, including second operands, with local
`__rvm_lr_data_` symbols. With startup support present, the final linker uses
the target output section to replace these operands and existing tracked LEAs
with signed register displacements. This covers clear/test, loads/stores,
read-only constants and memory-to-memory instructions. Register displacements
use the smallest signed byte width when the input code section has complete
relaxation coverage. Sections with inline data/alignment or other resize
restrictions use a same-size register-plus-signed56 form. Targets outside the
signed56 range retain their absolute relocations. Stored pointers, immediate
constants, absolute symbols, unmarked operands and other output sections
retain ordinary relocation behavior. Code targets retain PC-relative handling.

Base initialization references themselves always retain their absolute loader
relocations. `--no-relax` disables both shrinking and base conversion; an old
startup without the ABI marker never enables base conversion. The kernel
initializes its bases at entry, reinstalls them after saving interrupted GPRs
at trap/IRQ entries, and initializes them in the fresh-LWP trampoline. Saved
user bases are restored with the rest of the user register image on return.

crtend now places `_end`/`__rvm_end` in a `.rvm_end` input section collected last
in output BSS. The native brk veneers must begin after every loaded section;
leaving `_end` in `.data` lets the first heap allocation discard the later
read-only data/BSS mappings. Rebuild csu and relink native executables with the
new script. The regression also verifies the real crtend heap anchor and its
BSS relocation.

```sh
python3 sys/arch/rvm/tests/section_bases.py
python3 sys/arch/rvm/tests/link_relax.py
python3 sys/arch/rvm/tests/pcrel_link.py
python3 sys/arch/rvm/tests/compact_operands.py
```

The section-base regression checks all three registers, negative and positive
signed displacement boundaries, both operands of a memory copy, startup
relocations, stored pointers in data and inline code, non-resizable input,
legacy startup and `--no-relax`, then executes the fixture in VMEmu.

The maintained sibling sources and installed NetBSD assembler/linker include
this patch; their previous versions are backed up in
`/tmp/rvm-section-bases/backup-installed/`. The rebuilt production kernel is
90,156 bytes smaller, with loader relocations reduced from 15,787 to 3,262.
EARLY 64/640 MiB boots, native libc/csu/libm/TLS/context/constructor and shell
checks, and two authenticated logins pass. See
[the integration notes](../../../../../doc/rvm-section-bases-2026-11-10.md)
for runtime images, heap-anchor handling and verification details.

General multiply destination encoding (2026-11-10)
--------------------------------------------------

Apply `rvm-multiply-regmode.patch` after the other GAS patches. General
`muls.*` and `mulu.*` instructions restrict their destination to an integer
register, but encode that destination as `OAM_Reg` followed by the register
number. GAS previously emitted only the register number. The FHR assembly
adapter detected this and repaired each instruction with a trailing `.byte`,
which conservatively disabled final-link shrinking for the whole input text
section.

The new `AT_IRegMode` operand keeps register-only syntax checking while
emitting both bytes. The adapter's existing byte probe now selects ordinary
assembly without its raw-byte workaround. Rebuilding the production kernel
removes all 2,958 remaining PC64 code references; they become PC16/PC24.
The kernel shrinks from 2,102,658 to 2,072,632 bytes, and `.text` shrinks from
1,678,013 to 1,647,987 bytes. Signed and unsigned general multiplication in
all four widths, immediate and memory sources, compiler widening multiplies,
compact operands, PC-relative linking, section bases and final-link relaxation
pass VM execution tests.
