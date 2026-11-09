#!/bin/sh
# Build a native static login image using the production disk-root kernel.
set -eu
case "${1:-}" in
    -h|--help)
        echo 'Usage: ./build-native-root.sh [object-directory]'
        echo 'Optional environment: RVM_TOOLCHAIN, RVM_JOBS, RVM_MASTER_PASSWD'
        exit 0;;
esac
if [ "$#" -gt 1 ]; then
    echo 'Expected at most one object-directory argument' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$root"
obj=${1:-${RVM_NATIVE_OBJDIR:-"$root/obj.rvm-native-root"}}
cross=${RVM_TOOLCHAIN:-"$root/../rvm_gcc/rvm_cross_netbsd"}
mkdir -p "$obj"
obj=$(CDPATH= cd -- "$obj" && pwd)
./build-userland.sh --objdir "$obj" --toolchain "$cross" -j "${RVM_JOBS:-4}" \
    --libm --library lib/libutil --library lib/libterminfo --library lib/libedit \
    --library lib/libcrypt --library lib/libprop -V USE_PAM=no -V USE_KERBEROS=no -V USE_SKEY=no \
    --program bin/sh --program bin/echo --program bin/cat --program bin/pwd \
    --program bin/sync --program bin/ls --program bin/test --program bin/mkdir \
    --program bin/cp --program bin/mv --program bin/rm --program bin/stty \
    --program bin/sleep --program usr.bin/yes --program usr.bin/id \
    --program sbin/init --program libexec/getty --program usr.bin/login \
    --program usr.bin/passwd --program usr.sbin/pwd_mkdb \
    --program sbin/fsck --program sbin/fsck_ffs --program sbin/mount_tmpfs --program sbin/mount_ptyfs \
    --program sbin/mount --program sbin/mount_ffs --program sbin/umount --program sbin/reboot
RVM_KERNEL_OBJDIR="$obj/kernel" RVM_KERNEL_TOOLDIR="$obj/hosttools" \
    RVM_TOOLCHAIN="$cross" ./build-kernel.sh DISKROOT_GENERIC
set -- --objdir "$obj" --kernel-objdir "$obj/kernel"
if [ -n "${RVM_MASTER_PASSWD:-}" ]; then
    set -- "$@" --master-passwd "$RVM_MASTER_PASSWD"
fi
exec python3 sys/arch/rvm/stand/make_native_root.py "$@"
