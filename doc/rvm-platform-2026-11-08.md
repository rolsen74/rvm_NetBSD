# RVM shared MMIO, host entropy, physical instruction cache and TLB

The default `./build-diskroot.sh` now builds DISKROOT_ACCEL, enabling kernel
and user instruction caches plus the TLB. Data caching stays disabled.
This requires the updated VMEmu; GCC and VMAsm need no changes or rebuild.

## Register layout

| Device | Register base | Window |
| --- | --- | --- |
| GeneralDriver | `0x0700000000001000` | 4 KiB |
| HARDFILE | `0x0700000000003000` | 4 KiB |
| Video memory | `0x0700000200000000` | unchanged |

Both register windows occupy one 64 KiB supervisor, uncached, non-executable
MMU page starting at `0x0700000000000000`. GeneralDriver no longer rounds its
register address down to 64 KiB; both devices require 4 KiB alignment and have
4 KiB bus windows. Existing explicitly configured, aligned addresses still
work in VMEmu. HARDFILE's default address and NetBSD's configuration move
together. The kernel disk driver uses the existing wired identity mapping
instead of allocating a second virtual register mapping. Its 64 KiB RAM DMA
bounce buffer remains necessary. Video memory allocation and address are
unchanged. Unimplemented GeneralDriver reads return initialized zero bytes.

## Entropy

GeneralDriver register 39 (`+0x138`) returns a fresh 64-bit host random word.
Register 3 (`+0x18`) reports provider availability in bit 0 and success of the
most recent data read in bit 1. Read data first, then accept it only when
`(status & 3) == 3`. Zero is a valid successful random word.

Linux VMEmu uses `getrandom(2)`, retrying interrupted and partial reads. On
failure it returns zero and clears the success bit; it never substitutes a
clock value or weak PRNG. Other host platforms currently report unavailable.
Status accesses are atomic in the host. The guest is still uniprocessor;
device users must serialize the data/status pair if adding SMP support.

The new `rvmrnd0` device feeds successful words to NetBSD's entropy subsystem,
including demand callbacks. Boot now reports `entropy: ready`, and native
NetBSD `getrandom(2)` draws from the seeded kernel generator. This trusts the
host CSPRNG, as a virtual entropy device normally does. Failure paths never
receive entropy credit.

## Cache and translation ABI

Instruction and data cache lines are physically tagged. Every access first
translates its address and checks its current execute/read/write and privilege
permissions, even on a physical-cache hit. Thus two address spaces using the
same virtual address cannot reuse each other's cached mapping or permissions.
Uncached PTEs bypass cache storage entirely.

CR bit 4 enables a per-CPU, 64-entry direct-mapped TLB. It defaults off after
reset, preserving the table-walk behavior for older guests. Entries distinguish
the full root address, supervisor/user mode and virtual 64 KiB page, and retain
leaf permissions and cache policy. The first write following a read performs
the page-table walk needed to update hardware access bits; subsequent reads
and writes can hit. Invalid translations are not cached.

Writing MST or MUT conservatively invalidates the whole TLB, including writes
of the same value. Changing MMU/TLB enable bits also invalidates it. Software
must invalidate translations after changing page tables or clearing hardware
reference/modified bits. Global-page hints do not bypass root/mode tagging.

The existing cache opcodes retain their encodings and level argument `#1`:

| Type mask | Target |
| --- | --- |
| `1` | Data cache |
| `2` | Instruction cache |
| `4` | TLB |

`CInvA` discards selected caches; `CFlushA` writes dirty data back and invalidates.
Their `L` forms target a physical 1 KiB I/D cache line. For type 4, an `L`
address denotes a virtual 64 KiB page and invalidates matching entries across
roots/modes. Masks may be combined. `CPushA/L` writes back dirty data without
invalidating; pushing the clean instruction cache is a no-op, and TLB push is
rejected. All cache-control opcodes require supervisor mode. Invalid levels,
type masks and register indices are rejected. Discarding dirty data clears
its dirty state, preventing a later eviction from writing it back.

NetBSD performs conservative global I-cache/TLB maintenance at pmap transaction
boundaries, on access-bit clearing, and after physical page copying/zeroing or
kernel writes to user memory. This favors correctness over targeted flushing.
User executable PTEs use the cacheable policy when RVM_USER_ICACHE is selected.
Data and MMIO remain uncached. The implementation is UP; SMP would require
cross-CPU shootdowns and further locking.

For user code that modifies its own instructions, `sysarch(RVM_SYNC_ICACHE,
&range)` validates a user address/length pair and synchronizes instruction
fetches. The ABI is in `machine/sysarch.h`; the bootstrap runtime test invokes
the native syscall directly. A full libc/libgcc clear-cache hook remains
future integration work.

## Checks and build choices

The guest test exercises native getrandom, modified executable bytes, explicit
cache synchronization, executable COW across fork, warm NX protection and
unmapping/remapping code at the same virtual address. The NX test also found
a pre-existing FHR bug: a missing core-dump callback caused a kernel NULL call
on a disk-loaded program's crash. FHR now returns EOPNOTSUPP from that callback,
preserving normal signal containment; it still has no core-file format.

```sh
./build-diskroot.sh                         # DISKROOT_ACCEL
./run-diskroot.sh
python3 sys/arch/rvm/tests/hardfile.py --objdir obj.rvm-diskroot --diskroot --expect-accel
python3 sys/arch/rvm/tests/console.py --keypad --pixels
ASAN_OPTIONS=detect_leaks=0 python3 sys/arch/rvm/tests/vmemu_platform.py --sanitize
```

DISKROOT_ACCEL_DIAGNOSTIC adds MI assertions. Explicit DISKROOT and EARLY
configurations leave caches/TLB off; DISKROOT_ICACHE retains kernel-only
caching. All configurations use the new register layout and entropy source.
VMEmu's `--noinstcache` switch can disable I-cache while retaining the TLB.

The host tests execute the actual emulator walker/cache/opcode code over
controlled RAM, checking root/mode separation, physical aliasing, R/M updates,
permissions, cross-page faults, dirty discard/flush, explicit invalidation,
privilege checks, root writes and the legacy uncached path. Entropy tests cover
EINTR, short reads, hard failure, valid zero and actual host-provider success.
Address/UB sanitizers pass; leak detection is disabled under this environment's
ptrace sandbox. Existing clock/RTC/HALT tests also pass.

The boot benchmark compares the same loop with page-table walking, TLB only,
and TLB plus I-cache. Initial pairs measured about 16–17 ms, 5.1–5.3 ms and
4.6–4.8 ms respectively. These are small-loop timings, not whole-system speedups;
the TLB now provides most of the translation savings that the earlier virtually
tagged cache obtained by skipping page-table checks.

Development builds, host tests and guest logs are under
`/tmp/rvm-platform-0_qt_ek_/`; the diagnostic image is in
`/tmp/rvm-platform-kernel/`. Normal artifacts are in `obj.rvm-diskroot/`.

Both accelerated normal and DIAGNOSTIC builds passed boot and persistence
reboots at 64/640 MiB. Console tests passed at both sizes, including keypad
input and pixel erase checks at 64 MiB. The I-cache-disabled fallback passed
at 64 MiB with the TLB retained. Uncached EARLY no-disk boots and raw disk
read/write/read-only checks passed at both sizes. Invalid MMIO alignment and
overlapping device windows were rejected before program startup.

The tested VMEmu source updates are installed in the sibling repository, and
the same executable is installed in both `~/bin/VMEmu` and its sibling `bin/`
directory. Existing user edits, including memory-statistics build flags, were
preserved. Backups of changed source files and the previous executables are
under `/tmp/rvm-platform-0_qt_ek_/backup/` and
`/tmp/rvm-platform-0_qt_ek_/VMEmu.before`. The installed executable passed the
strict accelerated boot test; installed source passed the host sanitizer tests
with memory statistics enabled. Restart VMEmu to load the paired new kernel
and emulator.
