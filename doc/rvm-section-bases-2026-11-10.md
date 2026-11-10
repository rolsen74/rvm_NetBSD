# RVM section-base addressing

R50 addresses output `.rodata`, R51 output `.data`, and R52 output `.bss`.
Code references retain the existing PC-relative relaxation. GCC already
reserves these registers; no compiler register-allocation change was needed.

The assembler records actual absolute memory operands, including destination
operands and both sides of memory copies. An image exporting the startup marker
`__rvm_section_bases_v1` enables linker conversion to section-relative signed
register displacements. Fully covered code sections shrink to the smallest
supported displacement width. Code that cannot safely shrink uses an equal-size
register-plus-signed56 operand. Stored pointers and the startup base loads keep
loader relocations. Old startup objects and `--no-relax` retain absolute data
operands. The incremental toolchain patch is
`sys/arch/rvm/stand/toolchain/rvm-section-bases.patch`.

For example, the runtime regression turns the BSS clear into `clr.q (0,r52)`,
loads initialized data with R51, reads constants with R50, and converts both
operands of a memory-to-memory move. Its boundary checks cover -129/-128,
127/128 and 32767/32768. It also checks literal pointers, sections with inline
data, old startup objects, `--no-relax`, and the real crtend heap anchor.

crt0 initializes all three relocated bases before entering C. Kernel entry
initializes its own bases, trap/IRQ entries reinstall kernel bases after saving
the interrupted registers, and user returns restore the saved user bases.
Fresh LWP switch frames initially contain zeroed registers, so their trampoline
also initializes the kernel bases before calling C.

The standard linker scripts emit code, writable data, read-only data and BSS
in that order. Each data section retains eight trailing bytes to materialize
even an empty loader relocation target without disrupting input alignment.
Native userland still uses its existing zero-storage adapter: zero-initialized
objects collected in `.data` use R51, while actual output BSS uses R52.

crtend's `_end`/`__rvm_end` now live in `.rvm_end`, collected last in output BSS.
Previously they marked the end of writable initialized data. With read-only
data following it, the first heap allocation could shrink away the read-only
mappings. Relocating the heap anchor after all sections fixes that problem.
Rebuild csu and relink native executables together with the new user script.

The production kernel changes from 2,192,814 to 2,102,658 bytes (90,156 bytes
saved). Code shrinks from 1,718,469 to 1,678,013 bytes. Loader relocations drop
from 15,787 to 3,262. Data/rodata/BSS permissions are unchanged.

Verification logs and the installed tool/source backups are under
`/tmp/rvm-section-bases/`. The focused section-base fixture executes in VMEmu,
as do the existing PC-relative, final-link, compact-operand, GCC optimization
and bit-operation regressions. `--no-relax` output also matches the previous
linker. Both EARLY boots pass at 64 and 640 MiB, and the production kernel
passes two authenticated root logins at 640 MiB on a disposable disk copy.

The older standalone bootstrap build was also attempted. It stops at the
unchanged `abi-check.c` assertion comparing `mcontext_t` with `struct reg`;
that check predates the current FP context layout. The normal EARLY and
production kernel builds used for this update pass.

The rebuilt native libc/csu/libm, constructors, contexts, TLS, utilities, shell
scripts and interactive job-control suite pass at 64 MiB. Both the default
`./run.sh` kernel/loader and the native-regcall kernel/loader are updated, with
`.before-section-bases` backups. Both persistent guest-disk checksums are
unchanged; the rebuilt native libraries/programs remain in
`obj.rvm-native-regcall` for installation into a new root image.
