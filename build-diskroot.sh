#!/bin/sh
# Build the disk-root integration kernel and regenerate its disposable root.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$root"
RVM_KERNEL_OBJDIR=${RVM_KERNEL_OBJDIR:-"$root/obj.rvm-diskroot"}
export RVM_KERNEL_OBJDIR
config=DISKROOT
case "${1-}" in
    DISKROOT|DISKROOT_DIAGNOSTIC|DISKROOT_GENERIC|DISKROOT_ICACHE|DISKROOT_ICACHE_DIAGNOSTIC) config=$1; shift ;;
    ''|--*) ;;
    *) echo 'Choose DISKROOT, DISKROOT_DIAGNOSTIC, DISKROOT_GENERIC, DISKROOT_ICACHE or DISKROOT_ICACHE_DIAGNOSTIC.' >&2; exit 1 ;;
esac
./build-kernel.sh "$config"
tools=${RVM_KERNEL_TOOLDIR:-"$RVM_KERNEL_OBJDIR/hosttools"}
for tool in pax makefs; do
    "$tools/bin/nbmake-rvm" -C "tools/$tool" obj
    "$tools/bin/nbmake-rvm" -C "tools/$tool" -j "${RVM_JOBS:-4}" dependall install
done
python3 sys/arch/rvm/stand/make_root.py --objdir "$RVM_KERNEL_OBJDIR" \
    --makefs "$tools/bin/nbmakefs" \
    --toolchain "${RVM_TOOLCHAIN:-$root/../rvm_gcc/rvm_cross_netbsd}" "$@"
echo 'Run ./run-diskroot.sh to boot the disk-init integration fixture.'
