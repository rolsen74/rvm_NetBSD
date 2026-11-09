# Native storage, terminals and password tools, 2026-11-09

The native login image now includes passwd/pwd_mkdb, fsck/fsck_ffs and the
tmpfs/ptyfs mount helpers. GENERIC enables TMPFS, PTYFS and pseudo-terminals.
These build with the normal NetBSD makefiles, using the native libc, libutil,
libcrypt and libprop catalogs.

The image's rc checks the root filesystem in preen mode before remounting
it writable. It then mounts a 16 MiB tmpfs on `/tmp` and ptyfs on `/dev/pts`,
using `/etc/fstab`. The image supplies the raw disk nodes, ptm/ptmx nodes and
the tty group. The normal NetBSD passwd.conf selects Argon2id. Both login and
passwd have their normal guest setuid mode; the initial root account remains
locked unless a master.passwd file is supplied at build time.

```sh
./build-native-root.sh
```

Rebuild with the same object-directory argument or RVM_NATIVE_OBJDIR used
for the previous image. Password changes made inside a running guest update
that image's account databases; regenerating the image uses the build-time
master.passwd input again.

## Fault handling correction

The tmpfs regression exposed an RVM user-fault bug: reading a file mapping
past its new EOF after truncation delivered SIGSEGV. Invalid-table handling
also retried other access modes after pager errors and could replace EINVAL
with a later permission error. It now retries only EACCES and converts the
pager's EINVAL to SIGBUS/BUS_ADRERR, retaining the fault address. Ordinary
missing mappings and protection violations retain their SIGSEGV behavior.

## Guest validation

The integration driver uses temporary image copies and generated test accounts:

```sh
python3 sys/arch/rvm/tests/native_init.py --objdir obj.rvm-native-clean \
    --kernel-objdir obj.rvm-native-clean/kernel \
    --fixture-tree obj.rvm-native-clean/fixture-tree \
    --login --change-password --password-cipher argon2id --timeout 240

python3 sys/arch/rvm/tests/native_init.py --objdir obj.rvm-native-clean \
    --kernel-objdir obj.rvm-native-clean/kernel \
    --fixture-tree obj.rvm-native-clean/fixture-tree \
    --fsck --tmpfs --pty --ram-mib 640 --timeout 240
```

The fixture tree is exported by the native libc suite with `--export-tree`;
select your own native object/kernel directories when using another build.

Password tests pass at 64 MiB with bcrypt and with the normal Argon2id
configuration. They log in, change the password, invoke native pwd_mkdb via
vfork/exec, log out, reject the old password, and accept the new one. The final
process checks identity, environment, controlling tty, hash type and secure
database permissions.

Forced read-only UFS2 checks pass through the raw character disk interface,
including all five fsck phases and dispatch through the fsck frontend.
Tmpfs checks cover sparse/zeroed files, shared/private mmap, fork visibility,
truncation SIGBUS/code/address, open unlinked files, size limits and capacity
reclamation. PTY checks cover dynamic allocation, node permissions, window
size, raw/canonical transfer, poll/select, forkpty, controlling sessions and
shell execution. The storage/PTY checks pass at 64 and 640 MiB.

This adds local userland facilities; networking, native TLS/pthreads, dynamic
linking and full rc.d/service integration remain separate work.
