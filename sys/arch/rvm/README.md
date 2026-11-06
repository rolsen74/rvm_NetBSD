# NetBSD/RVM bring-up

This is a working standalone startup for the RVM virtual CPU, using the
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
It does not yet enter kernel `main()`, initialize full UVM km and object pagers,
or run user processes.
`build.sh -m rvm` is not integrated yet.

## Build and run

Install your updated RVM GCC toolchain before building. This repository
uses the installed cross compiler directly. The build runs a small codegen
probe and rejects compilers that can destroy flags between a comparison and
its branch.

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
does not prevent that transformation. Rebuild GCC, then clean-build the port:

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
verification, 14 after bootstrap heap verification, and 15 after MI pool
allocation/destruction verification. A fatal exception sets it to
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
Their standalone wrappers accept raw pages or registered resident objects,
with normal strategy and `UVM_PGA_ZERO`/`UVM_PGA_USERESERVE` flags.
Unknown objects, anonymous requests, duplicate/out-of-bounds object offsets,
missing owner locks, invalid frees and double frees halt before entering the
MI allocation/free bodies.
Raw requests also accept `UVM_FLAG_COLORMATCH` with a page-aligned offset
within the bootstrap arena, as required by MI wired kernel allocation.
Startup allocates a zeroed raw page, maps/writes it, removes the mapping and
frees it, checking the physical address and restored per-CPU free-page count.
With the current image and 64 MiB, 1,005 pages are managed after
bootstrap reservations. The regression suite boots with 2, 16, 64 and 640 MiB.
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
and invalid unlocks stop. Condition-variable waits, unsupported pool/cache paths,
scheduling and unsupported pager services stop if reached. Requests to wake a pagedaemon
are recorded; allocation still returns NULL at reserve limits. This support
file is excluded from normal kernel configuration, where the real kernel
services must replace it.

This initializes managed pages, resident objects and wired kernel allocation,
not `uvm_init()`. Full cached arena/heap startup, anonymous/file-backed objects,
paging daemons, user mappings and PV tracking
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
allocations, the bootstrap heap and MI pools, reaching stage 15 and printing:

```text
Early wired allocator ready; zeroed allocation/free verified.
UVM kernel VA map ready; reservations and wired buffers verified.
UVM resident objects ready; page ownership and wired allocation verified.
UVM kernel allocator ready; wired and VA-only allocation/free verified.
Bootstrap kmem heap ready; subpage allocation and span release verified.
Bootstrap MI pools ready; item allocation and page release verified.
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
`pool_subsystem_init()` and pool-cache machinery are still pending.

Two pool source files currently use `-O1`: RVM GCC's combine pass aborts
internally on their `-O2` form. Other units remain at `-O2`. Pool structs use
a cache-aligned, power-of-two storage stride, avoiding general immediate
multiplication when indexing that bootstrap storage.

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

Next, integrate full pool-cache startup and anonymous objects, managed mapping/PV
tracking and the remaining pmap API. Add kernel
build glue, missing machine interfaces, interrupt priority operations,
a timer and `mainbus` support to reach
`kern/init_main.c:main()`. The policy in `vmparam.h` remains provisional.

Context switching, copyin/copyout, syscalls, signals, user executable loading,
and a root filesystem are later milestones. FHR kernel loading and FHR
user executable support are separate tasks.

See `stand/boot/README.md` for the copied loader's origin and local changes.
