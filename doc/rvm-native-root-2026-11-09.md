# Minimal native login image, 2026-11-09

The port now boots a minimal image containing ordinary BSD-make NetBSD
init, sh, getty, login and utilities, linked against the native static libc.
It uses `DISKROOT_GENERIC`, without the EARLY kernel self-tests or bootstrap
user runtime. The root is a 32 MiB big-endian UFS2 filesystem on ld0a.

```sh
./build-native-root.sh
./run.sh
```

`run.sh` selects `obj.rvm-native-root/native-root`, falling back to the current
`obj.rvm-native-clean/native-root` build when the standard image has not been
generated. Set `RVM_NATIVE_OBJDIR` to select another object directory. It uses
the generated VM configuration so the root disk stays attached.

An optional first argument selects another object directory. `RVM_TOOLCHAIN`
and `RVM_JOBS` select the external compiler prefix directory and parallelism.
The script builds native libc/csu/libm, libutil, terminfo, libedit and libcrypt,
the installed programs, the production kernel configuration, and the image.
It also builds libprop and the filesystem/account maintenance tools.
Its output directory contains `netbsd.fhr`, `bootloader.bin`, `root-native.img`
and `vm.config`.

The minimal `/etc/rc` checks the root filesystem with fsck in preen mode, then
invokes the native mount command to remount `/` writable using `/etc/fstab`
and mount_ffs. It mounts tmpfs on `/tmp` (16 MiB limit) and ptyfs on `/dev/pts`.
NetBSD init then enters multiuser mode and
starts getty on `/dev/console`. The image includes core file utilities, stty,
id, sleep, yes, mount/umount, passwd/pwd_mkdb, fsck/fsck_ffs and reboot, plus
their standard command aliases. See the [storage/terminal update](rvm-storage-terminals-2026-11-09.md)
for the mapping, disk-I/O, password-change and PTY regressions.
The reboot syscall currently shuts down and halts the RVM kernel; restarting
the virtual machine remains separate work.

The default root account is locked. To supply a guest account database, set
`RVM_MASTER_PASSWD` to a NetBSD master.passwd input file when building. The
builder copies that input into its temporary image tree and invokes the host
nbpwd_mkdb tool with big-endian output. It preserves secure database modes and
sets the guest login executable's setuid mode through filesystem metadata.
It does not modify host accounts. Login uses traditional libcrypt authentication;
PAM, Kerberos and S/Key are disabled in this static configuration. The normal
Argon2id passwd.conf is installed; a password change and subsequent login have
been tested entirely inside the guest.

The [native library notes](rvm-native-libc-2026-11-09.md) describe the shared
build/link adapters and their remaining build-tree SDK restrictions.

## Validation

```sh
python3 sys/arch/rvm/tests/native_root.py
python3 sys/arch/rvm/tests/native_root.py --ram-mib 640
```

These checks boot temporary copies of the image, require successful rc/root
remount and a getty login prompt, and reject kernel panics. Both the 64 and
640 MiB checks pass. The build was exercised in `obj.rvm-native-clean`; select that tree
with `--objdir obj.rvm-native-clean` when using those local artifacts.

The separate `tests/native_init.py --login` fixture uses a disposable known
password/hash and generated big-endian databases. It verifies incorrect-password
rejection, successful login, root identity, environment and controlling tty.
Authentication passes at 64 and 640 MiB. When normal mount, mount_ffs, id and
stty artifacts are present, that fixture also runs them during rc; this path
passes at 64 MiB. The full shell passes scripted and interactive job-control
tests, and the native libc/libm suite passes at 64 and 640 MiB.

This is a minimal static login image, not a NetBSD distribution/release build.
Remaining integration includes full rc.d/services and more filesystem tools,
more libraries/programs, networking, native TLS/pthreads, dynamic linking and a
standalone installable SDK. The original `build-diskroot.sh` fixture remains
available for the detailed bootstrap regression tests.
