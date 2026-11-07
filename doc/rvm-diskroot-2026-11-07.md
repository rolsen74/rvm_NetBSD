# NetBSD/RVM disk-root and FHR exec integration, 2026-11-07

The normal kernel can mount a real big-endian UFS2 filesystem on VMEmu's
HARDFILE disk (`ld0a`) and load `/sbin/init` through NetBSD execve. This is an
early port with integration programs, not a complete NetBSD userland or shell.

## Reproduce

```sh
./build-diskroot.sh
./run-diskroot.sh
python3 sys/arch/rvm/tests/hardfile.py --objdir obj.rvm-diskroot --diskroot
```

The build helper builds DISKROOT, host pax/makefs and libc-free native
programs, then regenerates `obj.rvm-diskroot/root-fixture.img`. That filename
is a disposable build artifact: rebuilding replaces it. It never creates or
overwrites a user-supplied disk. `run-diskroot.sh` uses `vm.diskroot.config`,
including HARDFILE at `0x0700000300000000`, ReadOnly=0, and no disk IRQ.
The normal `build-kernel.sh`/`run-kernel.sh` EARLY workflow remains separate.

`RVM_KERNEL_OBJDIR`, `RVM_KERNEL_TOOLDIR`, `RVM_TOOLCHAIN` and `RVM_JOBS` also
apply to the disk-root build. `./build-diskroot.sh DISKROOT_DIAGNOSTIC` enables
MI consistency assertions; `DISKROOT_GENERIC` excludes the resident EARLY
kernel tests while still executing the disk's user programs.
Shared hosttools wrappers are regenerated for
the selected object directory; do not build different object trees through
the same hosttools directory concurrently.

## Implementation

* `ld_rvm.c` attaches the optional HARDFILE as MI ld0. Its MMIO mapping reserves
  one kernel VA page, and one real 64 KiB RAM page supplies contiguous DMA.
  It performs synchronous hardware commands on a deferred BIO completion so
  MI ld has finished accounting for the queued request before lddone.
* `mmio.c`/`mmio_probe.S` recover only the expected physical-read fault at the
  guarded probe instruction. Unrelated physical faults remain fatal.
* `exec_fhr.c` registers EXEC_FHR with the native NetBSD emulation. It accepts
  uncompressed, unencrypted 64-bit big-endian RVM FHR executables. It checks
  section bounds, alignment, flags, CRC16, relocation counts/targets/offsets,
  end tags and file consumption, then applies ABS64 relocations to a private
  file snapshot before replacing the process address space. Initial limits:
  32 sections, 16 MiB file and 64 MiB rounded section memory. Writable code
  is rejected. Sections start at 0x400000 and occupy separate 64 KiB pages.
* FHR has no encoded entry field: entry is the first byte of the first code
  section. crt0 must come first. The kernel link adapter now rejects an entry
  symbol displaced from the beginning of text, preventing silent bad boots.
* Exec maps anonymous storage, copies relocated contents, applies final
  protections, builds the normal MI argument stack and invokes setregs.
  Snapshot storage is freed after copyargs, or through exec's error cleanup.
* Normal supervisor data faults in the dynamic kernel window can now enter
  UVM at IPL_NONE without spin locks or interrupt context. This is needed by
  the pageable exec-argument submap. The temporary copy guard/cookie is
  suspended while UVM may sleep, then restored. Invalid guarded copies retain
  EFAULT recovery; unrelated invalid kernel accesses remain fatal.
* String copies inspect chunks of at most 256 bytes, bounded by both kernel
  and user page boundaries, and write only through the first NUL. This avoids
  a pmap walk and guarded copy for every argument byte. EARLY checks NUL at
  the end of user VA and at the end of a kernel alias followed by an unmapped
  page, in both directions. The 70 KB exec argument checks multiple chunks
  and pages through real syscalls and pageable kernel argument storage.
* The console cdev (major 500) writes to GeneralDriver. MI `/dev/console`
  (major 240, minor 0) forwards to it. makefs creates the special vnode from
  an mtree specification; no host mknod privilege is needed.

The root image is an unlabelled whole-filesystem image. MI disklabel fallback
provides partition a and the raw partition; a disklabel/partitioning utility
is not required for this fixture. FFS uses 16 KiB blocks and 2 KiB fragments.

## Current integration coverage

The disk init opens `/dev/console`, duplicates stdout/stderr, reads `/etc/motd`,
and checks ENOEXEC from four deliberately invalid executables (wrong CPU,
bad CRC, oversized section, out-of-range relocation). It then forks and calls
execve on a `#! /sbin/exec-check` script, passing a 70,000-byte argument across
VM pages. MI EXEC_SCRIPT loads the FHR interpreter and forwards its script
pathname and the original argument. The C child checks the argument stack, initialized data pointers and zero
storage, calls native syscalls, and exits with a status collected by PID 1.

The C child forks/executes a third program with a genuine two-page FHR BSS
section, checking zero fill and writes on both demand-faulted pages. The
builder constructs that BSS section explicitly because the current GAS FHR
backend truncates large `.section .bss` reservations; the compiler adapter
continues to emit C zero storage as explicit data.

The C fixture additionally exercises read-write root remount, file creation,
write, fsync, seek, readback, close and unlink, then remounts root read-only.
It creates a second durable file and verifies its contents on later boots.
PID 1 sleeps after success, without exiting or running a shell. Tests boot
each temporary image twice to verify persisted data after a complete emulator
restart, and retain logs under each object tree's hardfile-tests.

The separate EARLY raw-disk checks test read/write and read-only configurations
at 64/640 MiB, including exact host-image hashes before/after restoration.
The no-disk boot suite still checks the resident process, signals, memory
protection, timers and absent-device recovery. DISKROOT_DIAGNOSTIC enables
MI assertions for the disk-root path, including lazy worker creation.

## Remaining port work

Provide native csu/libc and user syscall stubs to build ordinary NetBSD
programs, then production init, fsck/mount tools and a shell. The console has
output only, with no tty/input buffering or terminal ioctls. The disk driver
supports one default-address disk, polling, no detach and no crash dump.
FHR dynamic linking, compressed/encrypted files, TLS, FPU contexts, core
files, networking, cache-enabled execution and SMP remain unsupported.

## Validation logs

* `obj.rvm-diskroot/hardfile-tests/`: normal disk-root boots and reboots at
  64/640 MiB; `root-*-reboot.log` includes the persisted-file check.
* `/tmp/rvm-strings-diag-tests.log` and
  `/tmp/rvm-diskroot-diag/hardfile-tests/`: DIAGNOSTIC coverage at 64/640 MiB.
* `/tmp/rvm-ready-generic-tests.log` and
  `/tmp/rvm-diskroot-generic/hardfile-tests/`: GENERIC without resident fixtures.
* `/tmp/rvm-ready-default-disk-tests.log`: raw disk RW/RO restoration checks.
* `/tmp/rvm-ready-default-boot-tests.log`: no-disk normal boot regressions.
* `/tmp/rvm-ld-normal-tools.log`: four binding/alignment/archive regressions.
