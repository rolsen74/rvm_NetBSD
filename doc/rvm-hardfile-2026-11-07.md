# VMEmu HARDFILE device, 2026-11-07

NetBSD now has a driver for this device: `sys/arch/rvm/dev/ld_rvm.c`, using
the MI `ld`/`dk` block and character interfaces. It safely probes the default
address, disables device IRQs, serializes commands through a BIO softint and
uses a single physically contiguous 64 KiB DMA bounce page. Polling matches
the emulator's synchronous command semantics. Missing hardware remains
optional for EARLY/GENERIC. Read-only writes return EROFS; dump is unsupported.

`python3 sys/arch/rvm/tests/hardfile.py` exercises actual 64 KiB disk transfers,
flush, bounds, read-only policy and exact host-image restoration at 64/640 MiB.
It creates its own marked temporary images. The EARLY disk fixture writes only
images explicitly carrying the `RVMTEST1` opt-in marker in their final sector;
ordinary filesystem images receive a read-only probe before root selection.

The [disk-root notes](rvm-diskroot-2026-11-07.md) describe FFS root mounting and
FHR user executable support above this driver.

The sibling VMEmu now implements a separate file-backed MMIO block device:

```ini
[HARDFILE]
 Address = 0x0700000300000000
 File = "/path/to/netbsd.img"
 ReadOnly = 0
 IntNr = 18
 End
```

File must already exist, be regular/nonempty, and have a size divisible by 512.
Opening never creates or truncates it. ReadOnly defaults to 1; use 0 explicitly
to permit writes. Address defaults to the value above; IntNr is optional for
polling. Relative paths use the emulator's working directory (`obj.rvm-kernel`
when launched through run-kernel.sh). Quoted paths support spaces.

The separate 64 KiB MMIO region has no 64 KiB RAM buffer behind it and does not
reduce configured guest RAM. There are small host device/metadata allocations;
guest page tables for mapping the device are the OS's responsibility. Existing
GeneralDriver offsets are preserved.

The public ABI is [VM_Hardfile.h](../../rvm_VMEmu/inc_public/VM_Hardfile.h).
[HARDFILE.md](../../rvm_VMEmu/HARDFILE.md) documents its registers and semantics,
and [hardfile.config](../../rvm_VMEmu/examples/hardfile.config) is a complete
configuration example.

The device supports sector read/write, flush, bounded physical-RAM DMA,
completion/error/byte-count registers and optional acknowledged completion IRQs.
Sectors are 512 bytes; a command transfers at most 1 MiB. DMA validates the entire
range before modifying the image or RAM, including adjacent RAM regions, holes,
overflow and MMIO rejection. It runs synchronously on the CPU thread. Linux
flush uses fflush+fsync. NetBSD's current uncached UP execution suits this initial
interface; a later cache-enabled driver needs the normal DMA cache/barrier work.

Config parsing now preserves quoted values with spaces and propagates plugin
validation failures instead of booting after invalid device setup. Default PC
selection chooses ordinary RAM when a device section precedes MEMORY. Device
range overlap is checked after every section has its final address/size.

The tested binary was installed atomically at `/home/vm/bin/VMEmu`. Its previous
binary is saved at `/tmp/VMEmu-before-hardfile-l6izkeae`. Running instances keep
using their old executable; restart to get HARDFILE support.

## Validation

```
python3 ../rvm_VMEmu/tests/hardfile/test.py
python3 sys/arch/rvm/tests/kernel_boot.py --timeout 12
```

Seventeen checks cover real guest read/write/flush and IRQ/polling execution,
host image contents, read-only policy, unaligned and cross-RAM-region DMA,
range/overflow/MMIO rejection and rollback, malformed configs and multiple disks.
The existing full NetBSD user-process boot checks passed at 64 and 640 MiB with
the candidate emulator. Logs: `/tmp/rvm-hardfile-final-tests.log`,
`/tmp/rvm-hardfile-netbsd-boot.log`, `/tmp/rvm-hardfile-installed-tests.log`, and
`/tmp/rvm-hardfile-installed-netbsd.log`.

## Remaining NetBSD work

This adds hardware, not a mounted root disk. The NetBSD kernel is unchanged by
this hardware addition and still lacks a block driver for it. That driver needs
safe MMIO mapping/probing, a strategy routine, bounded RAM/bounce-buffer DMA,
completion handling and disk/root attachment. Interrupt mode also needs to
preserve enabled disk sources when the clock changes its timer interrupt mask.
An initial polled implementation can avoid that interrupt integration step.
Executable-file loading, a root image with /sbin/init and user runtime support
remain required for a filesystem/shell boot. Existing run-kernel.sh still boots
without a hardfile configured; no existing image was attached or modified.
