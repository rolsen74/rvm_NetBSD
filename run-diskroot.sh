#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RVM_KERNEL_OBJDIR=${RVM_KERNEL_OBJDIR:-"$root/obj.rvm-diskroot"}
export RVM_KERNEL_OBJDIR
for file in root-fixture.img bootloader.bin netbsd.fhr vm.diskroot.config; do
    if [ ! -f "$RVM_KERNEL_OBJDIR/$file" ]; then
        echo "Missing $file; run ./build-diskroot.sh first." >&2
        exit 1
    fi
done
cd "$RVM_KERNEL_OBJDIR"
echo "RVM disk-root kernel: $RVM_KERNEL_OBJDIR/netbsd.fhr"
exec "${VMEMU:-VMEmu}" -c vm.diskroot.config "$@"
