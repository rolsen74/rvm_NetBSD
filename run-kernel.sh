#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
obj=${RVM_KERNEL_OBJDIR:-"$root/obj.rvm-kernel"}
for file in bootloader.bin netbsd.fhr vm.config; do
    if [ ! -f "$obj/$file" ]; then
        echo "Missing $obj/$file; run ./build-kernel.sh first." >&2
        exit 1
    fi
done
source_config="$root/sys/arch/rvm/conf/vmemu.conf"
if [ "$source_config" -nt "$obj/vm.config" ]; then
    cp "$source_config" "$obj/vm.config"
fi
echo "RVM normal kernel: $obj/netbsd.fhr"
exec python3 "$root/sys/arch/rvm/stand/fhr_run.py" "$obj" "$@"
