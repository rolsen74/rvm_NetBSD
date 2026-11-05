# RVM FHR startup loader

Adapted from Rene W. Olsen's `rvm_SnakeOS/bootloader` and `includes_i`
checkout, with permission from the author in the porting session.
Original copyright notices are retained. This is bootstrap code for trusted
locally built images, not NetBSD user executable loading.

Local changes:

- The startup banner identifies the NetBSD/RVM loader.
- CR and INTE are cleared on entry, and caches stay disabled during loading.
  Clearing CR does not flush dirty cache lines in VMEmu, so the loader must
  not enable caches before handing control to the kernel startup.
- A shared `machine/bootinfo.h` describes the version 1 boot record. C
  preprocessing supplies offsets to the assembler so C and loader layouts
  agree. `r2` contains its address at kernel entry.
- The record reports one contiguous RAM bank starting at zero, loaded section
  addresses/sizes/permissions, the page-rounded loaded end and a reserved final
  RAM page for the loader stack. It is eight-byte aligned inside the raw loader.
- Section count, allocation size and stack bounds are checked before loading
  sections. The BSS clearing call now passes its length in the documented `r1`.
- CPU.i retains only CPU constants. SnakeOS-specific CPU structures and
  virtual-address policy are omitted; include paths are local to this copy.

VMAsm assembles Source.vma with `--bin --optimize`. VMEmu loads that raw
binary immediately followed by `netbsd.fhr` in one RAM region starting at
zero. Run one CPU for this initial port. The loader retains SnakeOS's RAM
probe, FHR parsing/relocation logic and initial fault handlers. NetBSD entry
replaces the stack and VBR before calling C; it never returns to this loader.

The version 1 handoff supports one contiguous physical RAM bank at zero;
it does not describe discontiguous memory or firmware reservations. Kernel
startup copies and validates it before enabling the MMU. Bootstrap reservations
are maintained by the early allocator; UVM registration is still pending.
