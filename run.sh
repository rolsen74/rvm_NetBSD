#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$script_dir/obj.rvm-bootstrap"

for file in bootloader.bin netbsd.fhr vm.config; do
    if [ ! -f "$file" ]; then
        echo "Missing obj.rvm-bootstrap/$file; build the RVM bootstrap first." >&2
        exit 1
    fi
done

echo "RVM kernel: $script_dir/obj.rvm-bootstrap/netbsd.fhr"
exec "${VMEMU:-VMEmu}" -c vm.config "$@"
