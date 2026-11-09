# NetBSD/RVM static constructors and destructors — 2026-11-09

Programs built with `stand/user/build.py` now run static C constructors before
`main` and destructors on normal process exit. This is another step toward
libc/csu integration; it does not provide a dynamic linker or a C++ library.

## FHR adapter and link

The installed GCC emits legacy `.ctors`/`.dtors` sections, including inverted
numeric priority suffixes. FHR does not retain or sort those sections. The
user build therefore enables an optional `kernel_cc.py` path that extracts
initialization entries after private symbols have received unique names.
`stand/static_init.py` also recognizes raw `.preinit_array`, `.init_array` and
`.fini_array` entries, with direct priority suffixes. Unsupported table data
is rejected instead of being silently omitted. Named user variables placed
in these sections and crtbegin/crtend sentinel tables are not supported.

The callback code remains in ordinary text, with its unique symbol exported
for the generated registry. Each object has an `.init.json` sidecar. Runtime
archive construction collects these into `librvm.init.json`; keep that file
beside `librvm.a` when copying runtime artifacts. A missing catalog is a build
error. Normal kernel compilation does not enable this user initialization path.

`UserBuilder.program` generates direct calls ordered by priority, with preinit
before constructors and destructors in reverse order. For archive callbacks,
an initial link discovers which archive members are needed. The GNU ld map
provides this information because FHR executables have no symbol table. A
second link adds callbacks from those members only. An unused member is not
pulled in merely because it contains a constructor. Link maps stay in the
host object directory and are not installed in the guest filesystem.

## Runtime lifecycle

Startup initializes `environ`, `__progname` and `__ps_strings` before invoking
the registry. Constructors can use the allocator, environment and stdio APIs.
Returning from main uses normal `exit`.

Exit drains registered atexit/CXA handlers, invokes the destructor registry,
drains any handlers registered by destructors, flushes stdio and performs the
exit syscall. The destructor registry advances its progress before calling
each function, so a recursive `exit` finishes the remaining destructors
without repeating the current one. `_exit` and `_Exit` bypass both callbacks
and stdio cleanup. Fork inherits the current initialization/finalization state;
exec starts a new program's initialization normally.

This is static bootstrap initialization, not the complete ELF crt0 protocol.
Dynamic initialization/DSOs, TLS, pthreads and complete libc/csu distribution
build integration remain outstanding.

The subsequent [emulated TLS update](rvm-tls-2026-11-09.md) adds C/C11 TLS
variables for the single-LWP runtime, including access during constructors,
atexit handlers and destructors. Native ELF TLS and pthreads remain unported.

## Guest checks

The normal disk fixture executes `/sbin/constructor-check`. It verifies
preinit before constructors, priorities across separate translation units,
startup environment/program-name and allocation/formatting access, reverse
destructor order, atexit ordering, handlers registered by a destructor,
`_exit` bypass and recursive `exit` without duplicate callbacks.
Raw ELF-style init/fini entries are interleaved by priority with GCC's legacy
constructor/destructor sections in the same executable.

The separate archive check verifies that a referenced member's constructor
initializes its state before main and its destructor runs on exit. An
unreferenced member contains a constructor that would terminate the process;
the test checks both its absence from the link map and successful execution.
It also executes the complete constructor fixture at `-O0`, `-O2` and `-Os`.

```sh
./build-diskroot.sh
python3 sys/arch/rvm/tests/hardfile.py --objdir obj.rvm-diskroot \
    --diskroot --expect-accel
python3 sys/arch/rvm/tests/constructors.py
```

Boot logs remain under `obj.rvm-diskroot/hardfile-tests/`; the archive and
optimization-level test transcript is `obj.rvm-diskroot/constructors-archive.log`.
Normal and DIAGNOSTIC images are rebuilt in `obj.rvm-diskroot/` and
`obj.rvm-diskroot-diag/`; the existing math/FPU, shell and filesystem tests
remain enabled in these images.

Validation passed normal and DIAGNOSTIC boot/persistence at 64/640 MiB,
the archive and mixed-array fixtures at all three optimization levels,
the native compiler/FPU regression, a software-runtime boot/persistence run
at 64 MiB, interactive shell/keyboard/keypad/pixel checks and FHR
binding/archive regressions.
