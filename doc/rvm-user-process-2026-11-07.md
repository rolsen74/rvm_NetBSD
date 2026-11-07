# NetBSD/RVM real user processes, 2026-11-07

**Later progress:** [disk-root and FHR exec integration](rvm-diskroot-2026-11-07.md)
adds HARDFILE/ld0, an FFS root filesystem and disk-loaded PID 1. The remaining
sections record the earlier resident-program milestone and its limitations.

The EARLY kernel now runs a resident user program through real NetBSD process,
file-descriptor, syscall, UVM, signal, scheduling, exit and wait services. It
prints this from user mode through `sys_write`, `dofilewrite`, `uiomove` and
native console file operations:

```
Hello from a NetBSD/RVM user process via write(2)!
```

The program exits with status 37, which the parent checks and reaps. The kernel
then reaches `root device:` as before. A root filesystem and executable-file
loader have not been added: this is not `/sbin/init`, an execve test or a shell.

## Build and observe

```
./build-kernel.sh
./run-kernel.sh
python3 sys/arch/rvm/tests/kernel_boot.py --timeout 12
```

The default image is `obj.rvm-kernel/netbsd.fhr`. The tests require the actual
user-written message, every earlier integration marker, successful real-process
checks, and the root prompt with no panic/fatal kernel exception.

`EARLY_DIAGNOSTIC` enables MI assertions and runs the same test program.
`GENERIC` now links and boots using the same process implementations, without
boot-time test fixtures. Check it with:

```
./build-kernel.sh GENERIC
python3 sys/arch/rvm/tests/kernel_boot.py --generic
./build-kernel.sh
```

The last command restores the default EARLY image. Both normal configurations
and GENERIC were checked with 64 and 640 MiB RAM. Exception messages from VMEmu
are expected for the deliberate protection tests.

## Native LP64 syscall ABI

- R0 holds the NetBSD syscall number; R2 through R9 hold up to eight arguments.
  Each argument occupies an eight-byte big-endian register-sized slot, matching
  MI `syscallarg` layout. This is distinct from ordinary C stack argument homes.
- R0/R1 hold return values. Carry clear indicates success; carry set indicates
  a positive errno in R0. Other integer registers are preserved.
- `SYS_syscall` and `SYS___syscall` consume the real number from R2 and shift
  arguments. A possible eighth indirect argument is read from USP. Recursive
  indirection and unknown numbers go to `sys_nosys`, without indexing beyond
  the syscall table or treating MI's nine-slot indirect wrapper as a native call.
- SYSCALL is two bytes. VMEmu saves the following PC. `ERESTART` backs PC up by
  two while preserving number/arguments; `EJUSTRETURN` retains restored state.
- `syscall_intern` selects the dispatcher through the real process emulation
  table. Calls use `sy_invoke`, including MI tracing and `l_sysent` handling.

## Trap, IRQ and process returns

`user_entry.S` enters user mode from the canonical MD trapframe at the top of
an LWP's uarea. Machine offsets are asserted against C in `user_trap.c`. Initial
GPRs are cleared by `setregs`; fork uses the saved parent frame and honors a
replacement stack. Each LWP keeps its own USP and user page-table root. An empty forked pmap gets
a real zeroed root before user return, so physical page zero (loader data) can
never be interpreted as its page tables. Allocation failure terminates the
process with SIGKILL.

User hardware frames are normalized into that canonical trapframe. Read/write/
invalid-table faults share the existing scoped kernel-copy vectors; user faults
are forwarded into the process handler while kernel kcopy recovery stays scoped.
Invalid-entry faults do not identify R/W/X, so UVM populates using an allowed
access and the retried instruction checks precise permissions. Unresolvable
accesses become signals; privileged instructions become SIGILL. A user-selected
invalid return context terminates the process instead of panicking the kernel.

IRQs are enabled while MI calls/faults may sleep. Timer IRQs interrupted in user
mode take the same MI reschedule/signal return path. After device bookkeeping,
the hardware interrupt priority is lowered before MI can switch to another LWP;
otherwise the new LWP could inherit a priority that masks its wakeup clock. Return restores user GPRs,
USP and current MUT, and excludes supervisor/halt/priority/reserved SR bits.
The native fork child, spawn and new-LWP hooks use this common return path.

The CPU still runs uncached and uniprocessor. Kernel preemption and SMP have not
been enabled. Profiling, core-file output, FPU/TLS contexts and sysarch commands
remain unsupported or need further work; optional unimplemented requests return
errors rather than EARLY-only fake implementations.

## Signals

Native siginfo trampoline version 2 is implemented. The user signal stack holds
a return address, eight zeroed argument homes, siginfo and ucontext. A C handler
receives signo/info/context in R2/R3/R4. R10 carries the context pointer through
the handler to the user-supplied trampoline; as a normal callee-saved register,
a C handler preserves it. The trampoline calls `setcontext` to restore state.

Delivery supports an alternate stack, saved signal masks, stack-state markers,
and reset-on-delivery handling through MI. Failed frame copies terminate the
process. Supervisor stack/MMU/fault metadata are not exposed in ucontext.

## Resident integration program

`process_smoke.c` forks a real non-system process with a clean descriptor table.
It maps RX code, RW data, a nonresident stack and an alternate signal stack in
UVM, then initializes registers. It bypasses executable-file parsing only.

`console_file.c` supplies real write-only descriptors 0/1/2 backed by GeneralDriver
character output. This permits genuine `write(2)` without a root vnode. It is
not a tty and does not provide terminal input, line discipline or ioctls.

The program verifies getpid, both indirect syscall forms, ENOSYS with ignored
SIGSYS, EBADF, EFAULT, blocking nanosleep, register preservation, user timer IRQ
return, user fork/COW/wait and child exit status. Three further children cause
privilege, kernel-address and read-only-text faults; the parent checks their
SIGILL/SIGSEGV wait status and continues. A user-installed handler runs on an
alternate stack and returns through setcontext. A child then interrupts a
blocking wait with SA_RESTART; the test requires an actual kernel ERESTART and
successful reissue after the signal/context roundtrip.

The EARLY fixture temporarily clears proc0's automatic child-discard policy so
it can wait/reap its own test process, then restores it. Core limits are zeroed
for pre-root fatal-fault tests. No resident test hooks are compiled into GENERIC.

A common MI cleanup mismatch was fixed: `cwdinit` already permits/copies a null
current directory before root exists, but `cwdfree` unconditionally released it.
The latter now guards null, allowing a pre-root process to exit correctly.

VMAsm currently does not reliably resolve the fixture's forward label-difference
assignment as an immediate. The message length uses a shared numeric constant,
checked against its C string and emitted assembly span. No assembler, GCC,
linker or VMEmu binary/source changes were made in this continuation.

## Validation and next milestone

EARLY and EARLY_DIAGNOSTIC boot tests require every user-program check at 64 and
640 MiB. GENERIC checks require normal startup and the root prompt. Relevant logs
are `/tmp/rvm-process-final2-boot.log`,
`/tmp/rvm-process-final2-diagnostic-boot.log`,
`/tmp/rvm-process-final-generic-boot.log` and `/tmp/rvm-process-tools.log`.
All four object-tool execution cases and all nine standalone kcopy checks
passed (`/tmp/rvm-process-kcopy.log`).
The standalone image also rebuilds with the installed GCC; kernel-only paths
are excluded from its shared source files.

Next: add an executable-file format/loader compatible with the FHR toolchain,
a RAM-backed root image, crt0/syscall stubs and a small initial program. Then
exercise actual execve and `/sbin/init`, followed by libc, tty support and a shell.
RVM still has no registered ELF machine ID; another architecture's ID has not
been borrowed. The runtime register layout and syscall ABI now provide concrete
interfaces for that work.
