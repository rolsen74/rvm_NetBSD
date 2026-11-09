# NetBSD/RVM stdio and native shell — 2026-11-08

The subsequent [FPU/GCC update](rvm-fpu-2026-11-08.md) enables native binary64
arithmetic and full process FP context preservation. Its details supersede
the software-only floating-point description below.

The [libm/fenv](rvm-libm-fenv-2026-11-09.md) and
[static initialization](rvm-static-init-2026-11-09.md) updates also supersede
the pending fenv and constructor support recorded below.

The disk-root build now includes the tree's NetBSD `/bin/sh` in its `SMALL`
configuration, together with native utilities. The PID 1 integration monitor
remains available for bring-up. This extends the earlier
[libc milestone](rvm-libc-2026-11-08.md).

## Using the shell

```sh
./build-diskroot.sh
./run-diskroot.sh
```

At the `rvm#` prompt, type `sh`. The monitor remounts the disposable FFS root
read/write, starts `/bin/sh -i`, and waits for it. For example:

```text
rvm# sh
# ls -l /etc
# echo 'hello from NetBSD' > /tmp/hello
# cat /tmp/hello | cat
# mkdir -p /tmp/example/child
# rm -r /tmp/example
# sleep 0.1
# exit 0
rvm#
```

On shell exit, the monitor syncs the filesystem and remounts it read-only.
Files written by the shell remain available to the monitor and across a normal
subsequent boot. `build-diskroot.sh` regenerates the fixture and replaces its
contents. This is still a disposable development root with no fsck/shutdown
userland; exit the shell before stopping the emulator to finish the read-only
remount. The monitor reports a failed remount, for example if a surviving
background process keeps a file open for writing.

The shell supports quoting, expansions, functions, arithmetic, pipelines,
redirections, here documents, pathname expansion, traps, background jobs and
wait. Ctrl-Z suspends a foreground job; `jobs` and `fg` inspect/resume it;
Ctrl-C terminates the foreground command and returns to the shell. The `SMALL`
build uses canonical tty input without libedit command-line history.

The image contains the unchanged NetBSD sources for `sh`, `echo`, `cat`, `pwd`,
`ls` (`SMALL`, without ACL support), `rm`, `mkdir`, `rmdir`, `sleep`, `sync` and
`yes`. `/bin/rvm-echo` remains a bootstrap diagnostic. The root contains
`/etc/passwd`, `/etc/group`, `/etc/shells`, `/root` and a mode-1777 `/tmp`.

## Library and ABI work

`stand/user/libc.py` compiles NetBSD's FILE implementation, buffered I/O,
formatted output/input, memory streams, temporary files, multibyte conversion,
C/POSIX locale machinery, directory traversal, account-name caches and time
conversion. It uses the nonthreaded paths and the statically linked Citrus
NONE encoding. No locale databases or dynamically loaded encoding modules are
installed; this is not a UTF-8 locale implementation.

Formatting uses gdtoa and imported compiler-rt software IEEE binary32/binary64
arithmetic. RVM `long double` has the same representation as `double`.
Rounding is nearest, ties to even; floating exception flags and selectable
rounding modes are not implemented. A small gdtoa fix makes decimal-point
tracking independent of `Honor_FLT_ROUNDS`, including rejecting `.inf`.
The generated tzcode copy removes a `once_t` macro collision and confines its
mutex path to threaded builds. The original tzcode source is unchanged.

Compiler-rt's 32-bit comparison/count results need adapters because the RVM
GCC libcalls expect a 64-bit word result. The count adapters also avoid the
compiler lowering a 32-bit builtin back to the same 64-bit helper recursively.
GCC now has an SFmode move expansion preserving the 32-bit value in integer
registers. See the [toolchain notes](../sys/arch/rvm/stand/toolchain/README.md).

The RVM glue supplies native syscalls and user signal trampolines. A `vfork`
wrapper removes its return address from the shared stack before entering the
kernel; the parent keeps that address in its saved register state while the
child executes. `setjmp`/`longjmp` save all R10–R62, the caller's SP/PC and,
when requested, the signal mask. FHR entry also passes the MI `ps_strings`
pointer in R5 to crt0 for `setproctitle`.

Account lookups use small files-only adapters around NetBSD's passwd parser;
NSS and the passwd database backends remain unported. The random API adapter
obtains every byte through native `getrandom`, with rejection sampling for
`arc4random_uniform`; it keeps no userspace PRNG state across fork. Assertions
report directly to stderr without requiring syslogd.

This remains a static bootstrap library built by `UserBuilder`, not the full
NetBSD libc/csu distribution build. TLS, pthreads, dynamic linking, constructors,
production init/login, complete libm/fenv and broader ABI conformance remain
future work. Returning from main runs atexit handlers and the actual stdio
cleanup hook. `_exit` skips both.

## Validation

The boot fixture checks integer/positional/stacked varargs, floating formatting
and parsing, NaNs/infinity/subnormals, narrow/wide scanning and streams,
small-buffer file I/O, 64-bit seeks above 8 GiB, EOF/ungetc/error handling,
short writes, memory streams, anonymous temporary files and exit-time flushing.
The large seek does not write or allocate an 8 GiB file.

Nonlocal jump checks overwrite every preserved register, restore the stack and
frame pointer, test zero-to-one return values and saved/unsaved signal masks,
and deliver a user signal through the supplied trampoline. A vfork child
changes shared data and execs a native utility before the parent resumes.

`/etc/sh-check` runs through native sh during boot, checking external utilities,
quoting, arithmetic, functions, pipes, file redirection, here documents,
globbing, traps, child status, background wait and syntax-error recovery.
`ls` checks exercise FTS, mode/account names, timestamps and recursive listing.
The host test reboots the same copied filesystem to check persistence.

```sh
python3 sys/arch/rvm/tests/hardfile.py --objdir obj.rvm-diskroot \
    --diskroot --expect-accel
python3 sys/arch/rvm/tests/console.py --shell --pixels --keypad
python3 sys/arch/rvm/tests/console.py --shell --ram-mib 640
```

The SDL test types into the real tty, writes a file from interactive sh,
checks a pipeline, suspends/resumes/interrupts a job, verifies status 130 for
SIGINT, exits to the monitor, and reads the saved file. Images are copied before
tests, so validation does not modify the user's root fixture.
