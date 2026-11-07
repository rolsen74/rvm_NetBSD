# Normal NetBSD/RVM kernel, 2026-11-07

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
`EARLY` user memory operations fail with `EFAULT`; unimplemented execution
transitions stop with named panics. These hooks do not implement user support.

## Interfaces added

- Loader-to-`main()` entry with real `lwp0`/CPU setup, physical memory preload,
  supervisor pmap, message buffer and a GeneralDriver console.
- Native mainbus/CPU attachment and polled ASCII console input.
- Normal-kernel IPLs, UP atomics, MI mutex integration, kernel LWP stack setup
  and context switching. Slow MI softints run through normal scheduling.
- GeneralDriver free-running timecounter, read-only RTC registration, and
  alternating timer channels delivering real MI `hardclock()` ticks.
- Empty process-pmap lifetime/reference operations. User translations are
  still unsupported; supervisor mappings remain shared between LWPs.
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

## Compiler/object tools

The maintained sibling GCC backend and installed NetBSD `cc1`/`lto1` now emit
aligned/unaligned `.quad` pointer initializers and honor explicit `.balign`.
Their previous versions are backed up under
`/tmp/rvm-netbsd-normal-toolchain-backup-20261107/`.
The reproducible patch is `sys/arch/rvm/stand/toolchain/rvm-data-alignment.patch`.
No VMEmu, VMAsm or installed linker changes were needed in this session.

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
