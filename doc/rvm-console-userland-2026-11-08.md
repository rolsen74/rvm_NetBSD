# NetBSD/RVM console and bootstrap user runtime, 2026-11-08

The later [stdio and native-shell update](rvm-stdio-shell-2026-11-08.md)
adds the `sh` monitor command, writable shell sessions and native utilities.
The limitations below describe the earlier bootstrap monitor.

The disk-root image now reaches an interactive `rvm#` command monitor after
the filesystem/exec tests. Keyboard input travels through VMEmu's SDL event
queue, GeneralDriver MMIO, a NetBSD clock callout, MI tty input processing and
a blocking user `read(2)`. The monitor itself is a static FHR C program loaded
by PID 1 through execve. Full NetBSD libc, csu, production init and sh remain
porting work.

```sh
./build-diskroot.sh
./run-diskroot.sh
```

After the checks, type in the VMEmu window:

```text
help
pid
echo hello
cat /etc/motd
run /bin/rvm-echo hello from a child process
run /missing-program
```

`run` forks, execs an absolute pathname and reports the raw wait status. A
missing program returns status 32512 (127 << 8) and leaves the monitor usable.
The parser accepts up to 16 space/tab-separated words on a 255-byte line.
Quoting, expansion, pipelines, redirection and job control are not implemented.
There is no exit command that would accidentally terminate PID 1. Root is
returned to read-only after the startup write tests.

## Console implementation

`console_tty.c` backs character major 500, forwarded by MI `/dev/console`
(major 240). It allocates/attaches a real tty on first open, sets standard
termios defaults, carrier and a 100-column by 75-row size after the initial
line-discipline open. Input polling drains at most 64 hardware events per
tick, ignores releases/special keys and maps SDL Backspace to the default tty
erase byte. Output drains bounded 128-byte bursts through the standard tty
restart callout, including wakeup of writers when queues drain. Close stops
input polling and closes the line discipline. Early kernel console output
remains available; cn_pollc prevents the tty poller consuming polled-console
input during boot prompts.

Canonical editing, echo, EOF, raw mode, termios ioctls, window-size ioctls,
poll and kqueue entry points now use MI tty routines. The keyboard is polled;
its hardware IRQ is not enabled. The subsequent VMEmu input fix translates
Shift/Caps Lock letters, basic US-layout shifted punctuation and Ctrl letters,
and forwards SDL key-repeat events. Non-ASCII text/layout handling remains
future work.

The framebuffer console now treats Backspace as cursor movement. MI tty's
BS/space/BS erase sequence therefore clears the actual glyph and restores
the drawing position. Bell is ignored instead of drawing a blank cell.
`console.py --pixels` captures SDL-rendered pixels before typing, after typing
and after erase, checking that the glyph appeared and that erasing restores
the original image exactly. Uppercase input is injected as lowercase SDL
keycodes with Shift modifiers, matching real SDL events.

VMEmu also translates the standard numeric keypad: digits and decimal with
Num Lock enabled, plus slash, multiply, minus, plus and Enter independently
of Num Lock. Keypad equals/comma/tab/backspace are supported where present.
Without Num Lock the digit keys produce VMEmu navigation codes; the current
tty still ignores navigation codes. The source fix is in the sibling
`rvm_VMEmu/gui_SDL2/GUI_Win_Keyboard.c`, and the tested executable is installed
as `~/bin/VMEmu`. Restart VMEmu to use it.

`console.py --keypad --pixels` injects actual SDL keypad keycodes for digits,
operators, Enter and Backspace, including the slashes in `cat /etc/motd`.
It passed at 64 MiB with pixel erase checks, and `--keypad --ram-mib 640`
also passed. The converter unit test covers both Num Lock states and keypad
operators with Shift. Build/test logs and the previous installed executable
are retained under `/tmp/rvm-keypad-z254i82t/`.

## Static bootstrap runtime

`sys/arch/rvm/stand/user/` contains crt0, a syscall bridge, `librvm.a`, a small
header/API and a standalone builder. crt0 reads the MI argc/argv/envp stack,
initializes environ/progname, calls ordinary C main and exits with its result.
The syscall bridge moves the C ABI's number/arguments to native syscall
registers, including the eighth argument from the caller's stack, and converts
carry/errno to an internal negative result. C wrappers translate failures to
-1 and a single-threaded errno location. Versioned NetBSD fstat/nanosleep names
match the source headers.

The runtime supplies basic descriptor, file, mmap, pipe, process, tty and sleep
calls plus puts and NetBSD's unmodified MI strlen, strcmp, memcpy, memmove and
memset routines. It supports variadic open
mode arguments and a native variadic ioctl assembly entry, preserving optional
arguments without a C va_arg read for argument-free requests. The image also
provides `/dev/tty`, `/dev/null` and `/dev/zero` through NetBSD's standard
character devices. It now also includes NetBSD's allocator, environment and
exit-handler code; see the [libc progress notes](rvm-libc-2026-11-08.md).
It has no TLS, FILE streams, printf, locale, constructors, dynamic linker or
thread support. This library is a bootstrap subset separate from NetBSD's
full libc build.

Build and install a small C program:

```c
#include "rvm_user.h"
int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    return puts("Hello from my native RVM program") < 0;
}
```

```sh
python3 sys/arch/rvm/stand/user/build.py -o /tmp/hello.fhr /tmp/hello.c
./build-diskroot.sh --install /tmp/hello.fhr /bin/hello
./run-diskroot.sh
# In the VMEmu window:
# run /bin/hello
```

The builder uses the sibling external compiler and existing FHR assembly
adapters. Object/library artifacts go to obj.rvm-user by default; kernel
builds are not needed to compile the C program. Installing regenerates the
disposable root image, replacing its previous contents. Repeated --install
options are allowed; guest destinations must be absolute paths without `..`.
Disk-root builds also export crt0.o, librvm.a and end.o under
obj.rvm-diskroot/user. When linking manually, crt0.o must come first and end.o
must come after all application objects and libraries: it marks the heap floor.

## Validation

```sh
python3 sys/arch/rvm/tests/hardfile.py --objdir obj.rvm-diskroot --diskroot
python3 sys/arch/rvm/tests/console.py
python3 sys/arch/rvm/tests/console.py --ram-mib 640
python3 sys/arch/rvm/tests/console.py --pixels
python3 sys/arch/rvm/tests/console.py --keypad --pixels
```

The in-guest C test verifies standard tty ioctls, window size, nonblocking
EWOULDBLOCK, canonical erase/newline/EOF, raw reads, poll readiness, and a
blocking read awakened by input from a sleeping child. It uses authorized
TIOCSTI injection through the normal tty ioctl path for deterministic headless
checks. The independent host test injects paced SDL keydown/up events through
a temporary LD_PRELOAD shim, then types monitor commands and verifies actual
editing, file reads, child execution/status and recovery after failed exec.
It changes no VMEmu source or installed binary.

The runtime test verifies main arguments/environment, progname, errno and its
preservation on success, variadic open mode using fstat, write/read/seek,
anonymous mmap zero-fill/protection cleanup, pipe/fork/read/wait and C return
to exit. It checks the reused MI memory/string routines, including overlapping
memmove. The monitor establishes a session/controlling tty and verifies its
foreground process group through both the console and `/dev/tty`. Null and
zero device reads/writes and no-argument ioctl error handling are also tested.
A separate CLI-built hello program was installed into a temporary
image and executed from the monitor through the keyboard test.

Final logs: `/tmp/rvm-tty-verified-diag.log`,
`/tmp/rvm-tty-verified-generic.log`, `/tmp/rvm-tty-verified-normal.log`,
`/tmp/rvm-tty-verified-normal-keyboard.log`,
`/tmp/rvm-tty-verified-normal-keyboard640.log`,
`/tmp/rvm-tty-verified-custom.log`, plus each object tree's hardfile-tests and
console-keyboard.log. The raw disk RW/RO and absent-disk regressions passed in
`/tmp/rvm-tty-raw-final.log` and `/tmp/rvm-tty-nodisk-final.log`.
Root images are copied into
temporary test directories before boot; persistence is checked by rebooting
the same test image.
