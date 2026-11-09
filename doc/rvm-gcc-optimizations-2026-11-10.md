# RVM GCC code generation optimizations

The maintained GCC 14.3 backend and installed NetBSD cross-compiler now
include the optimizations recorded in
[`rvm-gcc-optimizations.patch`](../sys/arch/rvm/stand/toolchain/rvm-gcc-optimizations.patch).
This is incremental to the native FPU and unsigned VAdd/VSub updates.

| Case | Result |
| --- | --- |
| Narrow memory loads | Remove the incorrect `LOAD_EXTEND_OP = ZERO_EXTEND` description. Ordinary loads use narrow moves; genuine widening still uses EXTU/EXTS. |
| Move/logical result followed by EQ/NE against zero | Reuse Z when the immediately preceding instruction establishes it for that value and width. |
| Register copy of a known zero | Emit RCLR at the assignment's width. Stop tracking at control-flow boundaries, calls, asm and unknown patterns. |
| Adjacent constant store and register assignment | Load the constant into the existing destination, then store that register. Reject address dependencies on the destination's old value. |
| Quotient and remainder of the same operands | Expose named signed/unsigned divmod expanders and use one division instruction. |
| Empty leaf frame | Omit PUSHF/POPF when there are no saved registers, locals, outgoing area, alloca or frame-pointer requirement. Adjust argument-pointer elimination accordingly. |

The branch check runs during final output, after scheduling. It handles only
EQ/NE against zero and rejects labels, calls and unknown flag producers.
Signed comparisons retain explicit tests. An explicit
`-fno-omit-frame-pointer` now takes effect rather than being overridden.

Sharing the constant between the store and register assignment avoids an
extra scratch register or a speculative load before the comparison. A real
64-bit unsigned result from a 32-bit load still needs EXTU.L: MOVE.L leaves
the old upper half intact.

The standalone regression is:

```sh
python3 sys/arch/rvm/tests/gcc_optimizations.py
```

It checks assembly and executes boundary cases in VMEmu at `-O0`, `-O2`,
`-Os`, and `-O2 -fno-omit-frame-pointer`. Cases include dirty upper bits,
8/16/32-bit widening, signed and unsigned 32/64-bit division and remainder,
zero arguments, constant reuse, local frames and stack arguments. Signed
division overflow and division by zero are excluded from the C cases.
The unsigned add/sub encoding/compiler regression also passes.

The full native userland, cached DISKROOT_ACCEL kernel and production
DISKROOT_GENERIC kernel were rebuilt with the installed compiler. Production
boots passed at 64 and 640 MiB, with idle host CPU samples of 5.4% and 5.2%
of one core over five seconds. Authenticated login, raw-disk fsck, tmpfs
mapping/accounting and PTY terminal/session regressions also passed at 64 MiB.
The normal libc/csu integration suite passed, including libm/FPU behavior,
contexts, TLS, constructors, file utilities and interactive shell job control.

For the same regression source at `-O2`, function text decreased from 1,295
to 1,229 bytes. The production kernel's text decreased from 2,274,947 to
2,242,049 bytes; its data size remains 630,992 bytes. These are size
measurements, not execution-speed benchmarks.

The optimized native userland and DISKROOT_GENERIC image are packaged under
`obj.rvm-native-gcc-opt/native-root`. The image uses the existing development
password input. The previous `obj.rvm-native-clean/native-root` disk, kernel
and loader remain unchanged. Boot the optimized image with:

```sh
RVM_NATIVE_OBJDIR=obj.rvm-native-gcc-opt ./run.sh
```

Compiler sources and the original installed `cc1` are backed up under
`/tmp/rvm-gcc-opt/before` and `/tmp/rvm-gcc-opt/cc1-before`. The temporary
compiler build is `/tmp/rvm-netbsd-abi/build/gcc`. Integration logs are under
`/tmp/rvm-gcc-opt/`.

## Bit-op follow-up

The installed GCC and GAS also include the incremental
[`rvm-gcc-bit-operations.patch`](../sys/arch/rvm/stand/toolchain/rvm-gcc-bit-operations.patch)
and
[`rvm-gas-bit-operations.patch`](../sys/arch/rvm/stand/toolchain/rvm-gas-bit-operations.patch).
GAS accepts the general, register and compact immediate bit instructions,
with unsuffixed and `.q` spellings. Bit arguments are numbers in 0..63.
For example, `x & 0xfffffff7U` selects `vbclr #3,src,dst`, not a mask operand.
`x & 0xffff0000U` selects `rclr.w`.

GCC combines constant and variable clear/set/toggle operations and bit-test
branches. It converts big-endian extract positions to hardware bit numbers.
The general memory instructions operate on 64 bits; narrower C accesses
retain their width. Bit instructions describe the old bit in Z, and a
subword clear describes only the cleared subword. An instruction attribute
therefore prevents reusing those flags for a whole-result zero test.

```sh
python3 sys/arch/rvm/tests/bit_tools.py
python3 sys/arch/rvm/tests/bit_ops.py
```

Encoding, malformed-operand and disassembly round-trip tests pass. VMEmu
tests cover every bit in 8/16/32/64-bit C values, old-bit flags, branches
after clears/toggles, volatile access widths and surrounding memory guards,
at `-O0`, `-O2` and `-Os`. The earlier optimization, FPU-control and unsigned
add/sub regressions also pass.

The native userland and cached/production kernels were rebuilt again.
The native libc/csu/libm, contexts, TLS, constructors, file utilities,
interactive shell, authenticated login, fsck, tmpfs and PTY tests pass.
Production boots pass at 64 and 640 MiB; five-second idle CPU samples were
6.2% and 5.8% of one core. Kernel text decreased another 7,878 bytes, from
2,242,049 to 2,234,171 bytes, with unchanged data size. The fresh disassembly
contains 1,231 VBTst instructions.

This follow-up has its own boot image and disassembly, preserving both
earlier image directories:

```sh
RVM_NATIVE_OBJDIR=obj.rvm-native-bit-ops ./run.sh
# Fresh addresses, labels and opcode bytes:
less obj.rvm-native-bit-ops/native-root/netbsd.fhr.s
```

The development password input is unchanged. Compiler/GAS source backups,
previous executables and integration logs are in `/tmp/rvm-bits/`.

## Default launcher and returning-login regression

Plain `./run.sh` previously fell back to `obj.rvm-native-clean`, so it still
started the older kernel and userland. A disposable copy of that disk
reproduced the stall immediately after the previous login time was printed.
Using the new kernel with that old disk also stalled. The current bit-op
image passed two consecutive authenticated logins, shell commands and logout,
including the last-login record on the second login.

The launcher now prefers `obj.rvm-native-root` when present, then the bit-op
build, the preceding optimization build and finally the older clean build.
An explicit `RVM_NATIVE_OBJDIR` still selects its requested image. Existing
disk images are preserved, and the development password stays the same.

The production-image regression now exercises actual copied disks rather
than only a newly packaged fixture:

```sh
python3 sys/arch/rvm/tests/native_root.py --objdir obj.rvm-native-bit-ops --login
```

It defaults to two logins and a 300-second timeout when login is enabled.
`RVM_TEST_PASSWORD` overrides the development test password. The test injects
normal SDL keyboard events, verifies root identity and shell output, and
modifies only a temporary disk copy. Logs for this diagnosis are
`/tmp/rvm-login-old-long.log`, `/tmp/rvm-login-mixed.log` and
`/tmp/rvm-login-bit-ops-long.log`.
