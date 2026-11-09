# NetBSD idle HALT, 2026-11-09

The normal RVM kernel now executes HALT when NetBSD has no runnable work,
instead of spinning in cpu_idle. VMEmu's Linux CPU thread already waits on
a condition variable while halted, so this change needs no emulator patch.

On the uniprocessor, cpu_idle masks IRQs and checks both rescheduling requests
and runnable work, including slow softints. The small assembly helper then
enables interrupts and halts, preserving the MMU/cache/TLB control bits.

Timer interrupt return recognizes that assembly window. If an IRQ arrives
after interrupts are enabled but before HALT executes, it advances the return
PC past HALT. If the IRQ wakes a halted CPU, it clears the saved halt flag
before RTE. Both cases return to the scheduler with pending work intact.

The 50 Hz clock remains enabled. It wakes the CPU for clock work, callouts,
softints and the console's keyboard polling. This is an idle sleep change,
not a tickless-clock implementation.

## Rebuild and run

```sh
./build-native-root.sh
cd obj.rvm-native-root/native-root
VMEmu -c vm.config
```

The installed GCC/GAS/VMAsm/VMEmu add/sub update was applied before final
validation. Native libraries, programs, kernel/libkern, resident tests and
the loader were rebuilt, and a new bootstrap/native test filesystem was
generated. The bootloader now depends on the kernel build's toolchain stamp,
so a later compiler/assembler change also invalidates that cached binary.
The bootstrap C builder now uses NetBSD stddef/stdarg headers consistently.

## CPU measurements and wakeups

The Linux host tests sample /proc process accounting over five idle seconds
after reaching the login prompt. This includes all emulator threads. With
the same updated compiler/assembler and guest image, the spin-loop control
used 102.4% of one host core; HALT idle used 6.0%. The figures are headless
measurements and vary with host speed, GUI work and timer overhead.

```sh
python3 sys/arch/rvm/tests/native_root.py --objdir obj.rvm-native-clean \
    --idle-seconds 5 --max-idle-cpu 0.25 --timeout 120
```

An optional --kernel-objdir selects a control kernel against a temporary copy
of the same image. RVM_KERNEL_CPPFLAGS=-DRVM_IDLE_SPIN_TEST restores the old
spin loop for this comparison; it is not enabled in the normal build.

The EARLY kernel checks that an actual HALT wake occurs during a timed CV
wait. RVM_KERNEL_CPPFLAGS=-DRVM_IDLE_RACE_TEST instead delays past a timer
period with IRQs masked immediately before the enable/HALT sequence. That
check requires the IRQ return to skip HALT, exercising the pre-HALT race.
Keep this diagnostic option out of the normal kernel: it deliberately spins
with interrupts masked for the test.

The login integration driver accepts --idle-seconds and --max-idle-cpu too.
It measures idle first, then injects real SDL keyboard input, rejects a bad
password and completes login. This checks that timer/console activity resumes
after sleeping. Use the freshly exported fixture tree when testing after an
opcode-format change; old guest binaries must be regenerated.
