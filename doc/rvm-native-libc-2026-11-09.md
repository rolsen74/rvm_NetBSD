# Native NetBSD libc/csu build, 2026-11-09

The RVM port now builds the native static libc catalog through `lib/libc`'s
ordinary BSD makefiles, using headers installed by the normal `includes`
targets. The tested configuration contains 1,310 archive members, with 316 members in the optional native libm build. It selects
NetBSD's non-jemalloc allocator and keeps historical compatibility code outside
the native library. RVM has no older NetBSD binary ABI.

```sh
./build-userland.sh --libm --program bin/echo --program bin/cat \
    --program bin/pwd --program bin/sync
```

The default object directory is `obj.rvm-userland`, with its own host tools.
`--objdir`, `--tooldir`, `--toolchain` and `-j` override those locations.
The helper builds host tools, installs system/library headers, generates
libc dependencies, builds `libc.a` and static startup objects, and optionally builds `libm.a` and
selected programs with their ordinary `bsd.prog.mk` makefiles. An
external-toolchain fingerprint invalidates objects after compiler/adapter
changes. Repeating a build preserves up-to-date objects.

The build-tree SDK under `destdir.rvm/usr/lib` links back to the build artifacts.
FHR objects currently need their adjacent assembly and weak/initializer
metadata during final linking; copying `libc.a` alone is insufficient. This
is not yet an independently installable SDK or a distribution build.

## Native interfaces

* `MACHINE_CPU` now maps `rvm64` to `rvm`, and machine headers have normal
  architecture installation makefiles.
* `lib/libc/arch/rvm` supplies syscall macros and special veneers, nonlocal
  jumps, signal return, context construction/switching, IEEE
  constants/classification, and legacy FP control.
* RVM GAS treats semicolons as comments. Assembly macros use `RVM_ASM_NL`,
  expanded after preprocessing by `stand/user_cc.py`.
* Common definitions remain external; explicitly local common storage retains
  private names. This matters for libc's `environ`, `__progname` and
  `__ps_strings` definitions.
* The GCC NetBSD target now selects NetBSD's exact-width/least-width integer
  definitions, defines `__NetBSD__`, and uses signed `wint_t`. Its existing
  unsigned `wchar_t` representation is preserved. The incremental patch is
  `stand/toolchain/rvm-netbsd-types.patch`.
* Imported compiler runtime helpers provide binary32 operations and floating
  comparisons/conversions. The existing RVM word-result ABI adapters and GCC
  emulated TLS implementation are shared with the bootstrap runtime.
* The native libc/csu configuration remains single-LWP. Native ELF TLS and
  libpthread are not enabled.

Two MI wide-character fixes use an `int` sentinel from `__wctoint` and retain
`wint_t` while scanning floating input so that WEOF stays representable.
The nlist dispatcher accepts a port without symbol-table readers; FHR final
images have no symbol table and return ENOEXEC through that interface.

## Static startup and linking

`lib/csu/arch/rvm` supplies FHR startup rather than ELF startup metadata.
Startup sets the process argument/environment descriptors, runs preinit,
calls NetBSD `_libc_init()`, registers each selected destructor with
`atexit()`, invokes constructors, and calls `exit(main(...))`.

FHR `ld -r` cannot currently merge startup objects reliably. `fhr_csu.py`
combines preprocessed entry assembly with compiled startup assembly and
assembles one relocatable object instead.

`stand/user_link.py` selects archive members, prefers strong definitions over
weak fallbacks, resolves undefined weak references to null, and generates
constructor calls only for selected objects. Individual destructor registrations preserve
remaining destructors across a recursive `exit()`. It verifies that the linker
actually produced an FHR image and that `_start` is the first code byte.
`user_prog.py` provides this link step to ordinary program makefiles.

There are no ELF shared-library, unwind-frame, profiling or dynamic-linker
artifacts in this build. Dynamic symbol visibility attributes are ignored by
the current FHR compiler; the build keeps those warnings nonfatal.

The RVM `bzero` build disables builtin recognition because GCC otherwise
turns its own zeroing loop into recursive `bzero` calls. A native exit test
also exposed an MI atexit pool bug: the last active callback could reuse its
own descriptor when registering another callback, creating a cycle. An active
callback flag now excludes that descriptor from allocation until it returns.

## User atomics

The uncached uniprocessor CPU uses restartable 8/16/32/64-bit compare-and-swap
sequences registered by `__libc_atomic_init()`. MI CAS-based operations supply
the remaining public/GCC atomic interfaces. Initialization aborts if the
kernel cannot register the required sequences or reports more than one CPU.

The kernel advertises RAS support and rewinds interrupted user PCs before
returning from `mi_userret()`. Signal context capture already performs the
same adjustment. These primitives must be revisited for an SMP or cached
memory implementation.

## Validation and remaining integration

The guest suite passes at both 64 and 640 MiB, checking startup, stdio, errno, allocation/break tracking,
indirect syscalls, floating and wide I/O, FP control, signals, nonlocal jumps,
getcontext/setcontext, TLS, and CAS operations. A parent and child concurrently
increment the same shared-memory counter 100,000 times each. Both 32-bit and
64-bit C11 compare-exchange are checked.

Further programs verify preinit, constructor/destructor priorities, registration
of new exit handlers, recursive exit, TLS initialization/alignment, signal use,
fork/COW and exec reset. The makecontext/swapcontext test checks ten 64-bit
arguments (including stack arguments), linked returns, signal masks and FPU
rounding state. The normal libm passes the binary64 math/fenv test, including
directed decimal parsing/formatting. Its boundary checks disable GCC builtin
folding so that the actual library calls and exception flags are observed.

The normal makefiles build `echo`, `cat`, `pwd` and `sync`; those executables
also pass real guest command checks. The test automatically includes these
programs and libm when their native artifacts are present:

```sh
python3 sys/arch/rvm/tests/native_libc.py --objdir obj.rvm-userland
```

Use `--ram-mib 640` for the larger-memory run. For local iterations,
`--export-tree obj.rvm-userland/fixture-tree` saves a generated fixture tree;
subsequent `--reuse-tree obj.rvm-userland/fixture-tree` relinks the tests and
repacks it without rebuilding the bootstrap programs. The ordinary default
creates a fresh disposable fixture. The console driver accepts `--timeout`
and stops early if a child exits before producing its expected result.

The normal FHR binding/archive regression also passes with an added common
symbol shared across translation units. The kernel was rebuilt with RAS and
the adapter correction. An optional `RVM_KERNEL_CPPFLAGS=-DRVM_USER_FAULT_TRACE`
kernel build prints fault addresses/PCs; this flag is included in the kernel
build fingerprint and is disabled in the normal image.

The default disk-root image still uses the bootstrap runtime. Remaining work
includes libpthread/native TLS, more normal libraries and programs, production
init/root configuration, a standalone installable SDK and executable-format/
dynamic-linker support. Binary32 compiler runtime arithmetic/conversions still
have the earlier software-float rounding/exception limitations; this build
does not make those operations hardware FPU instructions.
