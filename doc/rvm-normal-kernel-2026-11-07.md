# Normal NetBSD/RVM kernel, 2026-11-07

**Later progress:** [real user processes and syscall/signal handling](rvm-user-process-2026-11-07.md)
and [HARDFILE, FFS root and native FHR exec](rvm-diskroot-2026-11-07.md)
supersede the user-mode/storage limitations below; this document retains the
earlier bring-up history.

The external-toolchain build now produces an experimental kernel through real
NetBSD `config(8)`, BSD make and `Makefile.kern.inc`. It enters the unchanged
`sys/kern/init_main.c:main()`, initializes full UVM, kmem, pools, process/LWP
services, kernel threads and devices, prints the NetBSD 11.99.10 banner, and
reaches the root-device prompt. This is a normal MI kernel rather than the
standalone source-selection harness. It does not run a user process or shell.

## Build and run

```sh
./build-kernel.sh
./run-kernel.sh
```

The default configuration is `EARLY`; the image is
`obj.rvm-kernel/netbsd.fhr`. The helper builds the required host tools and the
existing FHR loader. It uses `../rvm_gcc/rvm_cross_netbsd` by default. Override
`RVM_TOOLCHAIN`, `RVM_KERNEL_OBJDIR`, `RVM_KERNEL_TOOLDIR` or `RVM_JOBS` as needed.
`./run.sh` continues to launch the separate stage-51 standalone regression image.

`./build-kernel.sh EARLY_DIAGNOSTIC` enables MI consistency assertions.
`GENERIC` intentionally excludes the temporary user-mode boundaries in
`kernel_pending.c`, and still reports missing interfaces at link time.
User mappings and copies now have real implementations. Unimplemented process
execution transitions still stop with named panics in `EARLY`. The hardware
user-mode probe is a test, not a process startup path or the NetBSD syscall ABI.

## Interfaces added

- Loader-to-`main()` entry with real `lwp0`/CPU setup, physical memory preload,
  supervisor pmap, message buffer and a GeneralDriver console.
- Native mainbus/CPU attachment and polled ASCII console input.
- Normal-kernel IPLs, UP atomics, MI mutex integration, kernel LWP stack setup
  and context switching. Slow MI softints run through normal scheduling.
- GeneralDriver free-running timecounter, read-only RTC registration, and
  alternating timer channels delivering real MI `hardclock()` ticks.
- Managed process pmaps with separate user roots, reference/lifetime operations,
  reverse mappings, wiring/protection/removal and sticky access history.
  Supervisor mappings remain shared; context switches select the user root.
- Fault-aware `copyin/out`, string copies and integer fetch/store operations.
- Validated integer get/setcontext and ptrace register access. Privileged SR
  bits, invalid user PC/SP values and unsupported TLS/FPU contexts are rejected.
- Per-LWP USP save/restore across real scheduler switches; new LWPs inherit
  their saved user SP from the copied register frame.
- Wired user I/O aliases through `vmapbuf/vunmapbuf`, with a 4 MiB physio map
  and cleanup if a partially mapped request fails.
- Real `kcopy()`/MMU-fault recovery integrated into normal kernel startup.

Kernel VA is bounded by the prebuilt 256 MiB window. Kmem is capped at 128 MiB
and the buffer-cache window at 32 MiB, leaving room for other kernel maps.
The previous generic UBC default could exhaust that VA space during startup.

The pool header bitmap classes now scale for compile-time hardware pages above
4 KiB. Otherwise a 64 KiB page with 8-byte kmem objects exceeds the original
4096-item bitmap limit. The integration check also exercises a full 65536-item
off-page bitmap with one-byte `PR_NOTOUCH` objects.

RVM's current ABI aligns long/pointer objects to four bytes. The uncached UP
CPU performs scalar 64-bit accesses in one instruction at this alignment,
without interrupt delivery in the middle of an instruction. A narrow MI atomic
alignment hook preserves assertions against that MD contract; other ports keep
their existing object-size alignment requirement. Normal mutex storage also
has explicit eight-byte alignment. Cache/SMP support must revisit this contract.

## Managed user memory and hardware execution

`kernel_pmap.c` allocates six-level user tables from UVM, never from sealed
bootstrap memory. Each 2 KiB hardware table currently owns a full 64 KiB page;
metadata after the table links ownership for destruction. This deliberately
simple allocator retains intermediate tables until pmap destruction, including
partially allocated trees after `PMAP_CANFAIL` failures. Packing tables and
reclaiming empty branches are later memory optimizations. User leaf entries
have no supervisor bit and cannot be installed outside the user address range.

Reverse mappings link kernel and process aliases of each managed page, so
page-wide protection and reference/modify history cover all tracked aliases.
The high kernel window keeps preallocated PV slots; process PVs use nonblocking
kmem allocation. Transactions serialize through the existing UP IRQ guards.
No TLB exists in VMEmu with the instruction/data caches disabled; enabling
caches, SMP or kernel preemption requires revisiting these assumptions.

Supervisor loads/stores select MST, so `usercopy.c` walks the process pmap and
copies through the supervisor physical direct map. Missing or insufficient
permissions fault through real UVM, including demand-zero and COW allocation.
Access bits are recorded on successful copies. User bounds, partial transfers,
string terminators and zero-length semantics are checked. MI physio wires user
pages before `vmapbuf`; the MD mapper adds/removes kernel aliases and restores
the original buffer pointer. No storage/DMA driver has been added.

The EARLY-only instruction probe copies position-independent machine code into
a UVM page, protects it RX, switches MUT/USP and uses RTE to enter user mode.
It checks user stack writes/reads, two SYSCALL traps and an RTE back to user mode,
then returns to its supervisor caller. Further probes require actual hardware
faults on a kernel VA, read-only text writes, NX data execution and privileged
RTE. Probe vectors, MUT, USP, saved supervisor registers/status and IRQ guards
are restored; a subsequent timer/CV check verifies clock delivery still works.
This runs in a controlled test context with IRQs masked for the short user
instruction sequences. It does not implement asynchronous process trap handling,
syscall dispatch, signals or user scheduling.

Integer context/ptrace conversions are exercised using GPRs captured by the
actual user syscall probe, including rejected restores that must leave saved
state unchanged. Supervisor stack/MMU/fault metadata are not writable through
these interfaces. A sleeping main LWP and softclock LWP carry distinct hardware
USPs, verified across the real MI wakeup/context-switch path. The context
layout still needs wiring into production user trap entry/return and signals.

The next process milestone needs a stable syscall/trap ABI, user fault
and AST handling, exec/setregs and signal delivery, an executable format accepted
by the toolchain/kernel, a root image and an initial program. RVM still has no
registered ELF machine ID; no other architecture's ID has been borrowed.

## Compiler/object tools

The maintained sibling GCC backend and installed NetBSD `cc1`/`lto1` now emit
aligned/unaligned `.quad` pointer initializers and honor explicit `.balign`.
Their previous versions are backed up under
`/tmp/rvm-netbsd-normal-toolchain-backup-20261107/`.
The reproducible patch is `sys/arch/rvm/stand/toolchain/rvm-data-alignment.patch`.
No VMEmu, VMAsm or installed linker changes were needed for the normal kernel
or the subsequent managed-user-memory bring-up. The build helper fingerprints
GCC, cc1, assembler, linker and VMAsm; a changed fingerprint invalidates both
kernel and libkern objects, so an external GCC rebuild cannot silently reuse
old generated code. Direct build.sh invocations do not add this fingerprint.

The static-kernel compiler/link adapters scope private FHR symbols and resolve
strong/weak definitions before the final link. They retain real alias identity,
and include libkern archive members explicitly. ELF objcopy/debug metadata
processing is bypassed because the current FHR tools cannot preserve it.
Shared libraries and loadable modules need native object-format support later.

## Validation

```sh
python3 sys/arch/rvm/tests/kernel_boot.py
python3 sys/arch/rvm/tests/normal_tools.py
```

The normal-kernel tests boot at 64 and 640 MiB and require the real `main()`
console marker, CPU attachment, the integration-check markers, and the
root-device prompt, with no panic/fatal trap. The EARLY integration check uses
real MI heap allocation, a recoverable null copy, hardware ticks, an MI callout
running in softclock context and a CV wakeup. It also checks four-byte-aligned
64-bit atomic load/store/CAS/add and the maximum pool bitmap boundary.

User VM checks create real anonymous maps and require demand-zero reads, copies
across page boundaries, partial-copy counts, strings, overflow/kernel/null
address rejection, strings at the final byte of user VA, read-only failures,
fork COW isolation, sparse high-address aliases, mapping replacement, wiring
counts and reverse-page protection/removal. Eight repeated fork/fault/free
cycles check that physical memory is returned, allowing a small pool-cache margin.
The same boot test requires the hardware execution/protection probes and wired
I/O alias/rollback checks described above.

Both `EARLY` and `EARLY_DIAGNOSTIC` passed these checks at 64 and 640 MiB.
The default helper completed a clean host-tool/kernel build using the installed
compiler; `./run-kernel.sh` was independently checked against its output image.

Object-tool tests execute in VMEmu and check weak defaults, strong overrides,
reversed object order and archive members, plus private-symbol isolation,
pointer initializers, true alias identity and actual 64 KiB object alignment.
All four object-tool cases passed.

Standalone regression coverage also passed all 272 full-suite checks across
overlapping runs. An initial quick full run passed 186 checks before the
64 MiB amap-exhaustion case exceeded its two-second observation window. The
complete memory group then passed 115 checks with three-second ordinary waits
and eight-second amap-exhaustion waits, including that case and the remaining
allocator/MMU/protection tests. Duplicate labels are counted as in the harness.
The new `--suite memory` permits that group to be checked independently.

Temporary validation logs include `/tmp/rvm-normal-kernel-ready-tests.log`,
`/tmp/rvm-kernel-diagnostic-ready-tests.log`, `/tmp/rvm-normal-tools-tests2.log`,
`/tmp/rvm-bootstrap-recheck-full.log` and
`/tmp/rvm-bootstrap-recheck-memory.log`. Copy them elsewhere if they need to
survive temporary-file cleanup.

Boot logs are written under the selected object directory's `boot-tests/`.
The kernel waits for a root device with interrupts masked by the normal polled
console path; no root-capable device is attached yet. The entropy warning is
expected without a seed/RNG source and is not bypassed or assigned fake credit.

## Next work

Implement user pmap roots/translations and faults, user exception/syscall
entry/return, user copy/access primitives, register/context/signal handling and
an executable loader. Then attach an initial RAM filesystem containing init
and a shell, and add a real tty/device path. The normal kernel still lacks fast
softints/in-kernel preemption, cache/SMP support, and general device IRQ routing.

## Continuation validation after the GCC rebuild

The installed compiler passed the helper's ABI/data/alignment checks and a
fresh normal kernel build in `/tmp/rvm-user-kernel`. The default output is
rebuilt in `obj.rvm-kernel/netbsd.fhr`; `./run-kernel.sh` launches it. No sibling
compiler, assembler, linker or VMEmu source/binary was modified in this
continuation. All new port changes are in this repository.

Validation logs for this continuation:

- `/tmp/rvm-user-context-switch-build.log` and
  `/tmp/rvm-user-context-switch-boot.log`: default EARLY build/boot checks.
- `/tmp/rvm-user-context-diag-build.log` and
  `/tmp/rvm-user-context-diag-boot.log`: EARLY_DIAGNOSTIC build/boot checks.
- `/tmp/rvm-user-memory.log`: all 115 standalone memory-suite checks passed.
- `/tmp/rvm-user-managedpmap.log`: all 12 standalone managed-pmap checks passed.
- `/tmp/rvm-user-normal-tools.log`: all four object-tool execution cases passed.

Both normal configurations require every new check and the root-device prompt
at 64 and 640 MiB. The incremental build was also observed to rebuild only the
changed smoke object and the usual version object when the tool fingerprint
was unchanged; adding the fingerprint rebuilt kernel and libkern objects.
