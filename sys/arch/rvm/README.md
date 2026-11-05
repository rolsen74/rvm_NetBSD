# NetBSD/RVM bring-up

This is a working standalone startup for the RVM virtual CPU, using the
FHR bootloader and GeneralDriver interface adapted from SnakeOS. It now links
NetBSD's MI UVM physical-segment registry and raw-page allocator, initializes
managed-page metadata/free queues, and verifies allocation and freeing.
It does not yet enter kernel `main()`, initialize VM maps and object pagers,
or run user processes.
`build.sh -m rvm` is not integrated yet.

## Build and run

Install your updated RVM GCC toolchain before building. This repository
uses the installed cross compiler directly.

GCC needs the symbolic pointer-store fix in the maintained sibling source
`rvm_gcc/_data/work_gcc-14.3.0/gcc/config/rvm/rvm-inst.md`. The DI move
alternative that stores an immediate to memory must use numeric constraint
`n`, so reload cannot turn an address store back into a memory read. If your
installed GCC predates that correction, rebuild it before rebuilding this port:

```sh
cd ../rvm_gcc
rm -f _touch/build_cross_netbsd_gcc
make -f makefile_netbsd "$PWD/_touch/build_cross_netbsd_gcc"
```

From the source root, use GNU make with `rvm-netbsd-gcc`, `rvm-netbsd-ld`,
`VMAsm`, `VMEmu`, and Python 3 in `PATH`:

```sh
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
- `vm.config`: a VMEmu configuration with 64 MiB RAM (1,024 64 KiB pages) and GeneralDriver.
- `netbsd.map`: the GNU linker map.

`./run.sh` at the source root prints the image's absolute path and starts it.
If you see 256 pages or an image size around 142 KiB, you are running an older
copy; the current configuration uses 1,024 pages and prints the page-allocator
verification below. The `run`
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

Expected output includes:

```text
NetBSD/RVM early bootstrap
Supervisor SR: 0x0000000000000024
Kernel stack: 0x... ..0x...
Exception vectors installed; MMU and interrupts disabled.
RAM end: 0x0000000004000000
Reserved prefix end: 0x0000000000080000
Supervisor MMU root: 0x0000000000080000
Free physical range for UVM: 0x00000000000b0000..0x0000000003ff0000
MMU enabled; non-identity mapping verified.
Kernel pmap ready; bootstrap allocator sealed for UVM handoff.
UVM registered PFN range: 0x000000000000000b..0x00000000000003ff
UVM bootstrap allocation: 0x00000000000b0000
UVM physical memory registered; bootstrap allocation verified.
UVM managed pages: 0x00000000000003f1
UVM page allocator ready; allocation/free verified.
```

Addresses and condition-code bits can vary. After printing, the CPU halts
with translation enabled; the emulator window remains open until closed. This is the intended stopping
point for this startup milestone.

`rvm_boot_stage` is 1 on entering C, 2 after console setup, 3 after memory
validation, 4 after page-table construction, 5 after enabling the MMU,
6 after verifying translated memory accesses, 7 after testing a wired
mapping and its removal in the kernel VM window, 8 after UVM preload
registration and allocation verification, and 9 after managed-page allocation
and freeing. A fatal exception sets it to
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
- VMAsm captures the seven register arguments for early `printf`/`panic`.
  GCC's incoming variadic spills and outgoing stacked-argument frames are
  incomplete. Early diagnostics support `%s`, `%d`, `%u`, `%x`, `l`/`ll`
  integer modifiers and `%%`, with up to seven arguments after the format.
  These cover the MI physseg diagnostics; general printf and stacked
  arguments are not supported yet.
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
All current mappings are wired, supervisor-only and uncached. Only cache flags
zero and `PMAP_NOCACHE` are accepted. Invalid mapping edits halt with a
diagnostic. Tables remain allocated when mappings are removed.

With CR equal to 8, VMEmu walks the page tables for each access and has no
separate TLB. `pmap_update` checks that mode and supplies a compiler barrier.
Cache-enabled execution, interrupts, SMP, user pmaps, managed-page/PV tracking,
and the rest of the MI pmap API remain unimplemented.

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
Their standalone wrappers accept unowned raw pages with normal strategy and
`UVM_PGA_ZERO`/`UVM_PGA_USERESERVE` flags. Object/anonymous requests, invalid
frees and double frees halt before entering the MI allocation/free bodies.
Startup allocates a zeroed raw page, maps/writes it, removes the mapping and
frees it, checking the physical address and restored per-CPU free-page count.
With the default 64 MiB, 1,009 pages are managed after bootstrap reservations.

The standalone build compiles unmodified MI sources for page allocation,
physical segments, fixed-storage extents, the clock replacement policy,
percentage parameters and the UP freelist-cache implementation. Selected
entry points are retained with `-fwhole-program` and
`stand/uvm_profile.h`. Event/sysctl registration is disabled, and unavailable
object-pager paths stop explicitly. Full kernel builds do not use this profile.

`uvm_support.c` supplies a bootstrap LWP, CPU counters, logical IPL bookkeeping
and checked mutex state for one CPU with CR exactly 8. This prevents the absent
current LWP from being mistaken for the absent pagedaemon. Recursive locking
and invalid unlocks stop. Condition-variable waits, heap/pool allocation,
scheduling and object services stop if reached. Requests to wake a pagedaemon
are recorded; allocation still returns NULL at reserve limits. This support
file is excluded from normal kernel configuration, where the real kernel
services must replace it.

This initializes **managed raw-page allocation**, not `uvm_init()`. VM maps,
anonymous/file-backed objects, paging daemons, user mappings and PV tracking
remain pending. Interrupts and caches stay disabled. The MD context/process
headers provide types; signal delivery and context switching remain pending.

VMEmu's uncached MMU access loop must advance to the next **page base** when
an access crosses a page boundary. Advancing by the fragment length instead
corrupts cross-page reads/writes, including saved register frames on a stack.
The source correction is in `rvm_VMEmu/src_Emulator/Memory/Mem.c`.

Run the emulator regression checks with:

```sh
make -f sys/arch/rvm/Makefile.bootstrap \
    CROSS_COMPILE="$PWD/../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-" test
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

Next, integrate kernel VM maps and object allocation, managed mapping/PV
tracking and the remaining pmap API. Add kernel
build glue, missing machine interfaces, interrupt priority operations,
a timer and `mainbus` support to reach
`kern/init_main.c:main()`. The policy in `vmparam.h` remains provisional.

Context switching, copyin/copyout, syscalls, signals, user executable loading,
and a root filesystem are later milestones. FHR kernel loading and FHR
user executable support are separate tasks.

See `stand/boot/README.md` for the copied loader's origin and local changes.
