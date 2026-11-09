# NetBSD/RVM bring-up

The [native libc/csu build](../../../doc/rvm-native-libc-2026-11-09.md) builds
the normal static libc catalog and startup objects with BSD make, plus selected
native utilities. Guest checks cover user atomics, context switching,
constructors, TLS, math/fenv and utility execution. The default disk-root image
continues to use the bootstrap runtime while the remaining userland is ported.

The [emulated TLS update](../../../doc/rvm-tls-2026-11-09.md) adds C `__thread`
and C11 `_Thread_local` support for the single-LWP static runtime, including
constructor/destructor access, fork/COW and exec reset, plus LWP-private pointer
syscall veneers. Native ELF TLS and pthreads remain to be ported.

The [static initialization update](../../../doc/rvm-static-init-2026-11-09.md)
adds preinit, prioritized C constructors/destructors and initialization from
selected archive members, including normal and recursive process-exit checks.

The [libm/fenv update](../../../doc/rvm-libm-fenv-2026-11-09.md) adds the C
floating-point environment API, directed decimal formatting/parsing and a
binary64 NetBSD math runtime, with native sqrt and integer-rounding conversions.

The [FPU/GCC update](../../../doc/rvm-fpu-2026-11-08.md) adds native binary64
code generation with `-mfpu`, enabled by the userland builder, and preserves
all 64 FP registers and FPCR/FPSR across scheduling, fork/exec, signals,
setjmp/longjmp and ptrace. Kernel C code remains compiled without `-mfpu`.

The latest [userland update](../../../doc/rvm-stdio-shell-2026-11-08.md) adds
NetBSD stdio, software floating point, native utilities and the real NetBSD sh.
Type `sh` at the `rvm#` monitor prompt to start an interactive shell with a
writable root; `exit 0` returns to the monitor and syncs/remounts it read-only.

The [platform update](../../../doc/rvm-platform-2026-11-08.md) packs
the register windows into one MMIO page, adds host entropy, and enables physical
instruction caching and a TLB in the default disk-root build.

The normal MI kernel builds with NetBSD config and BSD make, initializes full
UVM and native devices, and reaches the root-device prompt in VMEmu. EARLY also
runs a real user process that prints through NetBSD write(2), sleeps, forks,
checks COW, handles signals on an alternate stack, and exits/waits. User timer
interrupts and fatal fault containment are checked. GENERIC now links and boots
with the same process interfaces and no boot-time test fixtures.

The HARDFILE backend now attaches as `ld0`. DISKROOT mounts a big-endian UFS2
root on `ld0a`, executes an FHR `/sbin/init` from disk, and opens `/dev/console`.
That small integration init reads a file, rejects malformed executables, and
forks/executes/waits for a GCC-compiled user program. The fixture has no libc
dependency. A console tty and a small static user runtime now support an
interactive `rvm#` monitor that reads files and launches C programs. NetBSD's
allocator, environment routines, exit handlers and PATH lookup now work, and
unchanged NetBSD utilities and `sh` are installed in the image. The library
now includes FILE streams, formatting/scanning, directory traversal and
C/POSIX locale support. More normal userland libraries/programs, native ELF TLS, pthreads,
dynamic linking and production init remain to be ported.
See the [libc progress notes](../../../doc/rvm-libc-2026-11-08.md).
The earlier [kernel-only instruction-cache configuration](../../../doc/rvm-icache-2026-11-08.md)
remains available with `./build-diskroot.sh DISKROOT_ICACHE`; that configuration
leaves user/data caching and the TLB disabled.
See the [console/user-runtime notes](../../../doc/rvm-console-userland-2026-11-08.md).
See the [disk-root notes](../../../doc/rvm-diskroot-2026-11-07.md) and earlier
[user-process notes](../../../doc/rvm-user-process-2026-11-07.md).

```sh
./build-diskroot.sh
./run-diskroot.sh
```

This uses `obj.rvm-diskroot/` and regenerates a disposable 16 MiB root image.
The init fixture finishes its checks and execs the C command monitor as PID 1.
The separate disk MMIO page consumes virtual address space, with one 64 KiB
RAM bounce page in the driver. A normal EARLY/GENERIC boot without HARDFILE
still recovers the absent-device probe and reaches the root-device prompt.

```sh
./build-kernel.sh
./run-kernel.sh
```

The helper builds the required host tools, uses the sibling external NetBSD
toolchain, and writes the normal image and loader under `obj.rvm-kernel/`.
`RVM_TOOLCHAIN`, `RVM_KERNEL_OBJDIR`, `RVM_KERNEL_TOOLDIR` and `RVM_JOBS` override
its paths/job count. `./build-kernel.sh EARLY_DIAGNOSTIC` enables MI assertions.
The complete kernel build log is useful when a new MD interface is required.
The [normal-kernel session notes](../../../doc/rvm-normal-kernel-2026-11-07.md)
describe the interfaces, compiler fixes, tests and remaining user-mode work.

```sh
python3 sys/arch/rvm/tests/kernel_boot.py
python3 sys/arch/rvm/tests/normal_tools.py
python3 sys/arch/rvm/tests/hardfile.py
python3 sys/arch/rvm/tests/hardfile.py --objdir obj.rvm-diskroot --diskroot
python3 sys/arch/rvm/tests/console.py
```

EARLY adds test fixtures; GENERIC uses the same syscall/trap/signal code.
The console now uses MI tty queues, canonical/raw reads, echo, termios and
polling of GeneralDriver ASCII key-down events. Core-file output and native ELF TLS
remain unsupported. The entropy warning is expected in configurations without
a registered seed/RNG. The normal kernel uses slow MI softints;
user return handles MI rescheduling and pending signals.

The remaining sections record historical standalone stages; their pending-work
statements describe those stages. Current normal-kernel status is summarized
above and in the user-process notes.

The standalone regression image is a separate startup for the RVM CPU, using the
FHR bootloader and GeneralDriver interface adapted from SnakeOS. It now links
NetBSD's MI UVM physical-segment registry and raw-page allocator, initializes
managed-page metadata/free queues, and verifies allocation and freeing.
An early wired buffer allocator combines managed pages with contiguous
kernel virtual addresses tracked by real MI UVM map entries and returns
both resources on free or failure. Each buffer now owns a resident UVM object;
its managed pages are indexed by offset and wired through real MI routines.
The real MI `uvm_km_alloc()`/`uvm_km_free()` path also supports non-sleeping
wired allocations and VA-only reservations in the bootstrap kernel map.
An MI-vmem-backed bootstrap heap now provides non-sleeping `kmem_alloc()`,
`kmem_zalloc()` and `kmem_free()` with eight-byte alignment.
Constrained MI pools now allocate fixed-size items from wired pages, with
checked ownership and explicit destruction to release idle backing pages.
MI pool caches now retain constructed objects in real per-CPU magazines,
with checked client ownership, explicit invalidation and teardown.
Resident anonymous descriptors now use MI `uvm_analloc()`/`uvm_anfree()`,
real page ownership and dirty accounting, and checked reference operations.
Bounded MI anonymous maps now group those descriptors in sparse slots and
support references to a whole map, with a shared owner lock.
Eager anonymous mappings now attach those maps to real kernel VM entries;
shared writable views retain their pages until the last entry is detached.
The MD `cpu_switchto()` path now switches between kernel LWP stacks and
preserves the compiler's callee-saved registers. This is a prerequisite for
scheduler integration; the tests drive cooperative switches explicitly.
Supervisor timer IRQs can now interrupt busy C code, defer callbacks to the
return boundary, and preempt busy system LWPs through MI `preempt()`/`mi_switch()`.
Uptime and LWP runtime use the GenericDriver free-running clock; the host
RTC initializes UTC, and monotonic deadlines support timed kernel-LWP sleep.
Non-interruptible MI condition variables, wait-channel wakeup and kernel pause
now release/reacquire interlocks through real MI sleep queues and callout timeouts.
Generic MI software interrupts now dispatch on four guarded kernel LWP stacks;
an optional dedicated softclock LWP runs MI callouts and wakes CV sleepers.
Managed kernel mappings now track every dynamic alias, retain hardware read/write
history, support unwired entries and revoke permissions across all aliases.
The corrected GCC argument ABI also enables NetBSD's real format parser,
stacked `printf` arguments and bounded `snprintf` buffers.
That test image halts after its checks. `./run.sh` still launches this stage-51
regression image; `./run-kernel.sh` launches the normal kernel described above.
`build.sh -m rvm` now recognizes the external-toolchain kernel target. Full
distribution/userland builds and an in-tree RVM compiler are not integrated.

## Build and run

Install your updated RVM GCC/binutils toolchain before building. The local
[toolchain notes](stand/toolchain/README.md) include the caller-home/stack/varargs
patch and the FHR undefined-symbol diagnostic patch. A clean rebuild is required
when changing the argument ABI.

This repository uses the installed cross compiler directly. The build runs a small codegen
probe and rejects compilers that can destroy flags between a comparison and
its branch or crash on widening multiplication at `-O2`.

GCC needs the symbolic pointer-store fix in the maintained sibling source
`rvm_gcc/_data/work_gcc-14.3.0/gcc/config/rvm/rvm-inst.md`. The DI move
alternative that stores an immediate to memory must use numeric constraint
`n`, so reload cannot turn an address store back into a memory read. If your
installed GCC predates that correction, rebuild it before rebuilding this port.
The backend also needs the atomic compare/branch fix in
`rvm_gcc/_data/work_gcc-14.3.0/gcc/config/rvm/rvm-brance.md`. All four integer
comparison widths now emit the compare and conditional branch as one RTL
instruction. Without it, GCC can generate `CMP; ADD; BNE`, where the branch
uses the add's flags and loops past an array. Disabling instruction scheduling
does not prevent that transformation.

The widening-multiply fix in `rvm-inst.md` and `rvm-inst-v.md` is also
required. Extension inputs must be registers or memory; constants belong
in the widened result mode. Previously `(unsigned long)n * 356` created
invalid RTL and crashed in `combine.cc:do_SUBST`. Both signed and unsigned
8/16/32-bit forms are corrected. Rebuild GCC, then clean-build the port:

```sh
cd ../rvm_gcc
rm -f _touch/build_cross_netbsd_gcc
make -f makefile_netbsd "$PWD/_touch/build_cross_netbsd_gcc"
```

From the source root, use GNU make with `rvm-netbsd-gcc`, `rvm-netbsd-ld`,
`VMAsm`, `VMEmu`, and Python 3 in `PATH`:

```sh
make -f sys/arch/rvm/Makefile.bootstrap clean
make -f sys/arch/rvm/Makefile.bootstrap
make -f sys/arch/rvm/Makefile.bootstrap run
```

For the sibling GCC checkout in this workspace:

```sh
make -f sys/arch/rvm/Makefile.bootstrap \
    CROSS_COMPILE="$PWD/../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-" run
```

`OBJDIR=/absolute/path` selects another output directory; the default is
`obj.rvm-bootstrap/`. The build produces:

- `netbsd.fhr`: the standalone NetBSD/RVM startup executable.
- `bootloader.bin`: the adapted SnakeOS FHR loader, assembled as raw code.
- `vm.config`: a VMEmu configuration with GeneralDriver and configurable RAM
  in 64 KiB pages (`1024` = 64 MiB; `10240` = 640 MiB).
- `netbsd.map`: the GNU linker map.

`./run.sh` at the source root prints the image's absolute path and starts it.
It refreshes `obj.rvm-bootstrap/vm.config` when the source configuration in
`sys/arch/rvm/conf/vmemu.conf` is newer, so RAM changes apply on the next launch.
If you see 256 pages or an image size around 142 KiB, you are running an older
copy. Memory size follows the source configuration, and current startup prints
the kernel-allocator verification below. The `run`
target starts VMEmu from the output directory, where the config's relative
file paths resolve. To launch a previously built image directly:

```sh
cd obj.rvm-bootstrap
VMEmu -c vm.config
```

The configuration concatenates the raw loader and FHR image in RAM. The
loader allocates sections on 64 KiB boundaries, copies data, clears BSS,
applies relocations, and enters the first code section in supervisor mode
with a versioned boot-information pointer in `r2`.
The linker uses its default layout with `_start` selected explicitly;
no custom linker script is required. The map's link addresses differ from
the runtime section addresses assigned by the loader.

## Startup behavior

`locore.S` disables interrupts and translation, selects a private 128 KiB
supervisor stack, installs 96 fatal exception vectors, and calls C startup.
Caches and interrupts remain disabled throughout this milestone. The loader
supplies the detected contiguous RAM bank and every relocated FHR section.
The kernel validates the handoff, reserves the entire loaded prefix (including
the raw loader, raw FHR, section metadata, kernel stack and vectors), and
conservatively retains the final RAM page used by the loader's stack.
A bounded physical allocator reserves further space for tables and the
translation-test page. The bootstrap initializes the GeneralDriver terminal at `0x0700000100000000`, using
SnakeOS's 800 by 600 display and 8 by 8 character cells. Every character is
also mirrored to VMEmu stdout through EmuFunc command 3 (`r2` command,
`r3` character).

Example output with `Size = 1024` (64 MiB):

```text
NetBSD/RVM early bootstrap
Supervisor SR: 0x0000000000000024
Kernel stack: 0x... ..0x...
Exception vectors installed; MMU and interrupts disabled.
RAM end: 0x0000000004000000
Reserved prefix end: 0x00000000000c0000
Supervisor MMU root: 0x00000000000c0000
Free physical range for UVM: 0x00000000000f0000..0x0000000003ff0000
MMU enabled; non-identity mapping verified.
Kernel pmap ready; bootstrap allocator sealed for UVM handoff.
UVM registered PFN range: 0x000000000000000f..0x00000000000003ff
UVM bootstrap allocation: 0x00000000000f0000
UVM physical memory registered; bootstrap allocation verified.
UVM managed pages: 0x00000000000003ed
UVM page allocator ready; allocation/free verified.
Early wired allocator ready; zeroed allocation/free verified.
UVM kernel VA map ready; reservations and wired buffers verified.
UVM resident objects ready; page ownership and wired allocation verified.
UVM kernel allocator ready; wired and VA-only allocation/free verified.
Bootstrap kmem heap ready; subpage allocation and span release verified.
Bootstrap MI pools ready; item allocation and page release verified.
Bootstrap MI pool caches ready; constructors and magazine reuse verified.
UVM anonymous pages ready; references, zeroing and release verified.
UVM anonymous maps ready; sparse slots, sharing and release verified.
UVM anonymous mappings ready; VM entries, shared aliases and detach verified.
RVM kernel context switching ready; separate LWP stacks and resume verified.
RVM kernel LWPs ready; fork setup and MI startup handoff verified.
RVM MI switching ready; resumed LWPs, zombie handoff and reaping verified.
RVM MI run queues ready; runnable selection, yield and worker exit verified.
RVM MI idle LWP ready; empty queues, sleep and wakeup verified.
RVM supervisor trap return ready; registers, status and stack restored.
RVM timer interrupt ready; HALT wakeup and interrupt return verified.
RVM repeating timer wakeups ready; idle deadlines and rearming verified.
Bootstrap MI clock ready; hardclock ticks and callout deadlines verified.
Bootstrap MI timecounter ready; uptime and timestamp reads verified.
RVM MI runtime accounting ready; running and sleeping LWP time verified.
RVM guarded u-areas ready; LWP stack ownership and release verified.
RVM MI LWP VM lifecycle ready; u-area attachment, fork and exit verified.
RVM RTC ready; host UTC initialized through MI timecounter.
RVM delay ready; elapsed time verified with interrupts masked.
RVM MI wall clock ready; UTC steps preserve monotonic uptime.
RVM monotonic deadlines ready; HALT waits never return early.
RVM timed LWP sleep ready; timeout, early wake and cancellation verified.
RVM asynchronous clock ready; busy IRQs, masking and elapsed tick catchup verified.
RVM timer preemption ready; busy LWPs switched through MI scheduler.
RVM busy IRQ return ready; general registers and stack preserved.
RVM asynchronous kernel services ready; heap, callouts and timed sleeps verified.
RVM MI condition variables ready; sleep-queue handoff and signal verified.
RVM MI condition timeouts ready; timeout and early-signal cancellation verified.
RVM MI kernel pause ready; timed sleep and interlock reacquisition verified.
RVM MI wait channels ready; wakeup, timeout and no-relock verified.
RVM MI bintime waits ready; timeout conversion and remaining time verified.
RVM MI software interrupts ready; priority, coalescing and LWP dispatch verified.
RVM MI softclock LWP ready; IRQ callouts and CV wakeups verified.
RVM MI printf ready; stack arguments, precision and bounded buffers verified.
RVM managed pmap ready; aliases, read/write tracking and page protection verified.
RVM kernel copies ready; MMU fault return and PCB recovery verified.
```

Addresses and condition-code bits can vary. After printing, the CPU halts
with translation enabled; the emulator window remains open until closed. This is the intended stopping
point for this startup milestone.

`rvm_boot_stage` is 1 on entering C, 2 after console setup, 3 after memory
validation, 4 after page-table construction, 5 after enabling the MMU,
6 after verifying translated memory accesses, 7 after testing a wired
mapping and its removal in the kernel VM window, 8 after UVM preload
registration and allocation verification, 9 after managed-page allocation
and freeing, 10 after wired buffer allocation and freeing, 11 after
verification using the MI kernel VA map, and 12 after object-owned page
allocation and freeing, and 13 after MI wired/VA-only kernel allocation
verification, 14 after bootstrap heap verification, 15 after MI pool
allocation/destruction verification, 16 after MI pool-cache constructor,
magazine reuse and destruction verification, 17 after anonymous-page
reference, zeroing and final-release verification, 18 after sparse
anonymous-map slots, shared references and final wipeout verification, and
19 after anonymous VM-entry attachment, shared aliases and MI detach, and
20 after kernel LWP stack switching and resumption, and 21 after kernel
LWP fork setup and MI first-entry handoff, and 22 after directed MI switches,
zombie unlock and stack reaping, 23 after MI run-queue selection and yield,
24 after idle-LWP empty-queue selection and sleep/wakeup, and 25 after
verified supervisor software-trap return, 26 after timer IRQ/HALT return, and
27 after repeating timer IRQs wake sleeping LWPs through idle deadlines, and
28 after MI hardclock ticks and callout deadlines, and 29 after MI
timecounter uptime and timestamp reads, 30 after MI LWP runtime accounting,
31 after guarded system-LWP u-area allocation/release, and 32 after the
MI LWP VM attachment/fork/exit path. Stages 33 through 37 add RTC UTC
initialization, busy delay, MI wall-clock steps, absolute monotonic HALT waits,
and timed LWP sleep with early-wake cancellation. Stages 38 through 41 add
asynchronous hardclock/catchup, MI timer preemption, full busy GPR/SP return,
and guarded heap/callout/RTC services with asynchronous sleepers. Stages 42–46
add MI condition-variable handoff, CV timeout/cancellation, kernel pause,
wait-channel wakeup/no-relock, and bintime-based waits. Stages 47 and 48 add
generic MI software interrupts and timer-driven callouts on a softclock LWP.
Stage 49 checks the real MI formatter when the compiler advertises the corrected
argument ABI; stage 50 checks managed kernel aliases, hardware access history
and page-wide permission revocation. Stage 51 verifies kernel copying and
return from an expected supervisor MMU fault through RTE.
A fatal exception sets it to
`0xff`, reports the hardware vector,
frame version, saved PC and low SR flags, then halts. All 63 non-SP integer
registers are saved before entering the fatal C handler. These handlers do
not return, implement NetBSD trap dispatch, or restore a process context.
A corrupt/unmapped supervisor stack cannot be recovered by this handler.

## Toolchain constraints

The build uses this tree's NetBSD headers, without SDK or host C libraries.
`abi-check.c` checks integer sizes, byte order, alignment, saved-register
layouts, format strings, and GeneralDriver register offsets. Run only the
compile checks with the `check` target.

The ABI follows the local GCC 14.3.0 backend: LP64, big-endian, unsigned
plain `char`, unsigned 32-bit `wchar_t`, and 4-byte scalar/stack alignment.
The private startup stack is aligned more strictly. Explicit integer
headers accommodate the compiler's missing exact-width macros; extended
integer headers derive from NetBSD/alpha. Signal delivery still needs
implementation; the integer limits reserve a signed 32-bit `sig_atomic_t`.

Current toolchain workarounds are confined to this port:

- VMAsm assembles the preprocessed CPU entry code because GNU as does not
  fully encode control-register moves. C code uses GCC and GNU as.
- Exported stack and vector storage, progress and cursor state use initialized
  data sections because the current GNU FHR BSS symbol handling is incomplete.
  This increases the image by about 129 KiB.
- Diagnostics use named strings instead of temporary `.LC` symbols, which the
  current GNU FHR tools can lose. Constant merging is disabled.
- VMAsm emits the standard `kernel_pmap_ptr` constant in read-only data because
  GCC's RVM backend cannot yet emit symbolic pointer initializers.
- With older GCC builds, VMAsm captures seven register arguments for early
  `printf`/`panic`, supporting `%s`, `%d`, `%u`, `%x`, `l`/`ll` and `%%`.
  GCC with `__RVM_CALLER_ARG_HOME__=64` saves incoming varargs and reserves
  outgoing stack arguments correctly. That compiler selects real MI `kprintf`,
  `snprintf` and `vsnprintf` bodies, with guarded console/panic wrappers and no
  logging/tty dependencies. All assembly calls into C reserve its 64-byte
  caller home area. Both compiler ABIs remain buildable; C objects from the
  two ABIs must not be mixed.
- `stand/fhr_asm.py` normalizes GCC's assembly for the MI sources: it promotes
  `.LC` strings to named symbols and converts common/BSS storage to explicit
  zero-filled data. The installed FHR tools lose
  local string references and mishandle common storage. Other bootstrap C
  sources retain their explicit data/named-string workarounds.

## Memory bootstrap and validation

`pmap_bootstrap()` builds six-level, 256-entry RVM tables using 2 KiB table
alignment and 64 KiB pages. Its temporary supervisor address space contains:

- Identity mappings for RAM above page zero. Code is readable/executable;
  read-only data stays read-only; writable data, stacks, tables and free RAM
  are readable/writable and non-executable. All mappings are supervisor-only.
- An uncached, non-executable GeneralDriver register mapping. The framebuffer
  is used by the emulator plugin and is not directly mapped for CPU access.
- A scratch physical page mapped at `0xffff800000000000`, separately from its
  identity address. Startup tests access both ends of this 64 KiB page through
  the alias and physical mapping, with CR's MMU bit enabled.

Page zero and the page following the scratch alias remain unmapped. The
allocator exposes a page-aligned free physical range that excludes all
bootstrap allocations and the loader's final stack page. After constructing
the tables, startup seals the allocator: further allocations and attempts to
reinitialize it fail. UVM then owns the available range, as described below.
This temporary
identity layout is not the final kernel virtual-address policy.

The initial wired kernel VM window is 256 MiB at `0xffff800001000000`.
Bootstrap preallocates its table paths, leaving the leaf entries unmapped.
`pmap_kenter_pa`, `pmap_kremove`, `pmap_extract`, `pmap_remove`, `pmap_protect`,
`pmap_update`, `pmap_virtual_space`, page zero/copy, and resident/wired counts
now implement NetBSD's kernel mapping interfaces. Replacement does not change
mapping counts; protection only removes permissions. Mapping edits are limited
to this window so they cannot accidentally unmap the running bootstrap.
`pmap_kenter_pa()` mappings remain wired; `pmap_enter()` also supports unwired
managed mappings and `pmap_unwire()` updates their wiring counts. All mappings
remain supervisor-only and uncached. Cache flags zero and `PMAP_NOCACHE` are
accepted. Invalid mapping edits halt with a
diagnostic. Tables remain allocated when mappings are removed.

With CR equal to 8, VMEmu walks the page tables for each access and has no
separate TLB. `pmap_update` checks that mode and supplies a compiler barrier.
Cache-enabled execution, SMP, user pmaps and the remaining process-pmap API
remain unimplemented. Mapping mutations and access-history operations now use
IRQ guards, including when called with the asynchronous timer enabled.

## UVM physical preload

Startup links `sys/uvm/uvm_physseg.c` without source changes. The standalone
`uvm_bootstrap.c` harness supplies actual `struct uvm`/`struct uvmexp` storage,
sets the 64 KiB page constants, initializes the physseg registry, and calls
`uvm_page_physload()` with page-frame numbers. Only the available physical
range is registered; the loaded prefix, page tables, scratch page and final
loader stack page cannot be allocated through this registry.

`uvm_machdep.c` implements `pmap_steal_memory()`. It obtains contiguous pages
through MI `uvm_page_physunload()`, which removes them from the registered
range, and zeroes them through the temporary RAM identity map. Requests round
up to whole pages. The high kernel VM window is not consumed. The bootstrap
allocator stays sealed, and no additional page tables are allocated.
One page is permanently stolen and tested during startup; its address is
printed. The implementation conservatively retains at least one free page
in the selected bank so the preload registry cannot become empty.

## Managed UVM pages

Startup next calls the real `uvm_page_init()` in `sys/uvm/uvm_page.c`. It
allocates the `vm_page` array and freelist buckets from the preload registry,
initializes metadata using `uvm_physseg_init_seg()`, and places managed pages
on MI free queues. Stolen pages, including metadata, are excluded from managed
lookups. `uvm_pagealloc_strat()` and `uvm_pagefree()` operate on those queues.
Their standalone wrappers accept raw pages, registered resident objects or
registered anonymous descriptors,
with normal strategy and `UVM_PGA_ZERO`/`UVM_PGA_USERESERVE` flags.
Unknown objects or anons, duplicate/out-of-bounds object offsets,
missing owner locks, invalid frees and double frees halt before entering the
MI allocation/free bodies.
Raw requests also accept `UVM_FLAG_COLORMATCH` with a page-aligned offset
within the bootstrap arena, as required by MI wired kernel allocation.
Startup allocates a zeroed raw page, maps/writes it, removes the mapping and
frees it, checking the physical address and restored per-CPU free-page count.
Managed-page counts follow the loaded image and its metadata reservations.
The regression suite boots with 2, 16, 64 and 640 MiB.
At 1 MiB, the current image cannot complete page-table construction; startup
stops before MMU activation. The exact low-memory stopping point depends on
image size and the pages needed for metadata and reserves.

The standalone build compiles unmodified MI sources for page allocation,
physical segments, fixed-storage extents, the clock replacement policy,
percentage parameters, object initialization, dirty status, radix trees and
the UP freelist-cache implementation, and wired kernel allocation/free. Selected
entry points are retained with `-fwhole-program` and
`stand/uvm_profile.h`. Event/sysctl registration is disabled, and unavailable
object-pager paths stop explicitly. Full kernel builds do not use this profile.

`uvm_support.c` supplies a bootstrap LWP, CPU counters, logical IPL bookkeeping
and checked mutex/rwlock state for one CPU with CR exactly 8. This prevents the absent
current LWP from being mistaken for the absent pagedaemon. Recursive locking
and invalid unlocks stop. Unsupported pool/cache and pager services stop if reached. Checked
non-interruptible condition-variable and wait-channel sleep is now connected
to the MI scheduler, as described at the current milestone below. Requests to wake a pagedaemon
are recorded; allocation still returns NULL at reserve limits. This support
file is excluded from normal kernel configuration, where the real kernel
services must replace it. Checked nesting on the bootstrap LWP now supports
non-preemptible dirty-page counter updates; it does not enable scheduling.

This initializes managed pages, resident objects and wired kernel allocation,
not `uvm_init()`. Full cached arena/heap startup, demand faults and object pagers,
file-backed objects,
paging daemons, user mappings and PV tracking
remain pending. Interrupts and caches stay disabled. The MD context/process
headers now support kernel-only stack switching; MI scheduler startup,
signal delivery and user context switching remain pending.

VMEmu's uncached MMU access loop must advance to the next **page base** when
an access crosses a page boundary. Advancing by the fragment length instead
corrupts cross-page reads/writes, including saved register frames on a stack.
The source correction is in `rvm_VMEmu/src_Emulator/Memory/Mem.c`.

`make test` now defaults to `TEST_SUITE=new`, which selects only the current
MI sleep-queue/CV checks. `TEST_SUITE=wait` selects the same scope;
`TEST_SUITE=asyncirq` selects asynchronous interrupt/preemption checks; `TEST_SUITE=platformclock` selects platform clock and timed-sleep checks; `TEST_SUITE=lwpvm` selects MI LWP VM lifecycle checks;
`TEST_SUITE=uarea` selects guarded stack checks,
`TEST_SUITE=runtime` selects compiler-division and MI runtime checks, `TEST_SUITE=timecounter` selects uptime checks,
`TEST_SUITE=hardclock` selects MI tick/callout checks,
`TEST_SUITE=clock` selects repeating timer wakeup checks,
`TEST_SUITE=irq` selects one-shot hardware timer IRQ checks,
`TEST_SUITE=trap` selects supervisor trap-return checks,
`TEST_SUITE=idle` selects idle/sleep/wakeup, `TEST_SUITE=runq` run queues,
`TEST_SUITE=switch` selects directed handoffs, `TEST_SUITE=lwp` first-entry
checks and `TEST_SUITE=context` the earlier stack-switch checks. Each
probe still performs normal bootstrap initialization before testing the new
path; the older emulator cases are not rerun by default.

```sh
make -f sys/arch/rvm/Makefile.bootstrap test
```

Run the full emulator regression set explicitly with:

```sh
make -f sys/arch/rvm/Makefile.bootstrap \
    CROSS_COMPILE="$PWD/../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-" TEST_SUITE=full test
```

These check FHR relocation offsets, RAM-size-dependent reservations,
insufficient-memory failure,
unsigned compiler comparisons, malformed boot records, non-identity memory
access, wired mapping replacement across table boundaries, page zero/copy,
mapping counts, allocator sealing, UVM ownership/stealing, managed metadata,
page reuse/zeroing, allocation uniqueness, reserve exhaustion, restored free
counts, bootstrap locks, allocation error paths and variadic diagnostics.
Preload-specific probes stop at stage 8 before page initialization closes
physical stealing. Cross-page probes use
unaligned 64-bit accesses backed by noncontiguous physical pages.
Hardware probes check null reads,
writes to code/read-only data, execution from data, and accesses after kernel
mapping removal or permission downgrades. They use SDL's dummy video/audio
drivers. GeneralDriver MMIO writes are exercised, but graphical text has not
been visually inspected in those
headless runs. Full exception return and recovery are not implemented.

## Early wired buffers

The standalone `bootalloc.c` harness provides `rvm_bootalloc(size)` and
`rvm_bootfree(base)` after managed page initialization. It uses NetBSD's
real MI UVM map implementation for virtual address reservations and the
real object-owned page allocator for backing memory. Buffers round up to 64 KiB pages,
are fully zeroed, and use supervisor read/write, non-executable mappings.
Backing pages need not be physically contiguous. Allocation returns zero on
invalid sizes or resource exhaustion; partial allocations remove their
mappings and return all pages and virtual space before failing.

The arena occupies the final 16 MiB of the preallocated 256 MiB kernel VM
window, with at most 32 live buffers. Callers must reserve that range for this
allocator and must not independently change its mappings or free its backing
pages. Only an exact live base may be freed; interior, unknown and double
frees return false. Each buffer has a resident object, linked from its map
entry. Pages are wired in both MI metadata and pmap, outside page-replacement
queues. These are
bootstrap buffers, separate from `kmem_alloc()` and `uvm_km_alloc()`.

This requires one CPU with interrupts/caches disabled and no scheduler.
The fixed map-entry store avoids depending on a heap or sleep support. Full kernel
builds exclude the harness and must use normal UVM startup and kernel allocators.
Startup verifies a two-page buffer, then aligned MI wired and VA-only
allocations, the bootstrap heap, MI pools, pool caches and anonymous pages,
as part of the sequence leading to stage 51 and printing:

```text
Early wired allocator ready; zeroed allocation/free verified.
UVM kernel VA map ready; reservations and wired buffers verified.
UVM resident objects ready; page ownership and wired allocation verified.
UVM kernel allocator ready; wired and VA-only allocation/free verified.
Bootstrap kmem heap ready; subpage allocation and span release verified.
Bootstrap MI pools ready; item allocation and page release verified.
Bootstrap MI pool caches ready; constructors and magazine reuse verified.
UVM anonymous pages ready; references, zeroing and release verified.
UVM anonymous maps ready; sparse slots, sharing and release verified.
UVM anonymous mappings ready; VM entries, shared aliases and detach verified.
RVM kernel context switching ready; separate LWP stacks and resume verified.
RVM kernel LWPs ready; fork setup and MI startup handoff verified.
RVM MI switching ready; resumed LWPs, zombie handoff and reaping verified.
RVM MI run queues ready; runnable selection, yield and worker exit verified.
RVM MI idle LWP ready; empty queues, sleep and wakeup verified.
RVM supervisor trap return ready; registers, status and stack restored.
RVM timer interrupt ready; HALT wakeup and interrupt return verified.
RVM repeating timer wakeups ready; idle deadlines and rearming verified.
Bootstrap MI clock ready; hardclock ticks and callout deadlines verified.
Bootstrap MI timecounter ready; uptime and timestamp reads verified.
RVM MI runtime accounting ready; running and sleeping LWP time verified.
RVM guarded u-areas ready; LWP stack ownership and release verified.
RVM MI LWP VM lifecycle ready; u-area attachment, fork and exit verified.
RVM RTC ready; host UTC initialized through MI timecounter.
RVM delay ready; elapsed time verified with interrupts masked.
RVM MI wall clock ready; UTC steps preserve monotonic uptime.
RVM monotonic deadlines ready; HALT waits never return early.
RVM timed LWP sleep ready; timeout, early wake and cancellation verified.
RVM asynchronous clock ready; busy IRQs, masking and elapsed tick catchup verified.
RVM timer preemption ready; busy LWPs switched through MI scheduler.
RVM busy IRQ return ready; general registers and stack preserved.
RVM asynchronous kernel services ready; heap, callouts and timed sleeps verified.
RVM MI condition variables ready; sleep-queue handoff and signal verified.
RVM MI condition timeouts ready; timeout and early-signal cancellation verified.
RVM MI kernel pause ready; timed sleep and interlock reacquisition verified.
RVM MI wait channels ready; wakeup, timeout and no-relock verified.
RVM MI bintime waits ready; timeout conversion and remaining time verified.
RVM MI software interrupts ready; priority, coalescing and LWP dispatch verified.
RVM MI softclock LWP ready; IRQ callouts and CV wakeups verified.
RVM MI printf ready; stack arguments, precision and bounded buffers verified.
RVM managed pmap ready; aliases, read/write tracking and page protection verified.
RVM kernel copies ready; MMU fault return and PCB recovery verified.
```

The emulator tests additionally cover multi-page zeroing, exact ownership,
address reuse, fragmentation, all 32 descriptor slots, physical exhaustion
with partial-allocation rollback, virtual arena exhaustion, restored free-page
and pmap counts, and hardware faults for execution and access after free.

## Kernel VA map

`bootmap.c` initializes a real `struct vm_map` and `kernel_map` for the
buffer arena. It calls MI `uvm_map_init()`/`uvm_map_setup()`, then the real
`uvm_map()` body to reserve addresses, and `uvm_unmap_remove()` and
`uvm_unmap_detach()` to release entries. The standalone compilation renames
the general mapping body to `rvm_mi_map`; callers use the constrained
`rvm_bootmap_reserve()`/`rvm_bootmap_release()` interface. Fixed addresses,
alignment and overlap checks, gap metadata, hints and red-black-tree operations
come from MI code, including the common NetBSD red-black-tree implementation.
The buffer allocator now uses this map in place of its earlier extent arena.

Plain reservations are VA-only, with no object, amap, protection or physical
mapping. `rvm_bootmap_reserve_object()` instead records a resident object,
offset zero, and read/write protection in the real map entry. It holds a
reference that prevents object destruction until the entry is released.
Releases require an exact entry and use `UVM_FLAG_VAONLY`; the caller owns
physical-page cleanup. Buffers are mapped eagerly by `bootalloc.c`.
Neither reservation API faults in pages or supports user mappings.
There are 32 shared, statically stored map entries in place of the unavailable
map-entry pool cache. All requests are non-sleeping and non-merging, so exact
release needs no additional entries. Unsupported pager, amap, fault and
scheduler paths stop explicitly. The bootstrap heap uses this map through
the wired km allocator.

`uvm_support.c` supplies checked reader/writer lock state in public rwlock
storage for the same single-CPU, interrupts-disabled bootstrap environment.
Readers may nest; writers exclude all other holders, try-lock can fail, and
blocking contention or invalid unlocks halt. This does not implement kernel
sleep queues or full MI rwlock machinery.

The MI map source remains unchanged. `stand/map_source.py` makes a generated
bootstrap-only copy that assigns its two tree callbacks at runtime, because
the current FHR GCC cannot emit static function-pointer initializers. The
adapter checks its expected input before transforming it. The existing
assembly normalization then handles local strings and common storage.

Map tests check ordered list/tree agreement and gap metadata after permuted
insertions/removals, half-open lookup boundaries, aligned and fixed requests,
overlap rejection, invalid/partial/double releases, entry exhaustion, VA
exhaustion, restored counts and reader/writer exclusion. The existing wired
buffer tests also exercise map rollback after physical allocation failure.

## Resident UVM objects

The standalone `bootobject.c` layer calls real MI `uvm_obj_init()`,
`uvm_obj_setlock()` and `uvm_obj_destroy()` with statically stored locks.
Its private resident pager type has no I/O callbacks and is distinct from
NetBSD's anonymous, vnode and device pagers. There are 32 reusable object
slots. An object can describe a sparse page-aligned range; offset validation
rejects negative, unaligned and out-of-range requests.

`rvm_bootobj_alloc()` uses real `uvm_pagealloc_strat()` insertion into the
object's radix tree, zeroes the page, marks its contents valid and calls real
`uvm_pagewire()`. Lookups use MI `uvm_pagelookup()`. Object destruction requires
an empty page tree, no lock holder and no map references. Freeing removes
the page from the tree and unwires it through the MI page-free routine.
Callers must remove all pmap aliases before freeing object pages; PV tracking
is still pending. The wired buffer API does this automatically, then releases
its map entry and object. Partial allocation failure unwinds the same resources.

The common MI radix-tree implementation uses a bootstrap-only store of 128
nodes of 128 bytes each. Its allocation hooks replace only the radix source's
`kmem_intr_alloc()`/`kmem_intr_free()` calls. This does not provide a general
heap. Node exhaustion returns failure through real radix insertion and page
allocation paths; freeing objects returns their node storage for reuse.
The node size is checked at runtime against allocation requests so an MI
layout change cannot overrun the store.

MI dirty-status routines maintain page flags and radix tags. Resident pages
start dirty after zeroing. Explicit CLEAN/DIRTY transitions are supported under
the object lock; UNKNOWN status, which needs managed pmap modification-bit
tracking, stops explicitly. There is no background writeback, page-fault
handling, anonymous swap pager or UBC yet.

The tests cover sparse offset lookup, zeroing, duplicate/out-of-range requests,
dirty tags, live-object destruction rejection, map reference lifetime,
wire counts, object/node exhaustion, failed insertion with existing pages,
and restoration of physical pages, map entries and node storage.

## MI wired kernel allocation

The standalone build now retains the real `uvm_km_alloc()`, `uvm_km_free()`
and batched `uvm_km_pgremove_intrsafe()` bodies from `sys/uvm/uvm_km.c`, plus
MI `uvm_unmap1()`. The standard `kernel_map` global comes from the MI km source;
the bootstrap map setup still initializes it. This does not run
`uvm_km_bootstrap()` or `uvm_km_init()` and does not initialize vmem/pool/kmem
arenas or the pageable kernel object.

`bootkm.c` supplies checked public entry points around those MI bodies.
Requests must use this `kernel_map`, one of `UVM_KMF_WIRED` or `UVM_KMF_VAONLY`,
and `UVM_KMF_NOWAIT`. Wired requests may add `UVM_KMF_ZERO`; both types may
add `UVM_KMF_TRYLOCK`. Sizes round up to pages; alignment may be zero or a
power of two from one page through the 16 MiB arena size. Invalid or
unsupported allocation requests return zero. Pageable, executable, sleeping,
wait-for-VA and caller-specified color-match requests are not supported.

Successful allocations are recorded in 32 fixed slots. Freeing requires
the original base, matching rounded size and allocation type. Interior,
partial, wrong-type, foreign and double frees stop before changing mappings.
The map-entry store and 16 MiB virtual arena are shared with the earlier
resident-object buffer API. Each allocation must be freed through its owning
API; callers must not independently alter its map entries or backing pages.

The private MI-km map shim forces non-merging entries. This lets exact frees
and physical-allocation rollback avoid map-entry splitting, which would use
an unavailable sleeping pool-cache path. MI itself selects and allocates raw
pages, applies zeroing/color offsets, maps them using `pmap_kenter_pa()` and
returns them in batches after removing their aliases. On physical exhaustion,
the MI NOWAIT path calls its own free routine to release partial backing and
the full VA reservation before returning zero.

As in MI's wired km implementation, these pages have no UVM object and are
not on replacement queues. Their aliases are wired in pmap; they do not
increment `vm_page.wire_count` or `uvmexp.wired`. The resident-object buffer
API separately maintains those MI wire counts. Tests check both accounting
models and allocations from both APIs sharing the map.

The daemon progress-generation ticket stays zero because no pagedaemon runs.
NOWAIT allocation never waits on it. Wait, reclaim and pageable-removal
services remain unavailable; callers cannot select those paths through the
checked interface. The heap described below uses the wired interface without
requiring dynamic pool caches.

The regression tests cover alignment and rounded sizes, zero-on-reuse,
nonzero color offsets, VA-only requests without physical allocation, busy-map
try-lock failure, more-than-16-page batched removal, map/record/VA limits,
physical exhaustion and rollback, invalid free rejection, restored counts,
and hardware faults for execution and access after free.

## Non-sleeping bootstrap heap

`bootheap.c` implements the three basic kmem calls using real MI
`subr_vmem.c` allocation, splitting and coalescing. Requests use `KM_NOSLEEP`;
`KM_SLEEP` and other flag combinations stop explicitly because there is no
scheduler or reclaim daemon yet. Allocation returns NULL for zero/oversized
requests or resource exhaustion. `kmem_zalloc()` clears the requested bytes.
Every block is eight-byte aligned. Free requires the exact returned base and
original requested size; unknown, interior, size-mismatched and double frees
stop before changing arena state.

The arena starts empty with 200 caller-owned boundary tags and 256 allocation
records. These are fixed metadata limits; tag exhaustion can happen before
record exhaustion. A private refill hook returns NULL for the MI boundary-tag
pool, allowing NOSLEEP failure to propagate without pretending a pool exists.
There are no quantum caches, asynchronous rehash workers or dynamic metadata
pools. The real vmem hash and boundary-tag lists track live blocks.

When vmem needs a span, its ximport callback obtains wired pages through
`uvm_km_alloc()`, rounding the span to 64 KiB pages and reporting that actual
size to vmem. Small allocations share a span. On the last free in a span,
MI coalescing invokes the release callback, which returns all backing pages
and the map entry through `uvm_km_free()`. A completely empty heap retains no
physical pages or virtual reservations. The earlier 32-map-entry and 16 MiB
arena limits also bound heap backing, shared with other bootstrap allocators.

`stand/vmem.c` includes the unchanged MI source under a standalone profile.
Quantum-cache code and event registration are disabled; public structure
layout is preserved. The 11-argument initializer and 9-argument constrained
allocation helper are forced inline into shorter entry points. This avoids
the current GCC stacked-argument limitation without introducing a private
compiler into this repository. The generated assembly has no calls to those
two long-argument functions. Full kernel builds do not use this wrapper or
the bootstrap kmem implementation; `subr_kmem.c` and its pool caches still
need normal startup integration.

The startup probe checks small and multi-page zeroed blocks and restores
the physical free-page count. Tests also cover 128 live blocks sharing one
page, non-overlap, data retention, zero-on-reuse, holes and coalescing, repeated
metadata exhaustion, backing-map exhaustion, partial physical-allocation
failure, exact-size frees, released-span hardware faults and NX protection.
The compiler probe also reproduces the reverse traversal that exposed the
condition-code ordering bug.

## Bootstrap MI pools

`rvm_bootpool_create(size, alignment)` initializes a pool through the real
`pool_init()` body. `pool_get()` and `pool_put()` call checked wrappers around
real MI allocation/free routines, and `rvm_bootpool_destroy()` calls real
`pool_destroy()` when no items remain outstanding. The MI implementation
manages full, partial and empty page lists, linked-list or bitmap free items,
page coloring and allocation statistics. It zeroes the requested item bytes
for `PR_ZERO`.

There are eight reusable pool slots and 512 outstanding-item records shared
by all pools. Item sizes range from 1 through 32 KiB. Alignment defaults to
eight bytes, or may be a power of two from 8 through 256 bytes. Each pool uses
`PR_PHINPAGE`, a custom wired-page backend and `IPL_NONE`. Requests require
`PR_NOWAIT`, optionally with `PR_ZERO`. Pools never use off-page header pools,
magazines, constructors, pserialize, drain hooks or sleeping requests.

Backing pages come directly from `uvm_km_alloc()` and are shared by multiple
items. Destroying an empty pool releases all retained pages through
`uvm_km_free()`. Returning an item alone can retain its page for reuse;
there is no idle-page expiry daemon or advancing uptime clock yet.
The shared allocator reference count and global pool lists are initialized
and updated by MI code. The wrapper rejects unknown pools, NULL/interior
items, cross-pool returns and double returns before changing MI lists.
Destroying a live pool returns false. Item-record or backing-page exhaustion
returns NULL; freeing items permits reuse, and destruction releases backing.

`stand/pool.c` includes the unchanged MI source under a standalone profile.
Its initialization wrapper forces `pool_init()` inline so known custom
allocator and on-page-header choices eliminate unavailable default allocator
tables. The global pool-list pointer initializer is replaced by runtime
initialization because of the FHR static-pointer limitation. Full kernel
`pool_subsystem_init()` and the default allocator tables are still pending.
The following bootstrap cache interface uses explicit custom backing.

All bootstrap units, including both pool sources, use `-O2` with the
corrected GCC widening-multiply patterns. Pool structs retain cache-aligned,
power-of-two storage. Regression tests exercise widening constants and
register products at signed and unsigned 8/16/32-bit boundaries.

The GNU assembler also currently emits a raw destination register byte for
general `MULS`/`MULU` instructions, while VMEmu expects a register-mode byte
followed by the register number. The MI assembly normalizer probes the
assembler's FHR object encoding and repairs only the legacy encoding. It
leaves a corrected assembler's output unchanged and rejects unknown formats.
Fast register-only multiply forms are unaffected. Tests execute signed and
unsigned general multiplication at every width, with immediate and memory
sources, in addition to the pool paths that exposed the problem.

Pool tests cover linked-list and bitmap storage, multi-page full/partial/empty
transitions, zero-on-reuse, live-destroy rejection, item-record/backing-map
exhaustion, allocator reference-count reuse, invalid returns, restored counts,
and NX and access-after-destruction hardware faults.

## Bootstrap MI pool caches

`rvm_bootcache_create(size, alignment, flags, ctor, dtor, arg)` initializes
caller-owned cache storage through real MI `pool_cache_bootstrap()`. There
are four cache slots, 512 shared outstanding-client records and the same
item-size/alignment limits as bootstrap pools. Flags are zero or
`PR_LARGECACHE`. NULL callbacks select no-op constructor/destructor functions.
Constructors receive `PR_NOWAIT`; failure returns the object to its pool and
returns NULL without invoking the destructor. Callbacks may use independent
non-sleeping allocators, but may not reenter the bootstrap cache API.

`pool_cache_get()` and `pool_cache_put()` validate client ownership and call
the unchanged MI magazine paths. Real current/previous CPU magazines and
global full/empty group lists retain constructed objects. A cache hit preserves
object contents and does not rerun the constructor. Requests must be exactly
`PR_NOWAIT`: `PR_ZERO` is rejected because clearing constructed state is not
part of this cache contract. Physical-address results are
`POOL_PADDR_INVALID`, which is also the only accepted return-address value.

The first cache initializes two real MI group pools, each with one wired
metadata page allocated using NOWAIT at IPL_NONE. A magazine allocation
at logical IPL_VM cannot grow those pools; it falls back to MI's immediate
destructor/return path if no group is available. Pages for cache items use
the wired MI kernel allocator. These two metadata mappings share the existing
32-entry, 16 MiB backing arena with other allocators.

`pool_cache_invalidate()` flushes cached objects and calls their destructors;
objects still held by clients remain valid. `rvm_bootcache_destroy()` refuses
live client items, then invokes MI cache invalidation and pool teardown.
Destroying the last cache also releases both metadata pools. Free-page,
pmap and map-entry counts return to their pre-cache values. Returning an
item or invalidating a cache can retain pool backing until destruction.

This is still a single-CPU bootstrap with hardware interrupts and caches
disabled. A checked pointer compare-and-swap and release compiler barrier
support MI magazine lists under that restriction; they are not general SMP
atomics. Cross-CPU calls and passive serialization remain fail-stop. The
`mp_online` flag has padded assembly storage because the current FHR compiler
does not emit scalar data padding. The 11-argument MI cache initializer is
forced inline to avoid the existing outgoing stacked-argument ABI limitation.

Tests cover normal and large magazines, full/partial transitions, callback
counts and retained contents, constructor failure, live invalidation,
cross-cache reuse, descriptor/client limits, metadata and item backing failures,
physical exhaustion, repeated teardown, invalid returns, NX and access after
cache destruction. Startup also checks callbacks and page-count restoration.

## Resident anonymous descriptors

`bootanon.c` checks ownership around the unchanged MI `uvm_analloc()` and
`uvm_anfree()` bodies in `uvm_anon.c`. `stand/anon.c` adapts their private
cache identity to the bounded, custom-backed MI cache facility and uses the
real MI constructor. The general MI default-allocator initialization path
is excluded. Descriptor allocation lazily initializes that cache and returns
NULL on descriptor, cache, virtual or physical backing exhaustion.
`uvm_anon_init()` provides explicit initialization and stops if it fails.

There are 128 registered descriptors. Each initially has one reference,
no page and no associated lock, as in MI UVM. `rvm_bootanon_hold()` and
`rvm_bootanon_drop()` bind a private checked writer lock and adjust references.
The last drop invokes real MI final release. `rvm_bootanon_alloc()` supplies
one zero-filled managed page at offset zero, through real `uvm_pagealloc_strat()`.
Its `uanon`, `an_page`, `PG_ANON` and per-CPU clean/dirty counts are maintained
by MI code. No pager or waiter exists, so the helper clears `PG_BUSY|PG_FAKE`
once zeroing is complete. These resident pages are not wired or queued for
reclaim. Repeated page allocation for the same descriptor is rejected.

The checked `uvm_pagefree()` path also supports a registered anonymous page
while its registered owner lock is held and no VM entries pin the map.
It removes its kernel aliases, then
MI detaches the page from the anon and restores counters/free queues. The
live descriptor remains usable for another allocation. Direct `uvm_anfree()`
is supported for an unbound, empty, zero-reference descriptor. Privately bound
descriptors use `rvm_bootanon_drop()`; descriptors transferred to a registered
amap are released through that amap. This controls both owner-lock lifetimes.
Swap slots, loans, queued/wanted or wired pages and delayed `PG_BUSY` final
release are rejected. Swap, COW faults and anonymous object pagers remain
pending. The bootstrap amap interface below supports sparse resident slots.

The MI anon compilation uses a private page-protection hook for final release.
It removes matching aliases across the existing 256 MiB mutable kernel window
and updates pmap statistics before the physical page is freed. The permanent
supervisor identity mapping remains, as with raw pages. The hook does not
implement generic `pmap_page_protect()` or user PV tracking. Virtual map-entry
reservations, if any, still belong to their callers.

`rvm_bootanon_fini()` refuses live descriptors. With none remaining, it destroys
the anon cache and releases its retained items/backing; magazine metadata is
also released when this was the last bootstrap cache. A failed descriptor
allocation can leave an initialized cache, which `fini()` releases. Startup
verifies reference retention, zeroing, alias removal, page accounting and full
resource restoration, then tears its test cache down so other bootstrap
clients can use all cache slots.

Tests exercise descriptor/cache exhaustion and reuse, private locking,
reference overflow, clean/dirty transitions with nested preemption guards,
multiple aliases, allocation failures at each cache backing stage, physical
exhaustion/recovery, invalid frees and unsupported loans/busy final release.
Hardware probes verify NX and faulting access through a released alias.

## Bootstrap anonymous maps

`rvm_bootamap_create(size)` calls the actual MI `amap_alloc()` body with
`UVM_FLAG_NOWAIT`, no extra padding request, and a checked size from one
through 256 pages. Sixteen maps may be live. MI allocates its used-slot,
back-pointer and anon-pointer arrays through the bootstrap heap; array
allocation failures follow MI's rollback paths. Slot rounding uses the heap's
eight-byte quantum. Cached map descriptors use the existing custom-backed MI
cache, with the real `amap_ctor()`/`amap_dtor()` maintaining the amap list.

The adapter provides 32 private managed-lock slots for those constructed
objects. Each lock belongs to exactly one cached amap; descriptors in the
same map share it. These private helpers replace only the MI amap
compilation's `rw_obj_*` calls. General dynamically shared rwlocks and
anonymous page-loan locks are still unavailable. Cache teardown checks that
all amap list entries and private locks have been released.

`rvm_bootamap_put(map, offset, anon)` transfers one caller-owned descriptor
reference into an empty slot. The descriptor must have one reference and
no existing map owner. It may already have a resident page. MI `amap_add()`
updates its sparse arrays; the registry changes its owner lock to the map's
lock. `rvm_bootamap_lookup()` returns a borrowed descriptor.
`rvm_bootamap_take()` calls MI `amap_unadd()`, which compacts the used slots
and repairs back pointers, then returns ownership and a private lock to the
caller. Put/take require an unshared map with no attached VM entries. Invalid maps, offsets, duplicate
placements or conflicting ownership return failure without transferring an
object. Borrowed descriptors cannot be dropped through the private anon API.

`rvm_bootamap_page()` returns an existing resident page or allocates a zeroed
one under the shared owner lock using real UVM page allocation. Whole-map
`rvm_bootamap_hold()`/`drop()` invoke MI `amap_ref()`/`amap_unref()`, including
its optional per-page reference array. Up to 1,024 references are supported.
These references share one amap; individual anon reference counts remain
unchanged. Final drop runs real MI `amap_wipeout()`, releases each descriptor
and resident page, removes kernel aliases, frees the slot arrays and returns
the map descriptor to its cache. The IRQ-off bootstrap preemption checkpoint
checks that no scheduling request is pending instead of switching LWPs.

`rvm_bootamap_fini()` refuses live maps, then destroys cached descriptors and
their locks/backing. The anonymous-descriptor cache is independent and can
be closed with `rvm_bootanon_fini()` once no descriptors remain. Metadata can
remain cached after an allocation failure until these explicit teardown calls.
Startup verifies sparse lookup, sharing, ownership transfer and wipeout, then
restores the physical free-page count and heap allocation count.

Use the `rvm_bootamap_*` interface for standalone amap ownership, and the
anonymous mapping interface below for kernel VM entries. Partial references/
unmaps, map extension, cross-amap COW, swap and page faults remain pending;
the general MI entry-point stubs have not been replaced. Tests exercise sparse arrays and back pointers, more than
32 entries to reach the MI preemption checkpoint, map/slot limits, ownership
transfer with data retention, shared-reference retention, constructor/heap/VA/
physical exhaustion, rollback of all three array allocation stages, repeated
cache teardown, and NX/access-after-wipeout faults.

## Anonymous kernel VM entries

`rvm_bootanonmap_alloc(size)` reserves a real `kernel_map` entry, creates an
amap, eagerly allocates zeroed anonymous pages and enters RW/NX kernel aliases.
It then transfers its amap reference into the entry's `aref`, with page offset
zero and no backing object. It returns a page-aligned VA or zero on failure.
Sizes are positive and round to 64 KiB pages, up to 128 pages (8 MiB), subject
to the shared descriptor, heap, physical-memory and 16 MiB VA-arena limits.
There are 16 mapping records; the existing 32 map-entry slots also serve
allocator metadata and other bootstrap allocations.

`rvm_bootanonmap_share(base)` creates another complete writable view of the
same pages, through a separate VM entry and a full amap reference. Writes are
visible in all views. Each entry pins the reference transferred into it;
`rvm_bootamap_drop()` may drop only separately held caller references.
Put/take and direct anonymous-page free are rejected while entries pin the
amap. A caller can explicitly hold an extra amap reference and retain its
resident pages after removing the final entry.

`rvm_bootanonmap_free(base, original_size)` requires the exact returned base
and requested byte size. It removes that entry's aliases before invoking MI
`uvm_unmap_remove()` with VAONLY, then calls real `uvm_unmap_detach()` after
unlocking the kernel map. VAONLY avoids the nonpageable map's raw-page freeing
path: these pages belong to anons. A constrained `amap_unref` hook in only the
MI MAP compilation validates the active full-entry detach and drops its pinned
amap reference. The last reference invokes MI wipeout and returns the pages;
remaining shared entries retain their aliases and data. Heap-span release
from wipeout may reenter the kernel map after its lock has been dropped.

Failed creation releases partially populated pages and aliases before
releasing its reservation. Failed sharing removes only the tentative view.
Pre-existing pmap aliases at a newly reserved range are detected and left
intact. The ordinary reservation-release API rejects attached amaps.
Allocation failures can retain initialized cache backing until explicit
`rvm_bootamap_fini()`/`rvm_bootanon_fini()` calls; those calls refuse live maps
or descriptors. Startup tears its test caches down and verifies restored
physical-page and pmap counts.

Tests cover real entry metadata, zeroing, bidirectional shared data, entry
reference pins, exact-size frees, retained caller references, mapping-record
limits and reuse, descriptor exhaustion after partial population, rollback at
all heap-array allocation stages, backing-entry exhaustion, physical exhaustion
and recovery. Full-range sharing also works when MI's optional ppref allocation
fails. Hardware checks verify NX, access after final release, and faulting access
to a removed view while another view remains alive.

These are resident bootstrap mappings. Demand-fault handling, partial unmaps,
protection changes, COW, swapping and user address spaces remain pending.
## Kernel LWP switching

`cpu_switch.c` and `cpuswitch.S` implement NetBSD's three-argument
`cpu_switchto(old, next, returning)` entry for kernel LWPs sharing the kernel
pmap. The caller must be at logical IPL_SCHED with hardware interrupts and
caches disabled. The path validates the LWP/PCB relationship, target stack
bounds, CPU ownership and MMU root, then updates `ci_curlwp`/`ci_onproc` and
changes stacks. It supports the MI scheduler's convention of selecting
`ci_onproc` before the MD call. The resumed caller receives the previous LWP,
matching the MI switch contract. `returning` is unused in this restricted path.

The assembly saves R10-R62 (including FP and the compiler's reserved registers)
in a 424-byte stack frame above its caller return PC, records the saved SP,
SSP and continuation in the PCB, then restores the next frame. C offset checks
in `abi-check.c` enforce the assembly layout. R0-R9 are call-clobbered. User
register/trap state and alternate address spaces are not switched here.

`rvm_cpu_bootstrap_lwp()` attaches a PCB and stack bounds to the bootstrap LWP.
`rvm_cpu_lwp_prepare()` builds an initial frame in a mapped 128 KiB kernel
u-area, with the PCB at its base and the stack above it. The caller owns the
LWP and u-area allocation. `rvm_cpu_lwp_discard()` detaches an inactive context;
the caller can then release the u-area. The entry trampoline validates first
entry and calls a kernel function; a returning function stops explicitly.
`cpu_lwp_fork()` now selects the MI first-entry handoff described below;
normal thread exit is still needed.

The public assembly entry passes only the two LWP pointers to its C body.
This avoids the current compiler allocating an unpadded 17-byte stack home
for two pointers followed by `bool`; the standard public MI signature is
unchanged. No compiler rebuild is needed. Checked preemption nesting now
accepts the active kernel LWP on this CPU, enabling allocator use from a
switched context, while IRQs remain off.

Startup switches between two separate worker stacks and the controller,
checks stack-local values and previous-LWP returns, then releases both stacks.
The focused emulator suite additionally checks every preserved register with
distinct patterns in each context, 64 resumptions, selected-`ci_onproc`
ordering, heap allocation/free on a worker, resource restoration, invalid
priority, NULL targets, invalid saved SPs and wrong MMU roots.
`TEST_SUITE=context` runs these seven checks explicitly.

This supplies the low-level kernel stack switch, not a working MI scheduler.
`vm_machdep.c` now supplies the kernel-only `cpu_lwp_fork()` interface. UVM
must allocate a mapped 128 KiB u-area and install `l_addr` first. Fork builds
the PCB and first stack frame there without copying the parent's kernel
stack. User stacks, user address spaces and user trapframes are rejected.
The trampoline routes these LWPs through a checked boundary to the real
`lwp_startup()` body from `kern/kern_lwp.c`, then calls the kernel function.
The standalone build extracts that body unchanged with `stand/lwp_source.py`
and preserves its license; full kernel builds must link normal `kern_lwp.c`
instead. No process-table or scheduler implementation is substituted.

First entry requires a running previous LWP with its scheduler spin lock held,
IPL_SCHED, the MI `ci_mtx_count == -2` convention, and an MPSAFE system LWP
with no private vmspace. The MI body clears the previous LP_RUNNING bit
(including the release-store zombie case), unlocks, resets the CPU mutex count,
and lowers to IPL_NONE. Bootstrap mutexes do not model the scheduler's spin
count protocol; the probes set its handoff value explicitly. Hardware IRQs
remain disabled throughout. Non-MPSAFE startup and VM activation are outside
this bootstrap path.

`cpu_lwp_free()` currently has no lazy coprocessor state to release.
`cpu_lwp_free2()` clears MD state only for an inactive zombie or unstarted
LWP, preserving `l_addr` for subsequent UVM u-area release. It cannot reclaim
the current stack. Returning kernel functions still panic: MI `lwp_exit()`,
full process-aware reaping, and scheduler exit selection remain pending.

Stage 21 checks live and zombie first-entry handoffs and releases both u-areas.
The seven `TEST_SUITE=lwp` checks additionally exercise twelve creations with
heap use on the child, state/lock/IPL checks, and restored memory/map counts
at 16 and 64 MiB; they reject user-stack requests, non-system children,
reclamation of a running LWP, and first entry without the scheduler handoff.
The final image also boots through this stage at 640 MiB.

`bootsched.c` adds a directed cooperative bridge to NetBSD's real `mi_switch()`
body, selected unchanged from `kern/kern_synch.c` by `stand/synch_source.py`.
It uses the MI `l_switchto` path with an outgoing LSSLEEP or LSZOMB LWP.
`rvm_bootsched_switch(next)` installs the selected target and holds the two
scheduler locks on MI entry. MI drops the run-queue lock, records switch
counts, transfers through `cpu_switchto()`, and releases the previous LWP's
last lock on the resumed stack. First entry still uses real `lwp_startup()`.
A scoped mutex adapter models the MI negative spin count and retains IPL_SCHED
through the release; it does not change other bootstrap mutex users.

`rvm_bootsched_exit(next)` marks a bootstrap worker as a zombie and performs
its final MI switch. The resumed LWP clears LP_RUNNING with the MI release
store; `rvm_bootsched_reap()` then detaches MD state and frees the wired
u-area from another stack. Up to sixteen workers are tracked, preventing
reclamation of sleeping/current workers, duplicate reaping and shutdown with
unreaped workers. `rvm_bootsched_init/fini()` install and remove the temporary
scheduler locks, restoring the bootstrap LWP's original lock and flags.
These APIs are standalone scaffolding, not replacements for MI `lwp_exit()`
or its process/reference/credential cleanup.

This profile does not run soft interrupt LWPs or schedule on timer ticks. Calls into unsupported scheduler branches stop
explicitly. The clock adapter supplies a fixed epoch with no elapsed-time
accounting; kernel VM activation checks the shared kernel root and does not
switch user address spaces. Hardware IRQs stay off in these scheduler paths;
the isolated timer probe below enables one line temporarily. Normal kernels must
link the complete `kern_synch.c` and normal services instead of this profile.

Stage 22 switches two workers repeatedly, hands off from each zombie and
reaps both u-areas. The seven `TEST_SUITE=switch` checks boot at 640 MiB and exercise a
three-worker ring at 16/64 MiB: 128 real MI switches, live heap buffers and
stack canaries across resumes, lock/IPL/LP_RUNNING state, switch counters,
zombie handoffs and complete memory restoration. They also reject use
without initialization, self-targets, running targets and zombie targets.

The bridge now also supports cooperative selection from real MI run queues.
`stand/runq_source.py` selects unchanged UP enqueue/dequeue/selection bodies
from `kern_runq.c`, while the MI switch profile includes its real `nextlwp()`.
The existing real `lwp_setlock()` and `lwp_eprio()` bodies provide lock transfer
and effective priority. Bootstrap initialization installs 224 static queue
heads in `spc_queue`; teardown restores the prior priority/flag state and
requires no queued or unreaped workers.

`rvm_bootsched_ready(l)` admits a prepared system LWP to the appropriate
priority queue, rejecting duplicate admission, invalid effective priority,
unsupported scheduling class and running/zombie targets. The MI bitmap tracks
nonempty priority queues. `rvm_bootsched_yield()` leaves the current LWP on
processor when entering MI, so `mi_switch()` enqueues it and chooses the next
LWP by effective priority. Equal-priority yields rotate in FIFO order. If the
current LWP is selected again, MI releases both locks without changing stacks;
the bootstrap lock adapter now handles that path too.

`rvm_bootsched_finish()` performs a zombie switch using queue selection rather
than a named destination. At least one runnable LWP or an initialized idle
LWP must remain. Stack reclamation uses the same checked reaper.
Directed handoffs remain available, but a named target cannot already be on a
run queue. This is cooperative scheduling with a fixed clock epoch: the
selected 4BSD per-dispatch hooks are empty, timer tick accounting and automatic
preemption are not enabled, and only SCHED_OTHER system LWPs are admitted.

Stage 23 runs two queued workers through yield, exit and reaping. The seven
`TEST_SUITE=runq` checks additionally cover strict priority selection across bitmap
words, FIFO alternation between equal-priority workers, repeated self
selection, live heap buffers and stack canaries, exact switch counts and
empty queues/bitmaps with restored memory at 16/64 MiB. They reject queue
operations before initialization, exit of the bootstrap owner, yielding at
IPL_SCHED and an out-of-range effective priority. The final image reaches
this stage at 640 MiB.

`rvm_bootsched_idle_init(poll, arg)` now creates a separate wired 128 KiB
u-area and idle LWP, sets LW_IDLE and installs `cpu_idlelwp`. The real MI
`nextlwp()` can select this LWP when all run queues are empty. The idle LWP
runs the unchanged `idle_loop()` body from `kern/kern_idle.c`, selected by
`stand/idle_source.py`, and remains LSIDL instead of entering a run queue.
The real MI `sched_curcpu_runnable_p()` determines when to switch back to
runnable work. UP bootstrap lock adapters supply the MI spin-count convention.

`rvm_bootsched_sleep()` leaves the current worker or controller in LSSLEEP
without enqueueing it. `rvm_bootsched_ready()` can wake it by enqueueing it
once LP_RUNNING has been cleared on the destination stack. A worker can now
finish with no runnable destination; MI selects idle and releases the zombie
before the next event makes another LWP runnable. The reaper still releases
the worker stack from a different LWP. Teardown also detaches and releases
the inactive idle u-area and clears `cpu_idlelwp`.

The MD `cpu_idle()` boundary optionally polls a bootstrap event callback. It
rechecks for runnable work and executes a single hardware HALT if none is
available. Without a callback it also halts. The wakeup probes use callbacks
to enqueue sleeping LWPs; they are cooperative event polling, not hardware
interrupt delivery. Without an active timer source this idle-loop path keeps IRQs disabled.
With the repeating source below, idle waits admit timer IRQs in a bounded
assembly window, then deliver deferred wakeups with IRQs disabled again. Running-cpuset publication and background `uvm_idle()`
work are inactive adapters until normal kernel startup is available. These
bootstrap sleep/wakeup APIs do not implement MI sleep queues, wait channels,
timeouts or condition variables.

Stage 24 repeatedly selects idle from empty queues and wakes the controller,
then restores all memory. The seven `TEST_SUITE=idle` checks include 64 event wakeups
through the real idle stack at 16/64 MiB, a worker retaining heap and stack
contents across sleeps, exit with all other LWPs asleep, safe worker/idle
reclamation and full memory restoration. A separate emulator case reaches
the HALT boundary exactly once, leaving the controller asleep. Rejection
cases cover uninitialized sleep, no destination without an idle LWP, and
calling `cpu_idle()` from the controller. The final image reaches stage 24
at 640 MiB.

`trap.c` and `rvm_supervisor_entry` now provide a checked returning path for
one supervisor-only software service on vector 10. Assembly pushes R0-R62
before calling C and restores the entire image before RTE. The hardware
version-2 frame follows the 504-byte register area: header at 504, PC at 512,
and saved MMU root at 520, total 528 bytes. Static ABI checks enforce these
offsets. The dispatcher checks supervisor mode, version/vector, current LWP
and stack bounds, shared kernel root, continuation mapping and service ID;
unknown services and all other faults retain fatal diagnostics.

The probe service uses R0=`RVM_SVC_PROBE`, returns R2 XOR `RVM_SVC_RESULT` in
R0, and preserves all other GPRs. This bring-up service is separate from the
NetBSD user syscall ABI. It does not enable hardware IRQs, restore user state,
recover faults, or switch address spaces. The saved MMU root is validated;
current VMEmu RTE does not load it into MST/MUT. Logical IPL is restored by C,
while hardware SR, its interrupt priority, and the PC are restored by RTE.

Emulator source and runtime checks agree that the big-endian first hardware
word preserves SR flags in bits 0..7 and priority in bits 16..21, alongside
vector/version in bits 32/48. The status mask in `psl.h` now matches VMEmu's
six-bit mask. The emulator's interrupt-entry code can produce level 64 for
IRQ 63, which its ordinary SR mask cannot retain; that highest IRQ line must
be resolved or avoided before enabling hardware interrupt delivery. This
step makes no emulator or compiler changes.

Stage 25 performs a real SYSCALL/RTE round trip. Its assembly fixture fills
all 63 GPRs with distinct patterns, checks the service result, stack pointer,
all condition flags, and hardware priority 31. The seven `TEST_SUITE=trap` checks repeat
this across every logical IPL and a scheduled worker stack at 16/64 MiB,
with heap/stack canaries and restored page counts. Additional fixtures check
hardware priorities 0 and 63. Unknown services and illegal opcodes remain
fatal. The image also verifies this path at 640 MiB. VMEmu currently prints
`Exce_10_SysCall` when executing the software-trap instruction.

`irq.c` now performs a real one-shot GeneralDriver timer interrupt. Timer 1
is configured on IRQ 60 in `vmemu.conf`, corresponding to vector 92 and
hardware priority 61. The probe enables only that INTE bit and CR=12 while
waiting; normal kernel code resumes with CR=8 and INTE=0. Timer control uses
ACTIVE|INTERRUPT, counter zero and reset two (nominally 40 ms at the GUI's
20 ms tick). No other device interrupt or cache is enabled.

The version-0 interrupt frame follows a saved CR and all 63 GPRs, total
536 bytes, with static ABI checks. Entry saves CR, masks hardware interrupts,
and dispatches C with the shared kernel MMU root. The dispatcher validates
mode, vector/version, priority, current LWP/stack and the permitted wait PCs;
it tracks interrupt depth and logical IPL_HIGH only while handling the IRQ.
It masks INTE immediately and acknowledges GeneralDriver timer bit 0, then
restores logical IPL/depth. Assembly restores CR/GPRs and uses RTE. The
return to ordinary C disables hardware interrupts again before checking the
register image.

VMEmu saves the pre-entry HALT bit in the raw frame, so the handler clears it
for return. If the IRQ arrived after enabling CR but before executing HALT,
the saved PC points at the HALT instruction; the handler redirects it to the
continuation. This prevents a lost wakeup. A deterministic alternate fixture
waits on a jump at that PC to exercise this early-arrival branch without
executing HALT. Both paths verify all 63 GPRs, SP and status flags.

This is a one-shot bring-up API, not a periodic NetBSD clock. GeneralDriver
queues ACK commands to the GUI thread and does not currently expose pending
or timer register readback. The handler therefore masks the line while its
ACK is in flight. The completed probe is idempotent and does not rearm and
risk consuming a stale level. Repeating rearm synchronization is now supplied by the paired source below;
At this initial milestone, MI hardclock, arbitrary-PC IRQ handling, nested IRQs and preemption remained
pending. No emulator or GCC changes were made.

Stage 26 verifies timer delivery, HALT wakeup and return at 640 MiB. The seven
`TEST_SUITE=irq` checks also deliver the IRQ on a scheduled worker stack at 16/64 MiB,
preserve heap/stack canaries and restore page/map counts, check the early IRQ
continuation and reject setup at the wrong logical IPL/CR mode or an invalid
interrupt frame. Other IRQ vectors remain fatal. The final stopping path
still masks all interrupts and halts after reporting success.

The repeating source alternates Timer 2 (IRQ 40/vector 72) and Timer 1
(IRQ 60/vector 92). `vmemu.conf` now assigns Timer 2 to IRQ 40 explicitly;
RAM remains 10240 pages. Only the current timer's INTE bit is enabled during
each bounded wait. `rvm_clock_start/wait/stop()` expose the source, while
`rvm_clock_ticks()` and per-channel event counts record actual serviced IRQs.
Each wait arms one GUI timer tick, nominally 20 ms (50 Hz), and returns with
CR=8, INTE=0, zero interrupt depth and the prior register/status image.

Alternation supplies the missing rearm fence without emulator changes. GUI
commands are consumed in FIFO order: once the opposite timer has fired, the
previous acknowledgement for the next timer has already been processed.
Reusing the next line therefore cannot consume its previous asserted level.
The next channel is preserved across stop/start, including odd-length sessions.
The one-shot startup probe finishes on Timer 1, so the repeating source begins
on Timer 2. Stop masks IRQs, disables both timers and queues their ACKs.

`cpu_idle()` now waits on this source when active. After IRQ return it calls
the bootstrap event callback again, which can enqueue LWPs whose tick deadline
has expired. The real MI idle loop then selects runnable work normally.
Wakeup/scheduling does not occur inside the hardware handler. Scheduler
teardown rejects an active timer source; callers stop it before releasing the
idle stack. No compiler or emulator rebuild is required.

This is a repeating interrupt-backed wait source, not a free-running NetBSD
wall clock. Timers are armed at explicit waits, including idle waits; busy
kernel work still has IRQs disabled, so the counter measures serviced waits
and cannot account for elapsed busy time. MI `hardclock` and the timing wheel are now connected as described below;
MI timecounter/uptime publication is connected below; general IRQ critical
sections, a free-running counter and preemption remain pending.
The bootstrap scheduler profiles still use their fixed time epoch.

Stage 27 wakes the sleeping controller at four two-tick deadlines through the
real idle stack. Seven `TEST_SUITE=clock` checks cover the final 640 MiB image and a
sleeping worker with live heap/stack canaries at 16/64 MiB: 26 actual timer
IRQs, eight worker deadlines, a later controller deadline, balanced channel
counts, odd stop/restart sessions and complete memory restoration. An alternate
fixture exercises early-IRQ continuations repeatedly. Invalid logical IPL/CR
contexts and unsolicited interrupts outside an armed wait are rejected.

`bootclock.c` now connects serviced timer IRQs to the unchanged MI `hardclock()`
and `getticks()` bodies selected from `kern/kern_clock.c`. Initialization sets
bootstrap `hz` to 50. Each repeating-source interrupt calls hardclock at
logical IPL_HIGH with a supervisor clockframe and interrupt depth one. The MI
counter advances once, and the real `callout_hardclock()` advances the timing
wheel and requests deferred dispatch when work is pending. `rvm_bootclock_binuptime()`
provides a nominal epoch before timecounter setup, then delegates to the
real MI `binuptime()` service once the timecounter is ready.

`stand/callout_source.py` selects the real MI timing-wheel macros, per-CPU
layout, scheduling/query/cancellation routines, `callout_hardclock()` and
`callout_softclock()` from `kern/kern_timeout.c`, preserving its licenses.
There are four levels of 256 buckets, with static bootstrap CPU storage.
Checked standard-API wrappers register at most 64 live `callout_t` objects (expanded from 32 for mixed MI/MD wait timers).
They support MPSAFE initialization, runtime callback assignment, positive
tick deadlines, reset/schedule, stop, pending/active/expired/invoking queries,
acknowledgement and destruction. Duplicate initialization, non-MPSAFE flags,
zero/negative deadlines, scheduling without a callback and destroying pending
objects stop explicitly. `callout_stop()` retains MI semantics: its return
reports whether the callout had fired, not whether cancellation was needed.

The softint adapter marks a private pending bit. After IRQ return,
`rvm_clock_wait()` polls deferred work with CR=8, logical IPL_NONE and zero
hardware interrupt depth; real `callout_softclock()` invokes callbacks there.
These callbacks may enqueue sleeping LWPs, change callout schedules or destroy
their own callout, but blocking timer waits and context switches are rejected.
This is deferred bootstrap dispatch on the current LWP, not a full MI softint
LWP implementation. Sleeping `callout_halt()`, SMP migration, and waiters on
in-flight callbacks are not implemented.

Hardclock's uninitialized dependencies have explicit bootstrap adapters:
process interval timers, entropy and heartbeat sampling are inactive; the
statistics adapter counts idle/system CPU ticks only; scheduler clock hooks
reset their dividers without requesting preemption. Full process accounting,
profiling and scheduler time-slice behavior remain pending. The timer is still
armed only at explicit waits, so hardclock tick accounting does not yet include
missed ticks during busy execution. The independent monotonic source described
below does measure busy elapsed time for uptime and LWP runtime. Normal kernel builds must use complete MI
clock/timeout services instead of these standalone selection profiles.

The nominal-epoch conversion uses the existing FHR assembly normalizer for
the known general-MUL assembler encoding issue. No toolchain or emulator
source changes are needed.

Stage 28 wakes the sleeping controller via a real three-tick MI callout.
Seven `TEST_SUITE=hardclock` checks also exercise IRQ-driven reset/cancel/rearm, callback
self-destruction and deferred deadlines at 16/64 MiB, with restored free-page
counts. A deterministic test emulates hardclock context for 1000 MI ticks to
cross the 256/257/512 timing-wheel boundaries and move queued deadlines earlier
and later; this case does not claim 1000 hardware IRQs. Rejection cases cover
zero deadlines, destruction while pending, and non-MPSAFE callouts. The final
image boots through stage 28 at 640 MiB.

`stand/timecounter_source.py` now selects the real MI timecounter read,
registration, windup, counter-selection and tick-publication bodies from
`kern/kern_tc.c`. Runtime setup links ten static timehands and installs a
1 MHz counter derived from GenericDriver monotonic nanoseconds. Function pointers and links are assigned
at runtime for FHR compatibility. The MI windup publishes cached timestamps
and `time_uptime`/`time_second`; uptime follows NetBSD's initial one-second
convention. Binary, microsecond and nanosecond uptime reads, their cached
variants, and boot/time reads use the MI generation and scaling algorithms.

Timer hardclock updates wind the timecounter before deferred callouts run.
Idle/switch timestamp reads now use the same uptime service, and the real
MI run-queue operations read `getticks()` instead of a constant zero. LWP
runtime accumulation is enabled below; full process accounting remains pending. The newly needed producer/consumer barriers use the same
checked UP compiler-fence implementation as the existing release barrier:
only one CPU, caches off, and hardware IRQs masked before reaching C.

Registration is checked and bounded to four live counters, with nonzero
frequency (at most 1 GHz), a contiguous low-bit mask and no PPS hook. Counter
replacement uses the real MI quality/frequency selection and preserves uptime.
Detach, NTP/adjtime, PPS and SMP are not enabled. `tc_setclock()` now uses
the selected unchanged MI wall-clock-step body, with validated timestamp and
context boundaries. `inittodr()` reads the RTC at stage 33; earlier stages retain
the unset boot epoch. Uptime advances during busy execution and HALT, independently
of UTC adjustments. Normal kernel clock startup and general interrupt delivery
remain necessary.

Stage 29 checks real-IRQ uptime and timestamp conversions at 640 MiB. Seven
`TEST_SUITE=timecounter` checks also compare cached/live reads on scheduled worker stacks at
16/64 MiB, preserve nested `l_tcgen` state, check scheduler tick accumulation
and restore memory. Deterministic tests switch between independent 8-bit and
32-bit counters, exercise wraparound and second crossings, and rotate MI
timehands repeatedly. Rejection cases cover duplicate registration, zero
frequency and invalid calendar timestamps.

These checks exposed a separate GCC scalar division constraint bug: for the
general (non-fast) DIV form, the dividend was not tied to the destination,
so e.g. a divide-by-1000 could use a stale destination register. The eight
signed/unsigned scalar patterns are corrected in
`../rvm_gcc/_data/work_gcc-14.3.0/gcc/config/rvm/rvm-inst.md`; compiler Makefiles
are unchanged. The rebuilt installed GCC is now validated: generated code
includes the required copy, runtime tests cover signed/unsigned 32/64-bit
immediate and memory division, and the timestamp reference again divides by
a literal 1000. `compiler_check.py` rejects the known missing-copy pattern.
The port image was clean-rebuilt with the updated compiler at -O2.

The switch profile now includes the unchanged MI `updatertime()` body from
`kern/kern_synch.c`, replacing the no-op runtime hook. On each MI switch it
accumulates `now - l_stime` in `l_rtime`, while MI stamps the selected LWP's
new start time. Sleeping LWPs do not accrue time between switches, and the
MI helper explicitly excludes LW_IDLE. Runtime now includes busy elapsed time
from the free-running source and excludes time spent switched out.
The bootstrap's limited diagnostic ABI turns a backwards-clock report into a
fatal diagnostic instead of invoking the long MI warning format.

When moving from the early zero-based clock to MI's one-second uptime base,
timecounter initialization first commits the current LWP's pending runtime
and then rebases its `l_stime`. This preserves already elapsed runtime and
prevents the epoch offset from being charged to the thread. Later counter
replacements retain the normal MI continuous uptime behavior.

Stage 30 verifies a running worker's runtime and excludes sleeping controller
and idle time, then restores all allocated stacks. The seven `TEST_SUITE=runtime` checks
also validate the rebuilt compiler's division paths, account for two workers
and an idle LWP at 16/64 MiB, check CPU tick classifications and memory
restoration, rerun the original immediate-division timestamp case, test the
epoch transition with pending time, and reject a backwards clock. The final
640 MiB image reaches stage 30. No new compiler source changes were needed
in this step.

The MD `cpu_uarea_alloc(bool)` / `cpu_uarea_free(void *)` hooks now back system
LWPs, with `__HAVE_CPU_UAREA_ROUTINES` advertised for UVM integration.
`uarea.c` reserves three pages through the real MI VA-only kernel allocator:
one unmapped guard below the existing 128 KiB (two-page) u-area. A resident
object owns two zeroed, wired physical pages, mapped supervisor read/write
without execute permission. At most sixteen such allocations can be live.
User allocation requests return NULL for MI fallback; user pmap/thread support
is still pending. Unknown free addresses return false so MI can free its own
allocations normally.

MD context preparation (or the MI attachment boundary below) binds the
guarded allocation to its LWP. MD discard or
`cpu_lwp_free2()` unbinds it only after it is inactive; freeing a still-bound
u-area stops before unmapping anything. The bootstrap reaper then uses the CPU
free hook and preserves the ordinary wired-stack fallback. Idle construction
now uses guarded u-areas too, and teardown releases its inactive u-area through
the same ownership path. Failure after allocating only one physical page rolls
back mappings, object/page ownership and the full VA reservation. Independently
installed aliases are detected and preserved rather than overwritten.

The public allocation entry uses a small assembly adapter for the existing
compiler bool-argument home alignment limitation: it canonicalizes the bool
and calls a C body with a four-byte argument home. This preserves the normal
MI signature and requires no compiler change. The PCB remains at u-area offset
zero with the stack above it; the guard catches crossing the lower u-area
boundary, after the PCB region. The initial boot stack remains static.

Stage 31 schedules a guarded worker, handles a timer IRQ on that stack, and
releases it through MD/reaper ownership. Seven `TEST_SUITE=uarea` checks also test all
sixteen slots, zeroing, PTE permissions, repeated worker allocation/reaping and
restored physical/map counts at 16/64 MiB. Hardware cases fault on guard access
(vector 5, MMU enable/present) and stack execution (vector 2). A running worker
cannot free its own u-area. A low-memory case repeatedly forces the second page
to fail and verifies full rollback. The final image reaches stage 31 at 640 MiB.

These hooks currently use the standalone bounded UVM/object services. Full
MI `kthread_create()` still needs process/credential/PID initialization,
normal u-area pool caches, and the full LWP exit/reaping path. Those services
are not replaced by bootstrap thread-creation stubs.

`stand/lwpvm_source.py` now selects the unchanged `uvm_lwp_getuarea()`,
`uvm_lwp_setuarea()`, `uvm_lwp_fork()` and `uvm_lwp_exit()` bodies from
`uvm/uvm_glue.c`. Checked bootstrap boundaries validate their lifecycle.
System u-area allocation currently delegates directly to the guarded CPU
allocator, with a plain wired allocation fallback on release; the normal MI
u-area pool caches are still pending.

Attachment claims a guarded u-area immediately, preventing another LWP from
attaching it or a direct allocator free while it is attached. MI fork then
calls the existing MD fork setup; binding the same LWP is idempotent across
those two steps. An unattached allocation can be freed directly, and an
attached but never-forked LWP can exit through the MI path. A prepared LWP must
first pass MD teardown (`cpu_lwp_free2()`), remain off-CPU, and be LSIDL or
LSZOMB before UVM release. MI exit uses the system free backend; the checked
boundary then clears `l_addr` in all builds so repeated exit is rejected.

Idle creation now uses the MI allocation/attachment/fork interfaces, while
idle teardown and the bootstrap reaper use MI exit after MD teardown. Plain
wired bootstrap u-areas remain supported without confusing them with guarded
VA-only reservations. User u-areas, full `lwp_create` process
initialization, u-area caches and full MI thread-exit bookkeeping remain outside
this selected glue path. Stack-magic instrumentation is not enabled in this
bootstrap build.

Stage 32 creates, runs and reaps a guarded worker through these MI interfaces.
Seven default checks additionally exercise unattached and never-started
cleanup, guarded/plain wired fallback, timer IRQ and supervisor-trap return on
worker stacks, repeated reuse and restored map/page counts at 16/64 MiB.
Rejection cases cover duplicate attachment, exit before MD teardown, repeated
exit and attaching one guarded allocation to two LWPs. The final image reaches
stage 32 at 640 MiB.

The Linux GenericDriver clock ABI uses read-only 64-bit big-endian registers
36 (monotonic nanoseconds since device initialization), 37 (Unix UTC seconds),
and 38 (fractional UTC nanoseconds). Reading seconds latches the paired fraction;
`rvm_rtc_read()` guards the pair against interrupt or LWP interleaving on this UP kernel.
The emulator must include the new clock-register implementation. To select the
rebuilt sibling binary without changing the installed emulator:

```sh
VMEMU="$PWD/../rvm_VMEmu/bin/VMEmu_linux_SDL2" ./run.sh
make -f sys/arch/rvm/Makefile.bootstrap test \
    VMEMU="$PWD/../rvm_VMEmu/bin/VMEmu_linux_SDL2"
```

The raw nanosecond counter backs `delay()`/`DELAY()` and absolute deadlines.
The MI timecounter uses its low 32 bits after conversion to microseconds, giving
one-microsecond resolution and a roughly 71-minute wrap interval. Timehands must
be wound at least once per wrap interval; the current explicit wait path does
this on timer interrupts. The legacy mode has no heartbeat during busy execution;
the asynchronous mode below maintains the clock during ordinary supervisor code.
Very long masked intervals or stopped clock maintenance can still lose whole wraps. The raw
64-bit delay/deadline source is independent of that limitation.

`inittodr(base)` initializes MI UTC from a valid RTC reading, falling back to a
nonnegative base if the RTC sample is invalid. The RTC is read-only: kernel
wall-clock steps update MI's boot-time estimate and cached/live calendar reads,
not the host clock. NTP and writing the RTC are pending. Live uptime advances
between IRQs; cached `get*()` timestamps advance at windup, as in the MI core.
The emulator's monotonic counter advances during guest HALT and host scheduling
pauses, excluding Linux host suspend time.

`rvm_clock_wait_until()` waits through the existing HALT/IRQ return path until
an absolute raw-monotonic deadline; expired deadlines return without a new tick.
`rvm_bootsched_sleep_until()` uses real MI callouts and switches the caller out.
It supports the controller and sixteen registered workers, requires an active
clock and an initialized idle LWP, returns 1 on timeout, 0 on independent early
wakeup, and -1 for unsupported context. Callbacks recheck the absolute deadline
before waking, so early GUI ticks cannot cause early expiration. Early wakeup
stops/destroys the pending callout before its bounded slot is reused. This is a
bootstrap deadline bridge. The MI sleep-queue APIs described below now provide
`kpause()` and non-interruptible condition-variable waits as well.
Hardclock and callout ticks still count serviced waits, not all elapsed periods;
the asynchronous mode below adds missed-tick catchup and arbitrary-PC timer
interrupts while preserving this legacy wait API.

Stages 33–37 verify RTC initialization, interrupt-masked delay, forward/backward
UTC steps preserving uptime, expired/future deadlines, and timed-sleep timeout,
early wake and slot reuse. The focused default suite also runs at 16 and 64 MiB:
two guarded worker LWPs sleep concurrently on independent deadlines, accumulate
busy runtime while IRQs are masked, exclude blocked time, and restore physical
pages/map entries after reaping. It checks early-wake cancellation, nested reader
generations, MI narrow/32-bit counter wrap and invalid wall-clock calls. Test
wrappers now use the existing FHR assembly normalization just like kernel MI
sources; no compiler source changes are required.

The separate asynchronous clock mode now admits timer IRQs at any executable
supervisor PC on a checked current-LWP stack. Its frame saves INTE, CR, all 63
non-SP integer registers and the version-0 hardware frame. Entry masks CR before
C; the restore window raises hardware IPL above both timer lines until RTE has
restored every register and the interrupted SR. Nested/device/user IRQ dispatch
is still outside this two-timer implementation. Bad CR, vector, stack, root and
non-executable return PC stop before modifying scheduler state.

Asynchronous one-shots alternate timer lines and preserve the GUI FIFO ACK
fence. Stop rejects callback/preemption-disabled contexts and drains the currently
armed one-shot without rearming; the arrival
fences the previous channel's ACK, allowing immediate restart or transition back
to the legacy clock. A failed/unsupported start does not overwrite vectors.
Asynchronous and legacy clocks are mutually exclusive.

`splraise()`/`splx()` mask hardware clock delivery at IPL_SCHED and above. Checked
mutexes conservatively mask all IRQs while held, with a per-CPU hold count that
survives MI's scheduler-lock handoff. Explicit IRQ guards nest per LWP, so a
suspended thread's guard does not disable interrupts on a newly selected worker.
The allocator, public MI-callout adapters and RTC/UTC-setting boundaries acquire
these guards automatically. Other bootstrap UVM/pmap interfaces still require
guarded callers; this is not a blanket conversion of the entire kernel to
interruptible execution. Lock contention still stops instead of sleeping.
Uncached UP producer/consumer barriers permit IRQ-enabled timecounter readers;
readers retry through the real MI timehands generation mechanism after preemption.

The clock computes hardclock debt from the raw monotonic source. Each interrupt
services at most 64 elapsed 20 ms periods, carries excess debt forward, and winds
the MI timecounter. Thus timer delays and masked critical sections no longer lose
all intervening hardclock periods. Idle/system tick classifications are still
bootstrap statistics, not full process interval timers or profiling.

Softclock callbacks run after hardware interrupt depth is cleared and logical
IPL is restored to NONE, under an IRQ guard on the interrupted stack. They are
deferred across raised IPL and `kpreempt_disable()` sections. They are not full
MI softint LWPs. Explicit `preempt_point()` checkpoints also enter the checked
preemption path when eligible, deferring inside guarded allocator operations.
The return boundary can then enter the unchanged MI `preempt()`
body, using checked lock adapters and real `mi_switch()`/run queues; involuntary
switch counts are updated by MI. Complete register frames remain on each
preempted LWP's guarded stack until that LWP resumes and returns through RTE.
The idle LWP uses a recognized atomic HALT window and polls deferred callbacks
before selecting a newly runnable LWP. User processes, kernel big-lock
contention, realtime scheduling, SMP and normal scheduler startup remain pending.

Stages 38–41 verify busy timer IRQs, hardware masking and elapsed tick catchup;
non-yielding worker preemption and nested preemption-disabled sections; GPR/SP
preservation in a busy assembly loop; and heap/callout/RTC operations on timed
sleepers under the asynchronous clock. The focused suite additionally exercises
three concurrent sleepers, page-crossing heap buffers, nested timecounter reader
generations, low-IPL callback deferral, lock masking, balanced IRQ guards,
start/stop exclusion, and corrupt frame rejection at 16/64 MiB. It restores page
and kernel-map counts after teardown. The final 640 MiB image reaches stage 41,
then stops the clock and halts as before. No GCC or VMEmu source changes are
required beyond the already installed GenericDriver clock registers.

An optional sustained stress case runs eight guarded LWPs with mixed-size heap
buffers, independent deadlines, cancelled callouts, register canaries and final
memory restoration, then checks a return to the legacy timer:

```sh
python3 sys/arch/rvm/tests/bootstrap.py --objdir obj.rvm-bootstrap \
    --cross rvm-netbsd- --vmemu "$PWD/../rvm_VMEmu/bin/VMEmu_linux_SDL2" \
    --suite asyncstress --stress-seconds 120
```

The current wait layer selects unchanged enqueue, sorted insert/reinsert,
remove, block, timeout and wake bodies from `kern_sleepq.c`, and CV
wait/signal/broadcast/bintime bodies from `kern_condvar.c`. Kernel pause,
`mtsleep()` and `wakeup()` use their MI `kern_synch.c` bodies. The generated
profile supplies checked UP scheduler/lock adapters and disables unavailable
signal, tracing and big-kernel-lock paths. Ordinary kernels must use their normal
initialization and complete services instead of this profile.

`rvm_wait_init()` initializes 128 hashed sleep-queue locks and runtime syncobj
callbacks. Up to 512 initialized CV addresses are tracked for checked lifetime;
init, destroy, signal and empty broadcast remain usable during early allocation
startup. Waiting additionally requires an initialized scheduler/idle LWP and an
active legacy or asynchronous timer. The profile bypasses the MI cold-boot abort
path only behind this checked initialization boundary; it does not mark the
whole bootstrap kernel fully initialized.

A waiter transfers its LWP mutex from the CPU scheduler lock to the MI sleep
queue, releases the caller's adaptive interlock, and enters real `mi_switch()`.
MI wakeup removes it, makes it runnable and queues it by effective priority. The
interlock is reacquired before CV wait and ordinary `mtsleep()` return. Sorted
CV queues retain FIFO order among equal priorities and reinsert after priority
lending. Hashed wait-channel queues distinguish channel addresses even when
they share a hash bucket. `mtsleep(..., PNORELOCK, ...)` deliberately returns
without the interlock. Adaptive mutex ownership is now the current LWP pointer;
spin-lock ownership retains the checked CPU handoff model. Contended adaptive
mutex acquisition and turnstiles remain unsupported.

Each waiting LWP lazily registers its own `l_timeout_ch`. Real MI timeout and
signal-unsleep paths cancel through the synchronization object's callback, and
`callout_halt()` uses the MI UP fast path when the LWP resumes. It may not block
on another running callback; softclock callbacks still run synchronously under
the existing IRQ-return guard. MD `cpu_lwp_free2()` and scheduler teardown destroy each registered timeout
before its u-area or LWP state is reused. There are seventeen
wait-timeout ownership slots, matching the controller plus sixteen workers.
The callout registry now has 64 slots to cover these persistent MI timers plus
simultaneous MD deadline timers and ordinary callbacks.

Supported APIs are `cv_wait`, `cv_timedwait`, `cv_timedwaitbt`, CV signal/broadcast,
non-interruptible `kpause` with a positive tick timeout, `mtsleep` without PCATCH,
and `wakeup`. Timed waits return EWOULDBLOCK on expiry, and signaled waits return
zero. Bintime waits use MI tick conversion and subtract elapsed ticks from the
remaining budget; callers must retry a remaining sub-tick budget as usual.
Signal-interruptible APIs and interruptible pause stop explicitly. Unsupported
lock/context combinations, negative timeouts, duplicate CV initialization, and
destruction with live waiters are rejected before queue mutation.

Bool-argument assembly adapters preserve the standard public sleepq/kpause ABI
and enter generated C with padded unsigned scalar homes, following the existing
u-area/compiler ABI workaround. Synchronization descriptors use loader assembly
storage with runtime callback assignment, avoiding FHR static pointer
initializers. No GCC or VMEmu source changes are needed.

Stages 42–46 exercise CV handoff/signal, timeout and early-signal cancellation,
`kpause` with/without an interlock, wait-channel wakeup and PNORELOCK, and
bintime remaining budgets. Focused tests run at 16/64 MiB, check priority lending,
FIFO/broadcast ordering, colliding wait channels, repeated timeout reuse,
interlock ownership and exact page/map restoration. A capacity test runs all
sixteen worker slots with overlapping MI/MD timeouts and guarded/plain u-areas.
The same waits are tested with the legacy explicit HALT clock. Rejection cases
cover invalid ownership, context, timeout and CV lifetime. The final 640 MiB
image reaches stage 51, then stops the clock and halts as before.

A separate sustained test runs four producers and four consumers through a
bounded message ring using real CV waits, broadcast, timed waits, kernel pause,
heap-backed messages and timer preemption. It checks message totals/checksums,
empty wait queues and complete cleanup:

```sh
python3 sys/arch/rvm/tests/bootstrap.py --objdir obj.rvm-bootstrap \
    --cross rvm-netbsd- --vmemu "$PWD/../rvm_VMEmu/bin/VMEmu_linux_SDL2" \
    --suite waitstress --stress-seconds 120
```

## Generic MI software interrupts and softclock

`rvm_softint_init()` initializes the MI UP slow-softint implementation after
scheduler/idle startup. Each of the four levels has a dedicated guarded LWP
stack; these LWPs sleep off the normal run queues. The unchanged MI
`softint_schedule()`, `softint_execute()`, `softint_thread()` and
`softint_picklwp()` bodies provide FIFO queues, pending-request coalescing,
callback rescheduling and serial/net/bio/clock priority selection. Ordinary
kernel builds must use the complete `kern_softint.c` startup instead of this
standalone compilation profile.

The public establish/schedule/disestablish APIs support local-CPU MPSAFE
handlers. There are 63 client slots. Establish/disestablish require normal
LWP context without locks; scheduling is also valid from hardware IRQs and
callbacks. The UP disestablish barrier drains a pending callback before its
record can be reused. Invalid cookies, unsupported flags and invalid contexts
stop with a diagnostic before queue mutation. Softint handlers must release
all locks and IRQ guards before returning; blocking callbacks, the big kernel
lock, remote CPUs and user ASTs remain unsupported.

`rvm_callout_softint_start()` optionally routes hardclock scheduling to a real
SOFTINT_CLOCK handler. MI `callout_softclock()` then executes on its own LWP
stack, with the existing bounded IRQ guard around callout dispatch. Timer IRQ
return services pending softints even when ordinary timer preemption is disabled.
The earlier synchronous callout profile remains available before softint startup
and after `rvm_callout_softint_stop()`. Stop the clock and drain/disestablish the
softclock handler before final softint teardown. `rvm_softint_fini()` refuses
live handlers and active work, and releases all four u-areas; scheduler teardown
refuses live softint state. Partial stack-allocation failure rolls back every
allocation before publishing per-CPU state.

The default `make -f sys/arch/rvm/Makefile.bootstrap test` now exercises this
milestone. `TEST_SUITE=wait` selects the earlier CV/sleep-queue checks, and
`TEST_SUITE=full` runs all regressions. Focused softint tests cover priority,
FIFO, coalescing, callback rescheduling, handler capacity, disestablishment,
busy-C IRQ delivery, softclock CV wakeups, repeated teardown, allocation-failure
rollback and exact page/map restoration at 16/64 MiB. A sustained eight-worker
message/CV/timeout test uses the dedicated MI softclock:

```sh
python3 sys/arch/rvm/tests/bootstrap.py --objdir obj.rvm-bootstrap \
    --cross rvm-netbsd- --vmemu "$PWD/../rvm_VMEmu/bin/VMEmu_linux_SDL2" \
    --suite softintstress --stress-seconds 120
```

## Managed kernel mappings and hardware access history

Each managed page now has an MD reverse-map head and sticky reference/modified
history, initialized by MI physical-page setup. A fixed table provides one PV
record per page in the preallocated 256 MiB kernel VM window (4096 records,
128 KiB). The per-page list uses constant-time insertion/removal and requires no
allocation while changing mappings. Kernel `pmap_enter()` supports managed wired
and unwired entries, access-type hints and replacement; `pmap_unwire()` updates
wiring independently of residency. Existing `pmap_kenter_pa()` aliases are also
tracked when they refer to managed pages. Freeing a page with a live alias is
rejected before the page can be recycled.

`pmap_is_referenced()`, `pmap_is_modified()`, `pmap_clear_reference()` and
`pmap_clear_modify()` combine sticky state with VMEmu's hardware PTE bits from
all dynamic aliases and the temporary RAM identity mapping. Removing or
replacing an alias harvests its bits first. Clearing references preserves dirty
history while clearing hardware write bits, so a later write is visible as a
new reference. `pmap_page_protect()` downgrades or removes every dynamic alias;
it preserves the bootstrap identity map, whose permissions remain independent.
User address spaces, page-table reclamation, cached execution and SMP are still
outside this implementation.

`--suite allocations` runs the ten earlier allocator fixtures directly.
The default VM observation time is five seconds; `--timeout` changes it for
ordinary cases while explicitly timed clock/stress cases retain their deadlines.
Large-RAM boots retain a five-second minimum because page-metadata setup scales
with RAM size.

`--suite managedpmap` checks alias replacement, list head/middle/tail removal,
the final kernel-window slot, R/M harvesting and clears, wiring, real IRQ use,
page-wide protection faults, invalid inputs and exact cleanup at 16/64 MiB.
The full-image RAM smoke checks now start at 4 MiB; focused allocator exhaustion
checks remain separate. A 1 MiB image may be rejected by the FHR loader before
entering C because relocated sections and the private stack exceed RAM.

## Fault-recovering kernel copies

`kcopy()` now copies kernel memory through an assembly loop with a scoped
`pcb_onfault` recovery cookie. It saves and restores the previous cookie,
supports overlapping regions, handles zero length and rejects address-range
wraparound. Supervisor MMU read, write and invalid-mapping exceptions save all
63 non-SP GPRs and enter a checked dispatcher. Only faults at the four guarded
copy load/store instructions, with the expected cookie, frame version, MMU root
and a valid current-LWP stack, can return through RTE to the EFAULT continuation.
Other faults retain the existing fatal diagnostic path. Physical-bus faults,
user traps, `copyin`/`copyout`, demand paging and signal delivery remain pending.

The copy runs under the current LWP's IRQ guard and does not sleep. This is a
bootstrap kernel-memory service; full kernel integration must revisit latency
for long copies and add normal VM-fault resolution. `rvm_trap_init()` installs
the MMU recovery entries alongside the supervisor software-trap handler.
Stage 51 checks successful copying, actual MMU fault return and restoration of
a pre-existing PCB handler.

`--suite kcopy` tests both copy directions, overlap, zero length, overflow,
read/write permission failures, partial copies across an unmapped page,
R10–R62 and stack preservation, per-LWP cookies under the MI scheduler and
asynchronous timer, and exact memory cleanup at 16/64 MiB. Corrupt recovery
frames and a fault outside the copy instructions remain fatal. The full suite
also reruns the existing supervisor trap and MMU protection regressions with
the new entries installed.

Next, implement normal scheduler/LWP creation and exit initialization, additional
device IRQ dispatch and complete kernel softint startup, alongside the remaining
normal kernel initialization dependencies.
Object pagers, user pmaps and the remaining pmap API also remain necessary. Add kernel
build glue, remaining machine interfaces, complete device priority mapping
and `mainbus` support to reach
`kern/init_main.c:main()`. The policy in `vmparam.h` remains provisional.

User context switching, copyin/copyout, syscalls, signals, user executable loading,
and a root filesystem are later milestones. FHR kernel loading and FHR
user executable support are separate tasks.

See `stand/boot/README.md` for the copied loader's origin and local changes.
