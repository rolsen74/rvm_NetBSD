# Kernel instruction-cache experiment — 2026-11-08

`DISKROOT_ICACHE` enables VMEmu's instruction cache for the kernel's fixed,
read-only FHR code sections. The MMU stays enabled, data caching stays off,
and user execution stays uncached. No VMEmu changes or rebuild are required.

```sh
./build-diskroot.sh DISKROOT_ICACHE
./run-diskroot.sh
```

The boot prints `RVM kernel instruction cache enabled; user/data caches disabled`.
To compare with VMEmu forcing the cache off:

```sh
./run-diskroot.sh --noinstcache
```

That boot reports `RVM kernel instruction cache disabled by VMEmu` and skips
the A/B comparison. `./build-diskroot.sh DISKROOT` rebuilds the uncached kernel;
the script's default remains DISKROOT. The previous uncached kernel image is
also saved as `obj.rvm-diskroot/netbsd.uncached.fhr` for this experiment.

## Scope and mechanism

CR bit 1 enables instruction caching. The immutable kernel code PTEs also need
a cache policy: they now use the write-through encoding when the option is on.
CR bit 0 stays clear, so data access, page-table updates, MMIO and the disk
driver's bounce-buffer transfers retain their uncached paths.

Boot, kernel interrupts and user trap handling enable caching for supervisor
code. User resume explicitly clears caching before RTE, and trap return
restores the incoming CR. The EARLY user probe saves/restores CR and also
disables caching before entering user mode. Diagnostic assertions check the
incoming user CR on faults/syscalls and timer interrupts.

VMEmu's cache hits remember virtual-address translations and permissions.
Its CInvA/CFlushA implementations currently support the data cache only.
Consequently this experiment never enables instruction caching in a user
address space, and it only caches immutable text in the shared kernel identity
map. This avoids needing instruction-cache invalidation across user exec,
COW, permission changes and switches between different user roots.
Dynamic executable kernel mappings, patched kernel text, loadable code and
full user instruction caching still need proper invalidation support.

`cache.h` selects the experimental kernel CR without changing the architecture's
MMU bit definition. Pmap and copy/MMIO recovery checks allow the instruction
bit while still requiring data caching and interrupts to be off in guarded
contexts. The assembly build strips config's unreferenced GNU option-symbol
metadata before handing preprocessed locore to VMAsm.

## Measurement and validation

The EARLY cache configuration runs the same 20,000-iteration arithmetic loop
three times with instruction caching off and on. Both sides use identical
input, instructions and data access, with interrupts masked during each pair.
The results must match; the code also reads CR back to check that instruction
caching is on and data caching is off. Host monotonic MMIO provides elapsed
time. Generated assembly was checked to ensure both function calls remain.

The final 64 MiB run measured 13,573 microseconds uncached and 3,417
microseconds cached in one pair, approximately four times faster.
This measures a small instruction-heavy loop,
not whole-system performance; host load and cache footprint affect timings.
Timing thresholds are deliberately excluded from correctness tests.

```sh
python3 sys/arch/rvm/tests/hardfile.py --objdir obj.rvm-diskroot --diskroot --expect-icache
python3 sys/arch/rvm/tests/console.py --pixels
python3 sys/arch/rvm/tests/console.py --ram-mib 640
```

`DISKROOT_ICACHE_DIAGNOSTIC` adds MI assertions to the same experiment.
The disk-root tests include the existing user isolation/NX/protection, COW,
signal, context-switch, fork/exec, allocator, FFS and persistence checks.
The console tests exercise file I/O, environment/PATH lookup, native programs,
Ctrl-C and monitor survival, with pixel-based Backspace checking.

The final cached normal and DIAGNOSTIC builds passed disk-root checks and
persistence reboots at 64/640 MiB. Per-size median loop speedups were 3.90–4.03
times. Cached console tests passed at both sizes (including pixels at 64 MiB);
the diagnostic console also passed. VMEmu's cache-disabled fallback passed
boot/reboot and console tests at 64 MiB. Uncached EARLY no-disk and raw disk
read/write/read-only regressions passed at both sizes.

Logs: `/tmp/rvm-icache-final-boot.log`,
`/tmp/rvm-icache-final-console{64,640}.log`,
`/tmp/rvm-icache-diag-{boot,console}.log`,
`/tmp/rvm-icache-disabled-{boot,console}.log`, and
`/tmp/rvm-icache-regression-{boot,disk}.log`. Cached boot transcripts and timing
samples remain in `obj.rvm-diskroot/hardfile-tests/`; fallback transcripts were
copied to `/tmp/rvm-icache-disabled-logs/` before the final cached runs.
